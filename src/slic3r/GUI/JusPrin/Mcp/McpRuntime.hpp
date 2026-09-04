#pragma once

#include "McpServer.hpp"
#include "McpDiscoveryFile.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.hpp"

namespace Slic3r::GUI::JusPrin::Mcp {

// GUI-thread adapter. The host owns this after the coordinator and destroys
// it first. Network traffic is never allowed to approve a proposal.
class McpRuntime
{
public:
    McpRuntime(Workspace::IWorkspace& workspace, Agent::ToolExecutionCoordinator& coordinator,
               std::filesystem::path discovery_path, ServerOptions options = {});
    ~McpRuntime();
    void poll();
    void detach_calls();
    const McpServer& server() const { return m_server; }
    const std::filesystem::path& discovery_path() const { return m_discovery_path; }

private:
    void on_activity(const Agent::ToolActivity& activity);
    Workspace::IWorkspace& m_workspace;
    Agent::ToolExecutionCoordinator& m_coordinator;
    McpServer m_server;
    std::filesystem::path m_discovery_path;
    DiscoveryRecord m_discovery;
    Agent::ToolActivitySubscription m_subscription;
    struct Call
    {
        std::shared_ptr<PendingCall> pending;
        std::string action_id;
        int last_progress{-1};
    };
    std::map<std::string, Call> m_calls;
};
} // namespace Slic3r::GUI::JusPrin::Mcp
