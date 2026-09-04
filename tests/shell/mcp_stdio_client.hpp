#pragma once

#include "../agent/mcp_test_client.hpp"
#include <boost/process.hpp>
#include <algorithm>
#include <mutex>

namespace JusPrinTest {
// A real child process, kept alive across the complete native-app scenario.
// Only the reader thread touches stdout; all assertions stay on the GUI thread.
class StdioClient
{
public:
    StdioClient(const std::string& executable, const std::string& discovery)
        : child(executable, "--discovery", discovery, boost::process::std_in < input,
                boost::process::std_out > output)
    {
        reader = std::thread([this] {
            std::string line;
            while (std::getline(output, line)) {
                auto message = json::parse(line, nullptr, false);
                std::lock_guard<std::mutex> lock(mutex);
                if (message.is_discarded()) failure = "Bridge wrote invalid JSON to stdout";
                else received.push_back(std::move(message));
            }
            std::lock_guard<std::mutex> lock(mutex);
            ended = true;
        });
        send({{"jsonrpc", "2.0"}, {"id", "native-initialize"}, {"method", "initialize"},
              {"params", {{"protocolVersion", "2025-11-25"}, {"capabilities", json::object()},
                          {"clientInfo", {{"name", "native-shell-test"}, {"version", "1"}}}}}});
    }
    ~StdioClient() { shutdown(); }

    void request(json rpc)
    {
        poll();
        messages_.clear();
        finished = false;
        current_id = "native-" + std::to_string(++sequence);
        rpc["id"] = current_id;
        // Exercise legacy tool calls, while discovery remains a modern probe.
        if (rpc["method"] != "server/discover")
            rpc["params"]["_meta"] = {{"progressToken", current_id}};
        pending = std::move(rpc);
        if (initialized) send_pending();
    }
    void poll()
    {
        std::vector<json> arrivals;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!failure.empty()) throw std::runtime_error(failure);
            if (ended) throw std::runtime_error("Bridge exited before the native scenario completed");
            arrivals.swap(received);
        }
        for (auto& message : arrivals) {
            if (message.value("id", json()) == "native-initialize") {
                if (!message.contains("result") || message["result"].value("protocolVersion", "") != "2025-11-25")
                    throw std::runtime_error("Bridge legacy initialization failed");
                initialized = true;
                send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
                send_pending();
            } else {
                if (message.contains("id")) {
                    if (message["id"] != current_id) throw std::runtime_error("Late or unexpected bridge result");
                    finished = true;
                }
                messages_.push_back(std::move(message));
            }
        }
    }
    bool done() { poll(); return finished; }
    bool streaming() const
    {
        return std::any_of(messages_.begin(), messages_.end(), [](const json& message) {
            return message.value("method", "") == "notifications/progress";
        });
    }
    const std::vector<json>& messages() const { return messages_; }
    void close()
    {
        send({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"},
              {"params", {{"requestId", current_id}}}});
    }
    bool shutdown()
    {
        if (!reader.joinable()) return clean_exit;
        input.pipe().close();
        // Boost 1.84's timed wait forks a timer child on macOS and calls exit
        // in that fork. Orca's inherited backup-manager destructor then waits
        // for a vanished thread. Poll child status without another fork.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (child.running() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const bool exited = !child.running();
        if (!exited) child.terminate();
        child.wait();
        reader.join();
        clean_exit = exited && child.exit_code() == 0;
        return clean_exit;
    }
private:
    void send(const json& message)
    {
        input << message.dump() << '\n' << std::flush;
        if (!input) throw std::runtime_error("Cannot write to bridge stdin");
    }
    void send_pending() { if (!pending.is_null()) { send(pending); pending = nullptr; } }

    boost::process::opstream input;
    boost::process::ipstream output;
    boost::process::child child;
    std::thread reader;
    std::mutex mutex;
    std::vector<json> received, messages_;
    std::string failure, current_id;
    json pending;
    unsigned sequence{0};
    bool ended{false}, initialized{false}, finished{false}, clean_exit{false};
};

// Keep the original direct-HTTP regression mode alongside the stdio mode.
class NativeMcpClient
{
public:
    explicit NativeMcpClient(std::shared_ptr<StdioClient> bridge) : bridge(std::move(bridge)) {}
    void request(const Mcp::McpServer& server, json rpc)
    {
        if (bridge) bridge->request(std::move(rpc));
        else http = std::make_unique<Client>(server, std::move(rpc));
    }
    void poll() { if (bridge) bridge->poll(); else http->poll(); }
    bool done() { return bridge ? bridge->done() : http->done(); }
    bool streaming() const { return bridge ? bridge->streaming() : http->streaming(); }
    std::vector<json> messages() const { return bridge ? bridge->messages() : http->messages(); }
    void close() { if (bridge) bridge->close(); else http->close(); }
private:
    std::shared_ptr<StdioClient> bridge;
    std::unique_ptr<Client> http;
};
} // namespace JusPrinTest
