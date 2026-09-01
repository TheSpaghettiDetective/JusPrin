#pragma once

// The portable, versioned semantic state of a JusPrin project: conversations,
// their messages and tool activity records, the linear editable revision and
// build timeline, and the non-revertible physical-print ledger. Serialized as
// Auxiliaries/JusPrin/state.json inside the
// project archive and mirrored to the local recovery store.
//
// The document is backed by one JSON tree that is edited in place, so
// optional fields written by other (newer) builds survive a load-edit-save
// cycle untouched. Every entry carries a monotonically increasing global
// sequence number `seq`; "later than revision R" is defined as seq > R.seq
// across every conversation, which is what Revert here truncates. GUI-free.

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
};

struct RevisionInfo
{
    std::string   id;
    std::uint64_t seq{0};
    std::string   created_at;
    std::string   cause;            // human-readable reason summary
    std::string   snapshot_file;    // relative to the JusPrin data dir; empty when capture failed
    std::string   conversation_id;  // conversation active when the change happened
    std::string   after_message_id; // last message at capture time (may be empty)
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
    // Add-only immutable records. Builds and copies belong to the editable
    // revision timeline; physical prints belong to the separate factual
    // ledger and are never removed by Revert.
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

    // -- Revisions ----------------------------------------------------------
    // The id add_revision will assign next — lets a caller name the snapshot
    // file before recording the revision entry.
    std::string peek_next_revision_id() const;
    std::string add_revision(const std::string& cause,
                             const std::string& snapshot_file,
                             const std::string& conversation_id,
                             const std::string& timestamp);
    std::vector<RevisionInfo> revisions() const;
    std::optional<RevisionInfo> find_revision(const std::string& revision_id) const;
    std::string current_revision_id() const;
    // Backfills the checkpoint of a revision whose capture failed at the
    // time (e.g. before the canvas could render); true when the revision
    // exists and had no snapshot.
    bool set_revision_snapshot(const std::string& revision_id, const std::string& snapshot_file);

    struct TruncateResult
    {
        std::vector<std::string> removed_snapshot_files;
        std::vector<std::string> kept_snapshot_files;
        // Attachment blob directories (relative to the JusPrin data dir) that
        // the truncation orphaned or preserved; the storage layer deletes the
        // former and copies the latter forward across a project replacement.
        std::vector<std::string> removed_attachment_dirs;
        std::vector<std::string> kept_attachment_dirs;
    };
    // Removes every editable entry (messages, activities, revisions, builds,
    // exported copies, composer state) with seq greater than the revision's
    // across all conversations, and makes it current. Physical prints remain.
    std::optional<TruncateResult> revert_to_revision(const std::string& revision_id);

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
