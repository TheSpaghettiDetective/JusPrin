#pragma once

#include "McpProtocol.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace Slic3r::GUI::JusPrin::Mcp {

struct ServerOptions
{
    // Tests default to an ephemeral port; AgentHost supplies production policy.
    unsigned short port{0};
    bool fallback_to_ephemeral{false};
    std::size_t max_connections{16};
    std::chrono::milliseconds request_timeout{std::chrono::minutes(5)};
    std::chrono::milliseconds header_timeout{std::chrono::seconds(10)};
};

struct PendingCall
{
    std::uint64_t connection_id;
    Request request;
    std::atomic<bool> cancelled{false};
};

class McpServer
{
public:
    explicit McpServer(ServerOptions options = {});
    ~McpServer();
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    // Only a deliberate, native diagnostics surface may display these values.
    const std::string& url() const;
    unsigned short port() const;

    // GUI-thread mailbox. No callback into a workspace or GUI is ever run by
    // the worker. Cancelled calls remain marked even if not yet dequeued.
    std::vector<std::shared_ptr<PendingCall>> take_calls();
    void send(std::uint64_t connection_id, nlohmann::json message, bool terminal);
    void stop(); // closes sockets, marks calls cancelled, then joins the worker

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Slic3r::GUI::JusPrin::Mcp
