#include <catch2/catch_all.hpp>
#include "mcp_test_directory.hpp"
#include "mcp_test_client.hpp"
#include "slic3r/GUI/JusPrin/Mcp/Bridge/Bridge.hpp"
#include "slic3r/GUI/JusPrin/Mcp/Bridge/HttpExchange.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"
#include <mutex>

using namespace Slic3r::GUI::JusPrin;
using nlohmann::json;
namespace {
Workspace::WorkspaceSnapshot fixture()
{
    Workspace::WorkspaceSnapshot snapshot;
    Workspace::WorkspacePlate plate;
    plate.id = Workspace::PlateId(Workspace::ProjectSessionId(1), 11); plate.active = true;
    Workspace::WorkspaceObject object;
    object.id = Workspace::ObjectId(Workspace::ProjectSessionId(1), 21); object.name = "Cube";
    plate.objects.push_back(object); snapshot.plates.push_back(plate); snapshot.active_plate = plate.id;
    return snapshot;
}
json rpc(int id, std::string method, json params = json::object())
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::move(method)}, {"params", std::move(params)}};
}
struct Harness
{
    JusPrinTest::McpDirectory directory;
    Workspace::FakeWorkspace workspace{fixture()};
    Agent::ToolExecutionCoordinator coordinator{workspace};
    std::unique_ptr<Mcp::McpRuntime> runtime;
    std::mutex mutex;
    std::vector<json> output;
    std::vector<std::string> diagnostics;
    Mcp::Bridge::Server bridge{{directory.path(), {}, std::chrono::milliseconds(200)},
        [this](Mcp::Bridge::Delivery value) { std::lock_guard<std::mutex> lock(mutex); output.push_back(std::move(value.message)); },
        [this](std::string value) { std::lock_guard<std::mutex> lock(mutex); diagnostics.push_back(std::move(value)); }};
    void start() { runtime = std::make_unique<Mcp::McpRuntime>(workspace, coordinator, directory.path()); }
    void pump() { if (runtime) runtime->poll(); coordinator.pump(); }
    void send(json message) { bridge.submit(message.dump()); }
    std::vector<json> messages() { std::lock_guard<std::mutex> lock(mutex); return output; }
    bool has(const json& id) {
        for (const auto& message : messages()) if (message.is_object() && message.value("id", json()) == id) return true;
        return false;
    }
    json wait(const json& id) {
        REQUIRE(JusPrinTest::wait_for([&] { return has(id); }, [&] { pump(); }));
        for (const auto& message : messages()) if (message.is_object() && message.value("id", json()) == id) return message;
        throw std::logic_error("Missing test response");
    }
    void initialize(std::string version = "2025-06-18") {
        send(rpc(0, "initialize", {{"protocolVersion", version}, {"capabilities", json::object()},
                                 {"clientInfo", {{"name", "bridge-tests"}, {"version", "1"}}}}));
        CHECK(wait(0)["result"]["protocolVersion"] == version);
        send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    }
    json settings_patch(int id, bool modern = false) {
        const auto snapshot = workspace.snapshot();
        auto call = rpc(id, "tools/call", {{"name", "settings_apply_patch"},
            {"arguments", {{"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision},
                           {"changes", {{"wall_loops", "4"}}}}},
            {"_meta", {{"progressToken", id}}}});
        if (modern) {
            call["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = Mcp::kProtocolVersion;
            call["params"]["_meta"]["io.modelcontextprotocol/clientCapabilities"] = json::object();
        }
        return call;
    }
    void pending(std::size_t count = 1) {
        REQUIRE(JusPrinTest::wait_for([&] { return coordinator.activities().size() >= count; }, [&] { pump(); }));
    }
};
}

TEST_CASE("MCP bridge offline legacy catalog is projected for each negotiated version", "[mcp][bridge]")
{
    Harness h;
    const std::string version = GENERATE("2025-03-26", "2025-06-18", "2025-11-25");
    h.initialize(version);
    h.send(rpc(1, "tools/list"));
    const auto listed = h.wait(1)["result"];
    CHECK(listed["tools"].size() == 6);
    CHECK_FALSE(listed.contains("ttlMs")); CHECK_FALSE(listed.contains("cacheScope"));
    CHECK_FALSE(listed.contains("resultType"));
    CHECK(listed["tools"][0].contains("outputSchema") == (version != "2025-03-26"));
    h.send(rpc(2, "tools/call", {{"name", "workspace_inspect"}}));
    const auto result = h.wait(2)["result"];
    CHECK(result["isError"] == true);
    CHECK(json::parse(result["content"][0]["text"].get<std::string>())["error"]["code"] == "workspace_unavailable");
    CHECK(result.contains("structuredContent") == (version != "2025-03-26"));
    h.send(rpc(3, "ping")); CHECK(h.wait(3)["result"] == json::object());
}

TEST_CASE("MCP bridge serves modern discovery without initialize and later discovers a live app", "[mcp][bridge]")
{
    Harness h;
    auto request = JusPrinTest::request("server/discover"); request["id"] = 1; h.send(request);
    CHECK(h.wait(1)["result"]["supportedVersions"] == json::array({Mcp::kProtocolVersion}));
    h.start();
    request = JusPrinTest::request("tools/call", {{"name", "workspace_inspect"}}); request["id"] = 2; h.send(request);
    const auto result = h.wait(2)["result"];
    CHECK(result["isError"] == false);
    CHECK(result["structuredContent"]["objectCount"] == 1);
    CHECK(result["resultType"] == "complete");
}

TEST_CASE("MCP bridge forwards native approval rejection and results in both eras", "[mcp][bridge]")
{
    Harness h; h.start();
    const bool modern = GENERATE(false, true);
    const bool approve = GENERATE(false, true);
    if (!modern) h.initialize();
    h.send(h.settings_patch(1, modern)); h.pending();
    CHECK_FALSE(h.has(1));
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    const auto id = h.coordinator.activities().back().action_id;
    if (approve) REQUIRE(h.coordinator.approve(id)); else REQUIRE(h.coordinator.reject(id));
    const auto result = h.wait(1)["result"];
    CHECK(result["isError"] == !approve);
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == (approve ? "4" : "2"));
    if (!approve) CHECK(result["structuredContent"]["error"]["code"] == "approval_rejected");
    else {
        const Workspace::SettingsPatch inverse{{{"wall_loops", "2"}}};
        Workspace::SettingsPreview applied;
        REQUIRE(h.workspace.apply_settings(inverse, Workspace::settings_confirmation(h.workspace.preview_settings(inverse)), applied).succeeded());
        CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    }
}

