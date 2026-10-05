#pragma once

#include "ProjectVersionStore.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI {
class Plater;
}

namespace Slic3r::GUI::JusPrin::Agent {
class ProjectPersistence;
}

namespace Slic3r::GUI::JusPrin::Workspace {

// Owns the one writer and schedules snapshots of the current Orca model.
// All methods except the private publish worker run on the GUI thread.
class ProjectAutosave
{
public:
    enum class State { Saving, Saved, Failed };
    struct ManagedProject {
        std::string id;
        std::string name;
        std::string updated_at;
        std::string source_path;
        std::string store_path;
        std::string thumbnail_url;
    };

    ProjectAutosave(Plater& plater, Agent::ProjectPersistence& persistence, IWorkspace& workspace,
                    std::filesystem::path root);
    ~ProjectAutosave();
    ProjectAutosave(const ProjectAutosave&) = delete;
    ProjectAutosave& operator=(const ProjectAutosave&) = delete;

    void tick();
    bool save_now();
    void ensure_current_preview();
    State state() const { return m_state; }
    std::string project_name() const;
    const std::string& error() const { return m_error; }
    const std::string& warning() const { return m_warning; }
    std::uint64_t capture_attempts() const { return m_capture_attempts; }
    std::string current_version() const;
    std::vector<ProjectVersionStore::Version> history();
    std::string pin_current();
    void record_agent_operation(const std::string& action_id, const std::string& tool,
                                const std::string& outcome, const std::string& before);
    bool restore(const std::string& version_id);
    bool restore_chat(const std::string& version_id, const nlohmann::json& planning,
                      const std::string& conversation_id);
    bool resume_latest();
    std::vector<ManagedProject> projects();
    // Read-only lookup also works when another app instance owns the store lock.
    bool has_local_project_for_source(const std::filesystem::path& source) const;
    std::vector<std::string> projects_for_source(const std::filesystem::path& source) const;
    bool open_managed_project(const std::string& project_id);
    bool export_copy(const std::filesystem::path& destination);

private:
    bool restore_impl(const std::string& version_id, const nlohmann::json* planning,
                      const std::string& conversation_id);
    void adopt_project();
    bool capture(bool wait_for_commit);
    bool persist_document(bool wait_for_commit);
    bool settle(bool wait);
    void observe_document_change();
    void fail(const std::string& message);
    void trim_history();
    std::string observed_input() const;

    Plater& m_plater;
    Agent::ProjectPersistence& m_persistence;
    IWorkspace& m_workspace;
    std::filesystem::path m_root;
    std::unique_ptr<ProjectVersionStore> m_store;
    WorkspaceSubscription m_subscription;
    std::future<void> m_worker;
    enum class WorkerKind { None, Model, Document };
    WorkerKind m_worker_kind{WorkerKind::None};
    std::optional<ProjectVersionStore::Materialization> m_active_materialization;
    std::string m_active_materialization_project;
    bool m_resume_attempted{false};
    std::chrono::steady_clock::time_point m_next_probe;
    std::chrono::steady_clock::time_point m_last_edit;
    std::chrono::steady_clock::time_point m_last_semantic_edit;
    std::optional<std::chrono::steady_clock::time_point> m_first_semantic_edit;
    std::uint64_t m_revision{0};
    std::uint64_t m_event_sequence{0};
    std::uint64_t m_capture_attempts{0};
    std::uint64_t m_committed_semantic_revision{0};
    std::uint64_t m_seen_semantic_revision{0};
    std::uint64_t m_inflight_semantic_revision{0};
    std::string m_last_capture_input;
    std::string m_seen_model_input;
    std::string m_inflight_capture_input;
    std::string m_committed_head;
    std::string m_project_name;
    State m_state{State::Saving};
    std::string m_error;
    std::string m_warning;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
