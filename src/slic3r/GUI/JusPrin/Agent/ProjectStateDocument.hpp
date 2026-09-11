#pragma once

// The portable, versioned semantic state of a JusPrin project: conversations,
// their messages and tool activity records, attachments, and the
// manufacturing history (builds, exported copies, physical prints).
// Serialized as Auxiliaries/JusPrin/state.json inside the project archive and
// mirrored to the local recovery store.
//
// The document is backed by one JSON tree that is edited in place, so
// optional fields written by other (newer) builds survive a load-edit-save
// cycle untouched. Every entry carries a monotonically increasing global
// sequence number `seq`, which orders entries across every conversation.
// GUI-free.

#include "AgentProtocol.hpp"
#include "ManufacturingHistory.hpp"
#include "ToolExecution.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct ConversationInfo
{
    std::string id;
    std::string title;
    std::string created_at;
    std::string updated_at;
    std::string preview;
    std::uint64_t activity_seq{0};
};

// One entry of the change log: an edit the workspace reported, stored raw.
// The Agent page groups consecutive entries for display; nothing here merges,
// words, or delays them, so the grouping can change without touching saved
// data.
struct ChangeEntry
{
    std::uint64_t seq{0};
    std::string   created_at;
    std::string   kind;            // step|undo|redo|setting|preset
    std::string   actor;           // person|agent
    std::string   label;           // as the workspace reported it; may be empty
    std::string   from, to, preset; // setting only
    std::string   conversation_id; // the conversation active at the time
    std::string   after_id;        // the conversation item it follows; empty before the first
};

// Metadata for one attachment. The blob lives under
// <JusPrin data dir>/attachments/<id>/<stored_name>; this record is the
// portable, saved description of it. `state` is "staged" while the attachment
// sits in the composer and "sent" once a durable user message owns it.
struct AttachmentRecord
{
    std::string   id;            // "a-<n>", also the blob subdirectory name
    std::uint64_t seq{0};
    std::string   client_id;     // page-supplied stable ID, for resend dedup
    std::string   original_name; // display name as chosen by the user
    std::string   stored_name;   // sanitized file name written to disk
    std::string   kind;          // text|image|svg|pdf|gcode|model|unsupported
    std::string   mime;
    std::uint64_t size_bytes{0};
    std::string   source;        // picker|drop|clipboard|project
    std::string   state;         // staged|sent|error
    std::string   preview_text;  // decoded text preview (may be truncated)
    std::string   preview_data_url; // small image thumbnail data URL
    std::string   summary;       // native, non-binary model summary
    std::string   content_hash;  // opaque content hash, when computed
    std::optional<AgentError> error;

    // Relative (to the JusPrin data dir) directory holding the blob.
    std::string relative_dir() const { return "attachments/" + id; }
    // Relative path of the blob itself.
    std::string relative_path() const { return relative_dir() + "/" + stored_name; }
};

class ProjectStateDocument
{
public:
    static constexpr int kSchemaVersion = 2;

    enum class LoadResult { Loaded, Migrated, Corrupt };

    ProjectStateDocument();

    // Corrupt input keeps a fresh empty document (the caller decides whether
    // to adopt a new identity); an older schema is migrated in place.
    LoadResult load(const std::string& json_text);
    std::string dump() const;

    // Monotonic per-document change counter, bumped by every mutating call.
    // The recovery mirror with the higher value is the newer state.
    std::uint64_t doc_revision() const;

    bool        has_identity() const;
    std::string project_id() const;
    std::string lineage_id() const;

    // Creates the identity and the first conversation.
    void initialize_identity(const std::string& project_id, const std::string& lineage_id, const std::string& timestamp);

