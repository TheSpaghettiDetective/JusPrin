#pragma once

#include "slic3r/GUI/JusPrin/Mcp/McpServer.hpp"
#include <boost/asio.hpp>
#include <thread>

namespace JusPrinTest {
using nlohmann::json;
namespace Mcp = Slic3r::GUI::JusPrin::Mcp;

inline json request(std::string method, json params = json::object())
{
    params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", Mcp::kProtocolVersion},
                       {"io.modelcontextprotocol/clientCapabilities", json::object()}, {"progressToken", "test-progress"}};
    return {{"jsonrpc", "2.0"}, {"id", "network-test"}, {"method", method}, {"params", std::move(params)}};
}

// Nonblocking reads let both Catch tests and the native shell harness advance
// the actual owner/event loop without a client thread touching GUI state.
class Client
{
public:
    explicit Client(const Mcp::McpServer& server, json rpc, std::string override_headers = {}) : socket(io)
    {
        socket.connect({boost::asio::ip::make_address_v4("127.0.0.1"), server.port()});
        const std::string body = rpc.dump();
        std::string headers = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
            "Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n"
            "MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: " + rpc["method"].get<std::string>() + "\r\n";
        if (rpc["params"].contains("name")) headers += "Mcp-Name: " + rpc["params"]["name"].get<std::string>() + "\r\n";
        if (!override_headers.empty()) headers = std::move(override_headers);
        boost::asio::write(socket, boost::asio::buffer(headers + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body));
        socket.non_blocking(true);
    }
    void poll()
    {
        if (finished) return;
        std::array<char, 8192> buffer{};
        for (;;) {
            boost::system::error_code error;
            auto count = socket.read_some(boost::asio::buffer(buffer), error);
            if (count) wire.append(buffer.data(), count);
            if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) break;
            if (error) { finished = true; break; }
        }
    }
    void close() { boost::system::error_code ignored; socket.close(ignored); finished = true; }
    bool done() { poll(); return finished; }
    std::string body() const { auto split = wire.find("\r\n\r\n"); return split == std::string::npos ? "" : wire.substr(split + 4); }
    bool streaming() const { return wire.find("Content-Type: text/event-stream") != std::string::npos; }
    std::vector<json> messages() const
    {
        const auto content = body();
        if (!streaming()) return content.empty() ? std::vector<json>{} : std::vector<json>{json::parse(content)};
        std::vector<json> result;
        std::size_t position = 0;
        while ((position = content.find("data: ", position)) != std::string::npos) {
            const auto end = content.find("\n\n", position);
            if (end == std::string::npos) break;
            result.push_back(json::parse(content.substr(position + 6, end - position - 6)));
            position = end + 2;
        }
        return result;
    }
    std::string wire;
private:
    boost::asio::io_context io;
    boost::asio::ip::tcp::socket socket;
    bool finished{false};
};

template<class Predicate, class Pump> bool wait_for(Predicate predicate, Pump pump)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
} // namespace JusPrinTest
