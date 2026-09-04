#pragma once
#include "../McpDiscoveryFile.hpp"
#include "../McpProtocol.hpp"
#include <chrono>
#include <atomic>
#include <functional>
#include <memory>

namespace Slic3r::GUI::JusPrin::Mcp::Bridge {
struct Config
{
    std::filesystem::path discovery_path;
    std::string url_override;
    std::chrono::milliseconds probe_timeout{2000};
    std::chrono::milliseconds call_timeout{std::chrono::minutes(5)};
};

struct Delivery
{
    nlohmann::json message;
    // A queued progress frame is invalidated by cancellation even if stdout is
    // blocked. A frame already started must finish to preserve JSON framing.
    std::shared_ptr<std::atomic<bool>> cancelled;
};

class Server
{
public:
    using Output = std::function<void(Delivery)>;
    Server(Config config, Output output, std::function<void(std::string)> diagnostic);
    ~Server();
    // Bounded ingress applies backpressure to stdin, never to the GUI process.
    void submit(std::string line);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Slic3r::GUI::JusPrin::Mcp::Bridge
