#include "Bridge.hpp"
#include "HttpExchange.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Slic3r::GUI::JusPrin::Mcp::Bridge {
using nlohmann::json;
namespace net = boost::asio;
namespace {
bool valid_id(const json& id) { return id.is_string() || id.is_number_integer(); }
bool notification(const json& message)
{
    return message.is_object() && !message.contains("id") && message.value("method", json()).is_string();
}
bool depth_valid(const std::string& bytes)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char c : bytes) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || c == '{') { if (++depth > kDepthLimit) return false; }
        else if ((c == ']' || c == '}') && depth) --depth;
    }
    return true;
}
json error(const json& id, int code, const std::string& text)
{
    auto result = rpc_error(id, code, text);
    result["id"] = id; // JSON-RPC parse errors use null when no ID can be recovered.
    return result;
}
json modern_request(std::string method, json params, json id)
{
    params["_meta"]["io.modelcontextprotocol/protocolVersion"] = kProtocolVersion;
    params["_meta"]["io.modelcontextprotocol/clientCapabilities"] = json::object();
    params["_meta"]["io.modelcontextprotocol/clientInfo"] = {{"name", "jusprin-mcp"}, {"version", mcp_build_version()}};
    return {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"method", std::move(method)}, {"params", std::move(params)}};
}
}

struct Server::Impl
{
    struct Batch { std::size_t remaining{0}; json results = json::array(); };
    struct Call {
        json rpc;
        bool modern{false};
        std::string legacy_version;
        std::shared_ptr<Batch> batch;
        std::shared_ptr<HttpExchange> exchange;
        std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
        bool finished{false};
    };
    Config config;
    Output output;
    std::function<void(std::string)> diagnostic;
    net::io_context io;
    net::executor_work_guard<net::io_context::executor_type> guard{io.get_executor()};
    std::thread worker;
    std::mutex ingress_mutex;
    std::condition_variable ingress_ready;
    std::size_t queued{0};
    bool stopped{false}; // guarded by ingress_mutex
    std::string legacy_version;
    bool initialized{false};
    std::map<std::string, std::shared_ptr<Call>> calls; // io thread only

    Impl(Config config, Output output, std::function<void(std::string)> diagnostic)
        : config(std::move(config)), output(std::move(output)), diagnostic(std::move(diagnostic))
    {
        if (!this->config.url_override.empty() && !loopback_port(this->config.url_override))
            throw std::invalid_argument("JUSPRIN_MCP_URL must be an exact http://127.0.0.1:PORT/mcp URL");
        worker = std::thread([this] { io.run(); });
    }

    void emit(json response, const std::shared_ptr<Batch>& batch = {})
    {
        if (!batch) { if (!response.is_null()) output({std::move(response), {}}); return; }
        if (!response.is_null()) batch->results.push_back(std::move(response));
        if (--batch->remaining == 0 && !batch->results.empty()) output({std::move(batch->results), {}});
    }

    void finish(const std::shared_ptr<Call>& call, json response = nullptr)
    {
        if (call->finished) return;
        call->finished = true;
        if (response.is_null()) call->cancelled->store(true);
        if (call->exchange) call->exchange->cancel();
        call->exchange.reset();
        calls.erase(call->rpc["id"].dump());
        if (!response.is_null() && !call->modern && response.contains("result")) {
            auto& result = response["result"];
            result.erase("resultType"); result.erase("ttlMs"); result.erase("cacheScope");
            if (call->legacy_version == "2025-03-26") {
                result.erase("structuredContent");
                if (result.contains("tools")) for (auto& tool : result["tools"]) {
                    tool.erase("outputSchema"); tool.erase("title");
                }
            }
        }
        emit(std::move(response), call->batch);
    }

