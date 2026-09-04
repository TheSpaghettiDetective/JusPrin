#include "McpRuntime.hpp"
#include <iostream>

namespace Slic3r::GUI::JusPrin::Mcp {
using nlohmann::json;

McpRuntime::McpRuntime(Workspace::IWorkspace& workspace, Agent::ToolExecutionCoordinator& coordinator,
                       std::filesystem::path discovery_path, ServerOptions options)
    : m_workspace(workspace), m_coordinator(coordinator), m_server(options),
      m_discovery_path(std::move(discovery_path)), m_discovery(write_discovery(m_discovery_path, m_server.url()))
{
    m_subscription = m_coordinator.subscribe([this](const Agent::ToolActivity& activity) { on_activity(activity); });
}

McpRuntime::~McpRuntime()
{
    m_server.stop(); // no worker can queue another request after this point
    detach_calls();
    try {
        remove_discovery(m_discovery_path, m_discovery.instance_id);
    } catch (const std::filesystem::filesystem_error& error) {
        // Cleanup can lose access after startup. The socket is already closed;
        // retaining a stale record is safe because readers must probe liveness.
        std::cerr << "MCP discovery cleanup left a stale file: " << error.what() << '\n';
    }
}

void McpRuntime::detach_calls()
{
    // Project replacement may clear the coordinator's records. Finish waiters
    // before that happens, so no request is orphaned until its network timeout.
    auto calls = std::move(m_calls);
    m_calls.clear();
    for (auto& entry : calls) {
        m_coordinator.cancel(entry.second.action_id);
        const auto& pending = entry.second.pending;
        m_server.send(pending->connection_id, rpc_result(pending->request.id,
                      tool_result(tool_error("cancelled", "The tool runtime or project was closed."), true)), true);
    }
}

void McpRuntime::poll()
{
    std::vector<std::string> cancelled;
    for (const auto& entry : m_calls)
        if (entry.second.pending->cancelled.load()) cancelled.push_back(entry.first);
    for (const auto& correlation : cancelled) {
        const auto found = m_calls.find(correlation);
        if (found == m_calls.end()) continue;
        const auto action_id = found->second.action_id;
        m_calls.erase(found);
        m_coordinator.cancel(action_id);
    }
    for (auto& pending : m_server.take_calls()) {
        if (pending->cancelled.load()) continue;
        if (!m_workspace.snapshot().session) {
            m_server.send(pending->connection_id, rpc_result(pending->request.id,
                tool_result(tool_error("workspace_unavailable", "No live project session is available."), true)), true);
            continue;
        }
        const auto& params = pending->request.params;
        const auto correlation = "mcp-" + std::to_string(pending->connection_id);
        // Insert before propose: validation failure and initial states notify
        // synchronously, and must not be lost before the action ID is known.
        m_calls.emplace(correlation, Call{pending, {}, -1});
        m_coordinator.propose({params["name"].get<std::string>(), params.value("arguments", json::object()).dump()},
                              correlation, {}, Agent::ToolSource::Mcp);
    }
}

void McpRuntime::on_activity(const Agent::ToolActivity& activity)
{
    const auto found = m_calls.find(activity.correlation_id);
    if (found == m_calls.end()) return;
    auto& call = found->second;
    call.action_id = activity.action_id;
    const auto pending = call.pending;
    if (Agent::tool_state_terminal(activity.state)) {
        auto result = activity_result(activity, m_workspace.snapshot());
        m_calls.erase(found);
        m_server.send(pending->connection_id, rpc_result(pending->request.id, std::move(result)), true);
        return;
    }
    if (!activity.requires_approval) return;
    const int progress = activity.state == Agent::ToolState::Pending ? 0 : activity.state == Agent::ToolState::Running ? 1 : -1;
    const auto& meta = pending->request.params["_meta"];
    if (progress <= call.last_progress || !meta.contains("progressToken")) return;
    call.last_progress = progress;
    m_server.send(pending->connection_id, {{"jsonrpc", "2.0"}, {"method", "notifications/progress"},
                  {"params", {{"progressToken", meta["progressToken"]}, {"progress", progress}, {"total", 2},
                               {"message", progress == 0 ? "Awaiting approval in JusPrin" : "Running in JusPrin"}}}}, false);
}
} // namespace Slic3r::GUI::JusPrin::Mcp
