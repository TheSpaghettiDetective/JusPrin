#pragma once

// The current semantic document of a JusPrin project: conversations,
// their messages and tool activity records, attachments, and the
// manufacturing history (builds, exported copies, physical prints).
// Managed projects save it independently of model checkpoints in
// current-state.json.
//
// The document is backed by one JSON tree that is edited in place, so
// optional fields written by other (newer) builds survive a load-edit-save
// cycle untouched. Every entry carries a monotonically increasing global
// sequence number `seq`, which orders entries across every conversation.
// GUI-free.

#include "AgentProtocol.hpp"
#include "ManufacturingHistory.hpp"
#include "ToolExecution.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

// Which provider and model a chat talks to, and at what thinking level. The
// person changes it from the composer at any point between turns. A chat
// without one uses the app's default provider and that provider's default
// model.
struct ChatModelChoice
{
    std::string provider;
    std::string model;
    std::string effort; // empty: the model's own default
    // For an account provider's alias, the wire format the chat's first
    // request was served in. A chat keeps it for its whole life, so the
    // gateway assigning the person another backend reaches new chats only.
    // Empty until that first request, and for every other provider.
    std::string format;

    bool operator==(const ChatModelChoice& other) const
    {
        return provider == other.provider && model == other.model && effort == other.effort && format == other.format;
    }
    bool operator!=(const ChatModelChoice& other) const { return !(*this == other); }
};

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
    bool          external{false}; // an agent edit an external tool made, through MCP
    std::string   client;          // external only: the MCP client's name for itself; may be empty
    std::string   label;           // as the workspace reported it; may be empty
    std::string   location;        // optional place of a hand edit, when known
    std::string   from, to, preset; // setting only
    std::string   key;              // setting only
    std::string   conversation_id; // the conversation active at the time
    std::string   after_id;        // the conversation item it follows; empty before the first
};

// One piece of work the application ran outside the chat: a slice, an arrange,
// an export. The entry is written when the work starts, which fixes its place
// in the timeline: after the conversation item that was last at that moment,
// so a message that arrives later reads below it. It is completed in place
// when the work ends. Progress is not saved; an entry still "running" in a
// document that was closed mid-run is shown as interrupted.
struct WorkEntry
{
    std::uint64_t seq{0};
    std::string   id;
    std::string   started_at, ended_at;
    std::string   kind;            // slice|job|export|upload|download
    std::string   title;           // what is running, in the app's words
    std::string   state;           // running|done|cancelled|failed
    std::string   detail;          // why it failed; never a folder path
    std::string   conversation_id; // the conversation active when it started
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

// One decision the agent made and is prepared to defend.
struct PlanDecision
{
    std::string topic;       // orientation, supports, material, ...
    std::string statement;   // the decision and its reasoning, in the user's terms
    std::string confidence;  // the agent's own confidence in it
    std::string alternative; // what else was considered, and why it lost
};

// The agent's pinned statement of how it means to print this project: what it
// decided, what it assumed without being able to check, and what could still
// go wrong. The agent's own words, not a description of the configuration --
// the configuration is read from the presets, which cannot say why.
struct PlanRecord
{
    std::uint64_t             seq{0};
    std::string               updated_at;
    std::string               headline;
    std::vector<PlanDecision> decisions;
    std::vector<std::string>  assumptions;
    std::vector<std::string>  risks;
    std::string               validity{"current"};
    std::string               invalidated_by;
};

class ProjectStateDocument
{
public:
    // One schema, no migration (the project is not live yet).
    static constexpr int kSchemaVersion = 1;

    enum class LoadResult { Loaded, Corrupt };

    ProjectStateDocument();

    // Corrupt input, or a document of another schema version, keeps a fresh
    // empty document; the caller decides whether to adopt a new identity.
    LoadResult load(const std::string& json_text);
    std::string dump() const;

    // Monotonic per-document change counter, bumped by every mutating call.
    std::uint64_t doc_revision() const;

    bool        has_identity() const;
    std::string project_id() const;
    std::string lineage_id() const;

    // Creates the identity and the first conversation.
    void initialize_identity(const std::string& project_id, const std::string& lineage_id, const std::string& timestamp);
    // An imported copy starts a distinct managed project while keeping its
    // conversations and manufacturing history.
    void fork_identity(const std::string& project_id, const std::string& timestamp);
    void set_draft(const std::string& draft);
    std::string draft() const;