    void offline(const std::shared_ptr<Call>& call)
    {
        const auto parsed = parse_request(wire_request(call->rpc), "http://127.0.0.1:1");
        if (!parsed.request) { finish(call, parsed.error.body); return; }
        const auto& request = *parsed.request;
        if (request.method == "server/discover") finish(call, discovery(request).body);
        else if (request.method == "tools/list") finish(call, list_tools(request).body);
        else finish(call, rpc_result(request.id, tool_result(tool_error("workspace_unavailable",
            "Open JusPrin and a project, then try again. The bridge does not launch JusPrin."), true)));
    }

    void forward(const std::shared_ptr<Call>& call, unsigned short port)
    {
        call->exchange = std::make_shared<HttpExchange>(io, port, wire_request(call->rpc), config.call_timeout,
            [this, call](json message, bool terminal) {
                if (call->finished) return;
                if (terminal) { finish(call, std::move(message)); return; }
                if (message.value("method", json()) == "notifications/progress") {
                    const auto meta = call->rpc["params"].value("_meta", json::object());
                    if (!meta.contains("progressToken")) return;
                    message["params"]["progressToken"] = meta["progressToken"];
                    output({std::move(message), call->cancelled});
                }
            }, [this, call](const std::string& reason) {
                finish(call, rpc_result(call->rpc["id"], tool_result(tool_error("connection_lost",
                    "Connection to JusPrin was lost before its result arrived. The operation's outcome is unknown; inspect the workspace before retrying.",
                    {{"reason", reason}}), true)));
            });
        call->exchange->start();
    }

    void route(const std::shared_ptr<Call>& call)
    {
        std::string url = config.url_override;
        if (url.empty()) {
            const auto record = read_discovery(config.discovery_path);
            if (!record) { offline(call); return; }
            url = record->url;
            if (record->app_version != mcp_build_version())
                diagnostic("JusPrin and jusprin-mcp versions differ; forwarding the live catalog and calls.");
        }
        const auto port = *loopback_port(url);
        const auto probe = modern_request("server/discover", json::object(), "bridge-liveness");
        call->exchange = std::make_shared<HttpExchange>(io, port, wire_request(probe), config.probe_timeout,
            [this, call, port](json message, bool terminal) {
                if (!terminal || call->finished) return;
                const auto result = message.value("result", json::object());
                const auto failure = message.value("error", json::object());
                const bool discovered = result.is_object() && result.value("supportedVersions", json()).is_array();
                const bool unsupported = failure.is_object() && failure.value("code", json()) == -32022;
                if (discovered || unsupported) forward(call, port);
                else offline(call);
            }, [this, call](const std::string&) { if (!call->finished) offline(call); });
        call->exchange->start();
    }

