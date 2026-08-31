#pragma once

// The portable, versioned semantic state of a JusPrin project: conversations,
// their messages and tool activity records, and the linear manufacturing
// revision timeline. Serialized as Auxiliaries/JusPrin/state.json inside the
// project archive and mirrored to the local recovery store.
//
// The document is backed by one JSON tree that is edited in place, so
// optional fields written by other (newer) builds survive a load-edit-save
// cycle untouched. Every entry carries a monotonically increasing global
// sequence number `seq`; "later than revision R" is defined as seq > R.seq
// across every conversation, which is what Revert here truncates. GUI-free.

#include "AgentProtocol.hpp"
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

class ProjectStateDocument
{
public:
    static constexpr int kSchemaVersion = 1;

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
    };
    // Removes every entry (messages, activities, revisions) with seq greater
    // than the revision's across all conversations, and makes it current.
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