    // -- Conversations ------------------------------------------------------
    std::vector<ConversationInfo> conversations() const;
    std::string                   active_conversation_id() const;
    std::string                   viewed_conversation_id() const;
    // Returns the id and makes the chat active. It starts with a copy of the
    // active chat's intent (setup title, purpose setup), which
    // is then its own to change, and with no plan.
    std::string create_conversation(const std::string& title, const std::string& timestamp);
    bool        set_active_conversation(const std::string& conversation_id);
    bool        set_viewed_conversation(const std::string& conversation_id);
    // What only a checkpoint can hold: the model version the chat was left
    // on, the conversation item it was left after, when, and the card's
    // summary of that moment. The chat's intent and plan stay on the chat,
    // which nothing edits while it is inactive. A missing checkpoint means
    // the chat was left with no model version to bind it to.
    std::optional<nlohmann::json> chat_checkpoint(const std::string& conversation_id) const;
    bool save_chat_checkpoint(const std::string& conversation_id, const std::string& version_id,
                              const nlohmann::json& summary, const std::string& timestamp);
    bool        needs_conversation_title(const std::string& conversation_id) const;
    bool        rename_conversation(const std::string& conversation_id, const std::string& title, bool generated = false);
    // The agent's restatement of what this chat asked the setup to be, in the
    // user's own words. Written by a settings change, never by talking, and
    // kept for the life of the chat: it is the record of a delegation, not a
    // description of the current config. Empty until the first change.
    std::string setup_intent(const std::string& conversation_id) const;
    // What makes this a calibration project (handoff 5.1): {startedFrom,
    // filament, printer, symptom, handover (the note that opened its chat)},
    // and once the test is made {test, method, start, end, step, generatedAt}
    // and the result saved from it {result, savedAs}. Kept with the project
    // because OrcaSlicer keeps the test's steps only in memory, so a reopened
    // test can say they are gone (board 7).
    std::optional<nlohmann::json> calibration() const;
    void                          set_calibration(nlohmann::json calibration);
    bool        set_setup_intent(const std::string& conversation_id, const std::string& intent);
    // The chat's own model choice, kept with the chat so reopening the
    // project resumes it. Absent until the person picks one.
    std::optional<ChatModelChoice> chat_model(const std::string& conversation_id) const;
    bool                           set_chat_model(const std::string& conversation_id, const ChatModelChoice& choice);
    // Erases chat content and returns orphaned attachment directories. Builds,
    // exported copies and physical prints retain their historical
    // conversation IDs. Deleting the active chat makes a replacement that
    // starts as create_conversation's does, from the deleted chat's intent.
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

    // -- Print intent and plan ------------------------------------------------
    // Product state with no Orca owner, and not in Orca's undo stack. Each
    // chat owns its own intent and plan; the project's current ones are the
    // active chat's, read through activeConversationId and stored nowhere
    // else. Every call below reads or writes the active chat; writing with no
    // active chat is a logic error.
    // A chat's printIntent, the fields setup_record kept before 2026-10-25,
    // is no longer read or copied; a document that holds one loads as before.
    // The setup in force (setup_in_force's shape: presets and plate type)
    // when the model was last asked to check it against the purpose: when
    // setup_record recorded a purpose for the first time or changed it, or
    // a request carried the setup or file-loaded skill under a recorded
    // purpose. An empty object when that is not known. It belongs with the
    // intent, and a new chat copies it with the intent.
    nlohmann::json purpose_setup() const;
    void           set_purpose_setup(nlohmann::json setup);
    // Whether the person restored an earlier version after the model last
    // replied anywhere in the project: a restore in the change log with no
    // assistant message after it other than `except_message_id`, the reply
    // being written now.
    bool restored_since_reply(const std::string& except_message_id) const;
    // The pinned plan, or a default record when the agent has not written one.
    PlanRecord plan() const;
    // A given chat's plan, for its saved card.
    PlanRecord plan(const std::string& conversation_id) const;
    // Replaces the plan wholesale: it is one statement, not a list of edits.
    PlanRecord set_plan(PlanRecord record, const std::string& timestamp);
    bool mark_plan_needs_reassessment(const std::string& reason);
    // Region annotations of the model in the workspace. They label modifier
    // volumes, paint and overrides that live in the model, so they belong
    // with it, not with a chat: every saved model version records the list it
    // was saved with (ProjectVersionStore), and restoring or resuming that
    // version puts its list back here. The list is replaced whole; a record
    // with no seq is new or changed and is stamped on the way in.
    std::vector<Workspace::RegionRecord> regions() const;
    std::vector<Workspace::RegionRecord> set_regions(std::vector<Workspace::RegionRecord> records, const std::string& timestamp);
    // Puts back the list a model version recorded, as it was stored.
    void restore_regions(const nlohmann::json& regions);
    // Restoring a model version otherwise keeps this document as it is: the
    // chats with their intent and plans, and the manufacturing audit.

    // -- Change log -----------------------------------------------------------
    // Appends with the next seq, placed in the active conversation after its
    // most recent item. Returns the entry as stored.
    ChangeEntry              add_change(ChangeEntry entry, const std::string& timestamp);
    // While set, a change follows this item rather than the conversation's
    // latest: the receipt whose Undo made it. Empty clears it.
    void                     set_change_anchor(std::string item_id) { m_change_anchor = std::move(item_id); }
    std::vector<ChangeEntry> changes() const;

    // Work the application ran outside the chat. `begin_work` assigns the id,
    // the sequence number and, unless the entry names its conversation and
    // the item it follows, the place in the timeline: after the active
    // conversation's last item.
    WorkEntry              begin_work(WorkEntry entry, const std::string& timestamp);
    // Completes a running entry. Empty when no running entry has that id.
    std::optional<WorkEntry> finish_work(const std::string& id, const std::string& state, const std::string& detail,
                                         const std::string& timestamp);
    std::vector<WorkEntry> work() const;
    // The conversation's most recent message or tool activity, by seq; empty
    // when it has none.
    std::string last_item_id(const std::string& conversation_id) const;

    // Raw access for tests and serialization helpers.
    const nlohmann::json& raw() const { return m_doc; }

private:
    nlohmann::json*       conversation_json(const std::string& conversation_id);
    const nlohmann::json* conversation_json(const std::string& conversation_id) const;
    // The active chat, which every intent and plan write goes to.
    nlohmann::json&       active_chat();
    std::string           add_conversation(const std::string& title, const std::string& timestamp, nlohmann::json intent);
    std::uint64_t         next_seq();
    void                  touch();

    nlohmann::json m_doc;
    std::string    m_change_anchor;
};

} // namespace Slic3r::GUI::JusPrin::Agent