TEST_CASE("MCP bridge client cancellation closes the native call without a terminal response", "[mcp][bridge]")
{
    Harness h; h.start();
    const bool modern = GENERATE(false, true);
    if (!modern) h.initialize();
    h.send(h.settings_patch(1, modern)); h.pending();
    const auto id = h.coordinator.activities().back().action_id;
    h.send({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", {{"requestId", 1}}}});
    REQUIRE(JusPrinTest::wait_for([&] { return h.coordinator.find(id)->state == Agent::ToolState::Cancelled; }, [&] { h.pump(); }));
    h.send(rpc(2, "ping")); h.wait(2);
    CHECK_FALSE(h.has(1));
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
}

TEST_CASE("MCP bridge ignores malformed cancellation notifications", "[mcp][bridge]")
{
    Harness h; h.start(); h.initialize();
    h.send(h.settings_patch(1)); h.pending();
    const auto id = h.coordinator.activities().back().action_id;
    h.send({{"method", "notifications/cancelled"}, {"params", {{"requestId", 1}}}});
    h.send(rpc(2, "ping")); h.wait(2);
    for (int i = 0; i < 20; ++i) { h.pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    CHECK(h.coordinator.find(id)->state == Agent::ToolState::Pending);
}

TEST_CASE("MCP bridge does not retry a call whose connection is lost and reconnects after restart", "[mcp][bridge]")
{
    Harness h; h.start(); h.initialize();
    h.send(h.settings_patch(1)); h.pending();
    h.runtime.reset();
    CHECK(h.wait(1)["result"]["structuredContent"]["error"]["code"] == "connection_lost");
    h.send(rpc(2, "tools/call", {{"name", "workspace_inspect"}}));
    CHECK(h.wait(2)["result"]["structuredContent"]["error"]["code"] == "workspace_unavailable");
    h.start();
    h.send(rpc(3, "tools/call", {{"name", "workspace_inspect"}}));
    CHECK(h.wait(3)["result"]["structuredContent"]["objectCount"] == 1);
    CHECK(h.coordinator.activities().size() == 2); // cancelled duplicate and one live read
}

TEST_CASE("MCP bridge bounds concurrency and shutdown cancels all pending approvals", "[mcp][bridge]")
{
    Harness h; h.start(); h.initialize();
    for (int i = 1; i <= 16; ++i) { h.send(h.settings_patch(i)); h.pending(i); }
    h.send(h.settings_patch(17));
    CHECK(h.wait(17)["error"]["code"] == -32000);
    h.bridge.stop();
    REQUIRE(JusPrinTest::wait_for([&] {
        for (const auto& activity : h.coordinator.activities()) if (activity.state != Agent::ToolState::Cancelled) return false;
        return true;
    }, [&] { h.pump(); }));
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
}

TEST_CASE("MCP bridge March batches collect responses and do not answer notifications", "[mcp][bridge]")
{
    Harness h; h.initialize("2025-03-26");
    h.send(json::array({rpc(1, "ping"), {{"jsonrpc", "2.0"}, {"method", "notifications/ignored"}}, rpc(2, "tools/list")}));
    REQUIRE(JusPrinTest::wait_for([&] { for (const auto& message : h.messages()) if (message.is_array()) return true; return false; }, [&] { h.pump(); }));
    const auto batch = h.messages().back();
    REQUIRE(batch.is_array()); REQUIRE(batch.size() == 2);
    CHECK(batch[0]["id"] == 1); CHECK(batch[1]["id"] == 2);
    CHECK_FALSE(batch[1]["result"]["tools"][0].contains("outputSchema"));
}

TEST_CASE("MCP bridge rejects malformed input and limits before recursive parsing", "[mcp][bridge]")
{
    Harness h;
    const int scenario = GENERATE(0, 1, 2, 3);
    const std::string input = scenario == 0 ? "{" : scenario == 1 ? std::string(Mcp::kBodyLimit + 1, 'x') :
        scenario == 2 ? std::string(40, '[') + std::string(40, ']') : "[]";
    h.bridge.submit(input);
    const auto response = h.wait(nullptr);
    CHECK(response["error"]["code"] == (scenario == 0 ? -32700 : -32600));
}

TEST_CASE("MCP bridge mirrors Base64 names without changing modern request metadata", "[mcp][bridge]")
{
    auto request = JusPrinTest::request("tools/call", {{"name", "打印"}, {"arguments", json::object()}});
    const auto wire = Mcp::Bridge::wire_request(request);
    CHECK(wire.headers.at("mcp-name").find("=?base64?") == 0);
    CHECK(json::parse(wire.body) == request);
    CHECK(Mcp::parse_request(wire, "http://127.0.0.1:12345").request.has_value());
}