    void dispatch(json message, std::shared_ptr<Batch> batch = {})
    {
        if (notification(message)) {
            if (message.value("jsonrpc", json()) != "2.0" || !message.value("params", json::object()).is_object()) return;
            if (message["method"] == "notifications/initialized" && !legacy_version.empty()) initialized = true;
            if (message["method"] == "notifications/cancelled") {
                const auto params = message.value("params", json::object());
                if (params.is_object() && valid_id(params.value("requestId", json()))) {
                    const auto found = calls.find(params["requestId"].dump());
                    if (found != calls.end()) { auto call = found->second; finish(call); }
                }
            }
            return;
        }
        const json id = message.is_object() && valid_id(message.value("id", json())) ? message["id"] : json();
        if (!message.is_object() || message.value("jsonrpc", json()) != "2.0" || id.is_null() ||
            !message.value("method", json()).is_string() || message.contains("result") || message.contains("error")) {
            emit(error(id, -32600, "Expected a JSON-RPC request with a string or integer ID."), batch); return;
        }
        auto params = message.value("params", json::object());
        if (!params.is_object() || !params.value("_meta", json::object()).is_object()) {
            emit(error(id, -32602, "Request params and metadata must be objects."), batch); return;
        }
        const std::string method = message["method"];
        const bool modern = params.value("_meta", json::object()).contains("io.modelcontextprotocol/protocolVersion");
        if (!modern && method == "initialize") {
            const auto info = params.value("clientInfo", json::object());
            if (!params.value("protocolVersion", json()).is_string() || !params.value("capabilities", json()).is_object() ||
                !info.is_object() || !info.value("name", json()).is_string() || !info.value("version", json()).is_string()) {
                emit(error(id, -32602, "initialize requires protocolVersion, capabilities, and clientInfo."), batch); return;
            }
            if (!legacy_version.empty()) { emit(error(id, -32600, "Already initialized."), batch); return; }
            legacy_version = params["protocolVersion"].get<std::string>();
            if (legacy_version != "2025-03-26" && legacy_version != "2025-06-18" && legacy_version != "2025-11-25")
                legacy_version = "2025-06-18";
            json result{{"protocolVersion", legacy_version},
                {"capabilities", {{"tools", {{"listChanged", false}}}}}, {"serverInfo", {{"name", "JusPrin"}, {"version", mcp_build_version()}}},
                {"instructions", discovery({id, "server/discover", json::object()}).body["result"]["instructions"]}};
            emit({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}, batch);
            return;
        }
        if (!modern && method == "ping") { emit({{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}}, batch); return; }
        if (!modern && method != "tools/list" && method != "tools/call") {
            emit(error(id, -32601, "Method not found."), batch); return;
        }
        if (!modern && !initialized) { emit(error(id, -32000, "Complete initialize first."), batch); return; }
        if (calls.count(id.dump())) { emit(error(id, -32600, "Request ID is already in flight."), batch); return; }
        if (calls.size() >= 16) { emit(error(id, -32000, "At most 16 requests may be in flight."), batch); return; }
        auto call = std::make_shared<Call>();
        call->modern = modern; call->legacy_version = legacy_version; call->batch = std::move(batch);
        call->rpc = modern ? std::move(message) : modern_request(method, std::move(params), id);
        calls.emplace(id.dump(), call);
        route(call);
    }

    void accept(const std::string& line)
    {
        if (line.size() > kBodyLimit || !depth_valid(line)) {
            emit(error(nullptr, -32600, "Message exceeds size or depth limits.")); return;
        }
        auto message = json::parse(line, nullptr, false);
        if (message.is_discarded()) { emit(error(nullptr, -32700, "Malformed JSON.")); return; }
        if (!message.is_array()) { dispatch(std::move(message)); return; }
        if (legacy_version != "2025-03-26" || !initialized || message.empty() || message.size() > 16) {
            emit(error(nullptr, -32600, "Batches require initialized 2025-03-26 and 1 to 16 entries.")); return;
        }
        auto batch = std::make_shared<Batch>();
        for (const auto& item : message) if (!notification(item)) ++batch->remaining;
        for (auto& item : message) dispatch(std::move(item), batch);
    }
};

Server::Server(Config config, Output output, std::function<void(std::string)> diagnostic)
    : m_impl(std::make_unique<Impl>(std::move(config), std::move(output), std::move(diagnostic))) {}
Server::~Server() { stop(); }

void Server::submit(std::string line)
{
    auto& impl = *m_impl;
    std::unique_lock<std::mutex> lock(impl.ingress_mutex);
    impl.ingress_ready.wait(lock, [&] { return impl.stopped || impl.queued < 16; });
    if (impl.stopped) return;
    ++impl.queued;
    net::post(impl.io, [&impl, line = std::move(line)] {
        { std::lock_guard<std::mutex> lock(impl.ingress_mutex); --impl.queued; }
        impl.ingress_ready.notify_one();
        impl.accept(line);
    });
}

void Server::stop()
{
    auto& impl = *m_impl;
    {
        std::lock_guard<std::mutex> lock(impl.ingress_mutex);
        if (impl.stopped) return;
        impl.stopped = true;
    }
    impl.ingress_ready.notify_all();
    net::post(impl.io, [&impl] {
        while (!impl.calls.empty()) { auto call = impl.calls.begin()->second; impl.finish(call); }
        impl.guard.reset();
    });
    impl.worker.join();
}
} // namespace Slic3r::GUI::JusPrin::Mcp::Bridge
