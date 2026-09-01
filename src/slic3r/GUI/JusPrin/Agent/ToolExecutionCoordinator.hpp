#pragma once

// Native coordinator for Agent tool execution. Any Agent — the deterministic
// mock today, an MCP adapter later — proposes typed tool requests here; the
// coordinator applies the approval policy, holds the authoritative activity
// records, and executes approved actions exclusively through the IWorkspace
// contract, so Orca's own commands, history, and events stay in charge.
// GUI-free and deterministic: execution advances only when the owner calls
// pump(), so tests can drive it without timers.

#include "ToolExecution.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class ToolExecutionCoordinator
{
public:
    // Invoked after every observable activity change with the updated record.
    using ActivityCallback = std::function<void(const ToolActivity&)>;

    struct ExtensionResult
    {
        bool                     handled{false};
        std::string              result_json;
        std::optional<ToolError> error;
    };
    // Typed native extensions still execute inside this coordinator and its
    // approval/state machine. The host uses this for durable manufacturing
    // records; future MCP adapters use the same seam, never a WebView path.
    using ExtensionExecutor = std::function<ExtensionResult(const ToolActivity&)>;

    explicit ToolExecutionCoordinator(Workspace::IWorkspace& workspace);

    ToolExecutionCoordinator(const ToolExecutionCoordinator&) = delete;
    ToolExecutionCoordinator& operator=(const ToolExecutionCoordinator&) = delete;

    void set_listener(ActivityCallback listener) { m_listener = std::move(listener); }
    void set_extension_executor(ExtensionExecutor executor) { m_extension_executor = std::move(executor); }

    // Action IDs default to a process-local counter; an owner with persisted
    // state injects its own allocator so IDs stay unique across restarts.
    void set_action_id_allocator(std::function<std::string()> allocator) { m_action_id_allocator = std::move(allocator); }

    // Resolves an attachment ID to the absolute path of its stored blob. The
    // owner (which holds project storage) provides this so import arguments
    // stay opaque IDs — no machine-specific path is persisted or shown.
    void set_attachment_path_resolver(std::function<std::string(const std::string&)> resolver)
    {
        m_attachment_path_resolver = std::move(resolver);
    }

    // Drops every record. For project replacement: the records belong to the
    // previous project session and any persisted history keeps its own copy.
    void clear() { m_activities.clear(); }

    // Creates a Pending record stamped with the current workspace session and
    // revision. Read-only actions are approved immediately by policy; every
    // other class waits for a user decision. Returns the new record.
    const ToolActivity& propose(const ToolRequest& request, const std::string& correlation_id);

    // User decisions. Each returns true only when it changed the record's
    // state, so a resent decision (reconnect, reload) can never run an action
    // twice or resurrect a terminal record.
    bool approve(const std::string& action_id);
    bool reject(const std::string& action_id);
    bool cancel(const std::string& action_id);

    // Advances every Running activity by one deterministic tick: progress
    // first, then the native command on the final tick. The owner paces this
    // from a timer; tests call it directly.
    void pump();

    bool any_running() const;

    const std::vector<ToolActivity>& activities() const { return m_activities; }
    const ToolActivity*              find(const std::string& action_id) const;

private:
    ToolActivity* find_mutable(const std::string& action_id);
    void          start_running(ToolActivity& activity);
    void          execute(ToolActivity& activity);
    void          fail(ToolActivity& activity, std::string code, std::string message);
    void          notify(const ToolActivity& activity);
    void          invalidate_pending(const Workspace::WorkspaceChanged& change);

    Workspace::IWorkspace&           m_workspace;
    Workspace::WorkspaceSubscription m_workspace_subscription;
    ActivityCallback                 m_listener;
    ExtensionExecutor                m_extension_executor;
    std::function<std::string()>     m_action_id_allocator;
    std::function<std::string(const std::string&)> m_attachment_path_resolver;
    std::vector<ToolActivity>        m_activities;
    std::uint64_t                    m_next_action_id{1};
};

} // namespace Slic3r::GUI::JusPrin::Agent