    // -- Conversations ------------------------------------------------------
    std::vector<ConversationInfo> conversations() const;
    std::string                   active_conversation_id() const;
    std::string create_conversation(const std::string& title, const std::string& timestamp); // returns id, makes it active
    bool        set_active_conversation(const std::string& conversation_id);
    bool        needs_conversation_title(const std::string& conversation_id) const;
    bool        rename_conversation(const std::string& conversation_id, const std::string& title, bool generated = false);
    // The agent's restatement of what this chat asked the setup to be, in the
    // user's own words. Written by a settings change, never by talking, and
    // kept for the life of the chat: it is the record of a delegation, not a
    // description of the current config. Empty until the first change.
    std::string setup_intent(const std::string& conversation_id) const;
    bool        set_setup_intent(const std::string& conversation_id, const std::string& intent);
    // Erases chat content and returns orphaned attachment directories. Builds,
    // exported copies and physical prints retain their historical
    // conversation IDs.
    std::optional<std::vector<std::string>> delete_conversation(const std::string& conversation_id,
                                                               const std::string& timestamp);

    // -- Messages -----------------------------------------------------------
    std::string allocate_message_id();
    std::string allocate_action_id();
    std::string allocate_attachment_id();

    void append_message(const std::string& conversation_id, const ConversationMessage& message, const std::string& timestamp);
    bool update_message(const std::string& conversation_id, const ConversationMessage& message);
    std::vector<ConversationMessage> messages(const std::string& conversation_id) const;
    // The conversation that holds the message, or empty.
    std::string conversation_of_message(const std::string& message_id) const;
    std::optional<std::string> client_message_lookup(const std::string& client_message_id) const;

    // -- Tool activities ----------------------------------------------------
    void upsert_activity(const ToolActivity& activity, const std::string& timestamp);
    std::vector<ToolActivity> activities() const;
    // Marks every non-terminal stored state (a crash mid-run) cancelled and
    // every streaming message stopped; returns whether anything changed.
    bool normalize_interrupted_state();

    // -- Attachments --------------------------------------------------------
    // Records are added while staged and flipped to "sent" when a user message
    // adopts them. add/update preserve unknown fields written by other builds.
    void add_attachment(const AttachmentRecord& record, const std::string& timestamp);
    bool update_attachment(const AttachmentRecord& record);
    std::vector<AttachmentRecord> attachments() const;
    std::optional<AttachmentRecord> find_attachment(const std::string& attachment_id) const;
    std::optional<AttachmentRecord> find_attachment_by_client_id(const std::string& client_id) const;
    // Removes a staged attachment record; returns its relative_dir for blob
    // cleanup, or nullopt when it does not exist or is not staged.
    std::optional<std::string> remove_staged_attachment(const std::string& attachment_id);
    // Flips the given staged attachments to "sent". IDs that are missing or
    // not staged are ignored; returns the IDs that were actually sent.
    std::vector<std::string> mark_attachments_sent(const std::vector<std::string>& attachment_ids);

    // -- Manufacturing history --------------------------------------------
    // Add-only immutable records.
    std::string add_build(BuildRecord record, const std::string& timestamp);
    std::string add_exported_copy(ExportedCopyRecord record, const std::string& timestamp);
    std::string add_physical_print(PhysicalPrintRecord record, const std::string& timestamp);
    std::vector<BuildRecord>         builds() const;
    std::vector<ExportedCopyRecord>  exported_copies() const;
    std::vector<PhysicalPrintRecord> physical_prints() const;
    // Ledger size without materializing every record; the shell's status row
    // reads this on every project change.
    std::size_t                      physical_print_count() const;
    std::optional<BuildRecord>       find_build(const std::string& build_id) const;
    std::optional<BuildRecord>       latest_build() const;

    // -- Change log -----------------------------------------------------------
    // Appends with the next seq, placed in the active conversation after its
    // most recent item. Returns the entry as stored.
    ChangeEntry              add_change(ChangeEntry entry, const std::string& timestamp);
    std::vector<ChangeEntry> changes() const;
    // The conversation's most recent message or tool activity, by seq; empty
    // when it has none.
    std::string last_item_id(const std::string& conversation_id) const;

    // Raw access for tests and serialization helpers.
    const nlohmann::json& raw() const { return m_doc; }

private:
    nlohmann::json*       conversation_json(const std::string& conversation_id);
    const nlohmann::json* conversation_json(const std::string& conversation_id) const;
    std::uint64_t         next_seq();
    void                  touch();

    nlohmann::json m_doc;
};

} // namespace Slic3r::GUI::JusPrin::Agent
