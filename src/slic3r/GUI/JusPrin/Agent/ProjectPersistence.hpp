#pragma once

// Binds the semantic project document to the current workspace. In managed
// mode the local store commits this document independently of Orca's model;
// attachment bytes live under the same stable project ID. External 3MF files
// are imports/exports and do not carry the managed conversation. The old
// auxiliary state and newer recovery mirror are read once during migration.
// A configuration without managed_root retains the legacy contract for older
// hosts and its focused tests.
//
// Project boundaries follow the auxiliary directory: it changes whenever the
// authoritative project is replaced by a load or a new project, and stays
// put on an in-place full reset — which per the product rules starts a new
// project identity. GUI-free; the owner drives flushing from its timer.

#include "ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

class ProjectPersistence
{
public:
    struct Config
    {
        // Managed projects keep semantic state beside their version store.
        // Empty preserves the legacy auxiliary/recovery behavior for tests
        // and older hosts that have not installed the local version store.
        std::string managed_root;
        // Root directory of the local recovery store; empty disables it.
        std::string recovery_root;
        // Injectable for deterministic tests.
        std::function<std::string()> clock; // ISO-8601 UTC timestamp
        std::function<std::string()> uuid;  // opaque unique identifier
        // A conversation about something other than the open project -- the
        // temporary task chat -- keeps everything in memory: nothing it
        // says belongs in this project's archive, and the session is gone
        // when the panel closes. Such an instance follows no project
        // boundary, writes no state.json and no recovery mirror, and holds
        // attachment bytes for its own lifetime only.
        bool in_memory{false};
    };

    ProjectPersistence(Workspace::IWorkspace& workspace, Config config);

    ProjectPersistence(const ProjectPersistence&) = delete;
    ProjectPersistence& operator=(const ProjectPersistence&) = delete;

    // Adopts the currently open project (call once after construction; later
    // project replacements are adopted automatically via workspace events).
    void attach();

    // A Project change whose auxiliary dir has not moved yet is ambiguous:
    // an in-place full reset, or a replacement whose event was published
    // before the new directory was adopted (Plater does both). The decision
    // is parked and resolved here once the directory has settled — the owner
    // calls this from its pacing timer; later workspace events and flushes
    // resolve it too.
    void resolve_pending_boundary();
    // Orca emits project-boundary events while loading a historical model.
    // The caller restores the existing managed identity when the load ends.
    void during_managed_restore(const std::function<void()>& operation);

    ProjectStateDocument&       document() { return m_document; }
    const ProjectStateDocument& document() const { return m_document; }

    // The configured clock, for consumers stamping document entries.
    std::string timestamp() const { return m_config.clock(); }

    // Fired after the document has been replaced by an adoption; the consumer
    // must rebuild everything it derived from the old document.
    void set_document_replaced_listener(std::function<void()> listener) { m_document_replaced = std::move(listener); }
    // Fired after each workspace edit has been appended to the change log.
    void set_change_listener(std::function<void(const ChangeEntry&)> listener) { m_change_added = std::move(listener); }

    // Fired when the manufacturing ledger gains an entry or the document is
    // replaced. Any number of shell surfaces may observe it -- the Project
    // pane's print history, the status row's overflow menu -- each holding its
    // own subscription; dropping one never affects another.
    Workspace::WorkspaceSubscription subscribe_ledger(std::function<void()> listener);
    void notify_ledger_changed() const;

    // Marks the document changed. In managed mode ProjectAutosave coalesces
    // document writes between model versions; legacy mode flushes auxiliary files.
    void commit() { m_dirty = true; }
    void flush();
    void flush_if_dirty();
    void record_managed_restore(const std::string& current_state, const std::string& from_version,
                                const std::string& selected_version);
    void record_chat_restore(const std::string& current_state, const std::string& from_version,
                             const std::string& selected_version, const nlohmann::json& planning,
                             const std::string& conversation_id);
    void notify_chat_restore_published() { notify_document_replaced(); }
    void adopt_managed_state(const std::string& state);

    // The composer draft is a document field in managed mode; legacy mode
    // continues to keep it in the recovery mirror.
    void               set_draft(const std::string& text);
    const std::string& draft() const { return m_draft; }

    // -- Attachment blobs ---------------------------------------------------
    // Blobs live under <JusPrin data dir>/attachments/<id>/<name>. The host
    // owns the semantic record; persistence owns only the bytes on disk.
    std::string attachments_dir() const;
    // Writes bytes to <JusPrin data dir>/relative_path (attachment paths are
    // caller-sanitized). Returns false when the write fails.
    bool write_attachment_blob(const std::string& relative_path, const std::string& bytes);
    // Reads an attachment blob back (empty when missing); used to rebuild image
    // previews after a reload without persisting them in state.json.
    std::string read_attachment_blob(const std::string& relative_path) const;
    // Removes an attachment's blob directory (relative to the JusPrin data
    // dir); used when a staged attachment is discarded.
    void remove_attachment_dir(const std::string& relative_dir);

    // A clean-sharing copy: the project without conversations or any other
    // auxiliary content.
    Workspace::CommandResult export_clean_copy(const std::string& file_path);

    // Storage locations resolved for the active managed or legacy project.
    std::string jusprin_data_dir() const;
    std::string state_file_path() const;
    std::string recovery_dir() const; // empty when disabled or no identity

private:
    void notify_document_replaced();
    void on_workspace_changed(const Workspace::WorkspaceChanged& change);
    void on_edit(const Workspace::WorkspaceEdit& edit);
    bool heal_if_directory_moved();
    void adopt_current_project(bool in_place_reset);
    void start_fresh_identity();
    void write_state_to(const std::string& directory) const;
    void write_recovery_meta() const;
    void load_recovery_meta();

    Workspace::IWorkspace&           m_workspace;
    Config                           m_config;
    Workspace::WorkspaceSubscription m_subscription;
    Workspace::WorkspaceSubscription m_edit_subscription;
    ProjectStateDocument             m_document;

    std::function<void()>                   m_document_replaced;
    std::function<void(const ChangeEntry&)> m_change_added;
    struct LedgerObservers;
    std::shared_ptr<LedgerObservers>        m_ledger_observers;

    // Attachment bytes of an in-memory session, by the same relative path a
    // project session writes on disk.
    std::map<std::string, std::string> m_blobs;

    std::string m_attached_aux_dir;
    std::string m_draft;
    bool        m_dirty{false};
    bool        m_attached{false};
    bool        m_boundary_pending{false};
    bool        m_managed_restore_active{false};
};

} // namespace Slic3r::GUI::JusPrin::Agent
