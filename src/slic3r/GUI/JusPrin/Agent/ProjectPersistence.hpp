#pragma once

// Binds the semantic project document to its storage and to the workspace:
//
//  - state.json and revision checkpoints live under <auxiliary_data_dir>/
//    JusPrin/, which Orca embeds in the project archive on save and extracts
//    on open — so an explicit save carries the conversation state saved so
//    far, exactly as of that save;
//  - a local recovery store (keyed by project identity, outside the project)
//    mirrors newer working state — the current draft and everything written
//    since the last explicit save — and wins at adoption when it is newer;
//  - manufacturing changes reported by the workspace produce revision
//    checkpoints through IWorkspace::export_project_archive;
//  - Revert here restores a checkpoint through the workspace and atomically
//    truncates every conversation's later entries, keeping no redo branch.
//
// Project boundaries follow the auxiliary directory: it changes whenever the
// authoritative project is replaced by a load or a new project, and stays
// put on an in-place full reset — which per the product rules starts a new
// project identity. GUI-free; the owner drives flushing from its timer.

#include "ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

class ProjectPersistence
{
public:
    struct Config
    {
        // Root directory of the local recovery store; empty disables it.
        std::string recovery_root;
        // Injectable for deterministic tests.
        std::function<std::string()> clock; // ISO-8601 UTC timestamp
        std::function<std::string()> uuid;  // opaque unique identifier
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

    ProjectStateDocument&       document() { return m_document; }
    const ProjectStateDocument& document() const { return m_document; }

    // The configured clock, for consumers stamping document entries.
    std::string timestamp() const { return m_config.clock(); }

    // Fired after the document has been replaced by an adoption or a revert;
    // the consumer must rebuild everything it derived from the old document.
    void set_document_replaced_listener(std::function<void()> listener) { m_document_replaced = std::move(listener); }
    void set_revision_listener(std::function<void(const RevisionInfo&)> listener) { m_revision_added = std::move(listener); }

    // Marks the document changed; flush() writes state.json to the project's
    // auxiliary dir and the recovery mirror. The owner calls flush_if_dirty()
    // from its pacing timer so streaming deltas coalesce.
    void commit() { m_dirty = true; }
    void flush();
    void flush_if_dirty();

    // The composer draft lives only in the recovery store: it is working
    // state, not saved conversation history.
    void               set_draft(const std::string& text);
    const std::string& draft() const { return m_draft; }

    struct RevertResult
    {
        bool        ok{false};
        std::string error;
    };
    RevertResult revert_to_revision(const std::string& revision_id);

    // A clean-sharing copy: the project without conversations, revision
    // history, or any other auxiliary content.
    Workspace::CommandResult export_clean_copy(const std::string& file_path);

    struct CheckpointStats
    {
        std::size_t    captures{0};
        std::size_t    capture_failures{0};
        std::uintmax_t total_snapshot_bytes{0};
        double         last_capture_ms{-1.0};
        double         last_restore_ms{-1.0};
    };
    const CheckpointStats& stats() const { return m_stats; }

    // Storage locations (resolved fresh; the auxiliary dir moves with the
    // project).
    std::string jusprin_data_dir() const;
    std::string state_file_path() const;
    std::string recovery_dir() const; // empty when disabled or no identity

private:
    void on_workspace_changed(const Workspace::WorkspaceChanged& change);
    bool heal_if_directory_moved();
    void adopt_current_project(bool in_place_reset);
    void start_fresh_identity();
    void capture_revision(const std::string& cause);
    void retry_initial_capture();
    void write_state_to(const std::string& directory) const;
    void write_recovery_meta() const;
    void load_recovery_meta();

    Workspace::IWorkspace&           m_workspace;
    Config                           m_config;
    Workspace::WorkspaceSubscription m_subscription;
    ProjectStateDocument             m_document;

    std::function<void()>                         m_document_replaced;
    std::function<void(const RevisionInfo&)>      m_revision_added;

    std::string m_attached_aux_dir;
    std::string m_draft;
    bool        m_dirty{false};
    bool        m_in_revert{false};
    bool        m_attached{false};
    bool        m_boundary_pending{false};
    // The initial checkpoint could not be captured (adoption before the
    // canvas could render); retried on later events until a manufacturing
    // change makes the initial state unrecoverable.
    bool        m_initial_capture_pending{false};

    CheckpointStats m_stats;
};

} // namespace Slic3r::GUI::JusPrin::Agent
