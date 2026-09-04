#include <catch2/catch_all.hpp>
#include "mcp_test_client.hpp"
#include "mcp_test_directory.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolResults.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"

using namespace Slic3r::GUI::JusPrin;
using namespace JusPrinTest;

namespace {
Workspace::WorkspaceSnapshot fixture()
{
    Workspace::WorkspaceSnapshot snapshot;
    Workspace::WorkspacePlate plate;
    plate.id = Workspace::PlateId(Workspace::ProjectSessionId(1), 11);
    plate.name = "Plate 1"; plate.active = true;
    Workspace::WorkspaceObject object;
    object.id = Workspace::ObjectId(Workspace::ProjectSessionId(1), 21);
    object.name = "Cube";
    plate.objects.push_back(object);
    snapshot.plates.push_back(plate);
    snapshot.active_plate = plate.id;
    return snapshot;
}

struct RuntimeHarness
{
    Workspace::FakeWorkspace workspace{fixture()};
    Agent::ToolExecutionCoordinator coordinator{workspace};
    McpDirectory directory;
    Mcp::McpRuntime runtime{workspace, coordinator, directory.path()};
    json settings_patch() const {
        const auto snapshot = workspace.snapshot();
        return request("tools/call", {{"name", "settings_apply_patch"},
            {"arguments", {{"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision},
                           {"changes", {{"wall_loops", "4"}}}}}});
    }
    void pump() { runtime.poll(); coordinator.pump(); }
    bool finish(Client& client) { return wait_for([&] { return client.done(); }, [&] { pump(); }); }
    bool pending() { return wait_for([&] { return !coordinator.activities().empty(); }, [&] { pump(); }); }
};
}

TEST_CASE("MCP network discovery and quick reads do not require initialization", "[mcp][network]")
{
    RuntimeHarness h;
    CHECK(h.runtime.server().url().find("http://127.0.0.1:") == 0);
    Client discover(h.runtime.server(), request("server/discover"));
    REQUIRE(h.finish(discover));
    CHECK(discover.wire.find("HTTP/1.1 200") == 0);
    CHECK_FALSE(discover.streaming());
    CHECK(discover.messages()[0]["result"]["supportedVersions"] == json::array({Mcp::kProtocolVersion}));
    CHECK(discover.messages()[0]["result"]["ttlMs"] == 0);
    CHECK(discover.messages()[0]["result"]["cacheScope"] == "private");
    Client list(h.runtime.server(), request("tools/list"));
    REQUIRE(h.finish(list));
    auto tools = list.messages()[0]["result"]["tools"];
    CHECK(list.messages()[0]["result"]["ttlMs"] == 0);
    CHECK(list.messages()[0]["result"]["cacheScope"] == "private");
    REQUIRE(tools.size() == 5);
    CHECK(tools.back()["name"] == "workspace_inspect");
    Client inspect(h.runtime.server(), request("tools/call", {{"name", "workspace_inspect"}}));
    REQUIRE(h.finish(inspect));
    CHECK_FALSE(inspect.streaming());
    const auto result = inspect.messages()[0]["result"];
    CHECK(result["structuredContent"]["objectCount"] == 1);
    CHECK(result["structuredContent"]["plates"]["items"][0]["objects"]["items"][0]["name"] == "Cube");
    CHECK(json::parse(result["content"][0]["text"].get<std::string>()) == result["structuredContent"]);
    CHECK(h.coordinator.activities().size() == 1);
}

TEST_CASE("MCP mutations wait for the shared approval and observers", "[mcp][network][approval]")
{
    RuntimeHarness h;
    std::vector<Agent::ToolState> states;
    auto observer = h.coordinator.subscribe([&](const Agent::ToolActivity& activity) { states.push_back(activity.state); });
    Client client(h.runtime.server(), h.settings_patch());
    REQUIRE(h.pending());
    const auto id = h.coordinator.activities().back().action_id;
    CHECK(h.coordinator.find(id)->state == Agent::ToolState::Pending);
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    CHECK_FALSE(client.done());
    SECTION("approve") {
        CHECK(h.coordinator.approve(id));
        REQUIRE(h.finish(client));
        CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "4");
        CHECK(client.messages().back()["result"]["isError"] == false);
        CHECK(states.back() == Agent::ToolState::Succeeded);
        const Workspace::SettingsPatch inverse{{{"wall_loops", "2"}}};
        Workspace::SettingsPreview applied;
        REQUIRE(h.workspace.apply_settings(inverse, Workspace::settings_confirmation(h.workspace.preview_settings(inverse)), applied).succeeded());
        CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    }
    SECTION("reject") {
        CHECK(h.coordinator.reject(id));
        REQUIRE(h.finish(client));
        CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
        CHECK(client.messages().back()["result"]["structuredContent"]["error"]["code"] == "approval_rejected");
        CHECK(states.back() == Agent::ToolState::Rejected);
    }
    SECTION("cancel") {
        CHECK(h.coordinator.cancel(id));
        REQUIRE(h.finish(client));
        CHECK(client.messages().back()["result"]["structuredContent"]["error"]["code"] == "cancelled");
        CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    }
    SECTION("stale") {
        REQUIRE(h.workspace.rename_object(h.workspace.snapshot().plates[0].objects[0].id, "Changed").succeeded());
        REQUIRE(h.finish(client));
        CHECK(client.messages().back()["result"]["structuredContent"]["error"]["code"] == "stale_workspace");
        CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    }
    CHECK(client.streaming());
    const auto messages = client.messages();
    REQUIRE(messages.size() >= 2);
    CHECK(messages[0]["method"] == "notifications/progress");
    CHECK(messages[0]["params"]["progressToken"] == "test-progress");
}

TEST_CASE("MCP disconnect cancels pending native proposals", "[mcp][network][cancellation]")
{
    RuntimeHarness h;
    Client client(h.runtime.server(), h.settings_patch());
    REQUIRE(h.pending());
    const auto id = h.coordinator.activities().back().action_id;
    client.close();
    REQUIRE(wait_for([&] { return h.coordinator.find(id)->state == Agent::ToolState::Cancelled; }, [&] { h.pump(); }));
    CHECK_FALSE(h.coordinator.approve(id));
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
}

TEST_CASE("MCP exposure and schema failures cannot reach a native mutation", "[mcp][network]")
{
    RuntimeHarness h;
    const std::string name = GENERATE("duplicate_object", "inspect_selection", "import_model", "record_build", "missing", "settings_apply_patch");
    Client client(h.runtime.server(), request("tools/call", {{"name", name}, {"arguments", {{"actionClass", "read_only"}}}}));
    REQUIRE(h.finish(client));
    auto response = client.messages().back();
    if (name == "settings_apply_patch") {
        CHECK(response["result"]["isError"] == true);
        CHECK(response["result"]["structuredContent"]["error"]["code"] == "invalid_arguments");
    } else {
        CHECK(response["error"]["code"] == -32602);
        CHECK(response["error"]["data"]["code"] == "unknown_tool");
    }
    CHECK(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
}

TEST_CASE("MCP uses the configured port when it is free", "[mcp][network]")
{
    unsigned short free_port;
    { Mcp::McpServer temporary; free_port = temporary.port(); }
    Mcp::ServerOptions options;
    options.port = free_port;
    options.fallback_to_ephemeral = true;
    Mcp::McpServer server(options);
    CHECK(server.port() == free_port);
}

TEST_CASE("MCP startup collisions and shutdown have bounded lifetimes", "[mcp][network][lifetime]")
{
    Mcp::McpServer server;
    Mcp::ServerOptions collision;
    collision.port = server.port();
    REQUIRE_THROWS_AS(Mcp::McpServer{collision}, boost::system::system_error);
    collision.fallback_to_ephemeral = true;
    Mcp::McpServer second(collision);
    CHECK(second.port() != server.port());
    CHECK(second.url() == "http://127.0.0.1:" + std::to_string(second.port()) + "/mcp");
    Client fallback(second, request("server/discover"));
    REQUIRE(wait_for([&] { return fallback.done(); }, [] {}));
    CHECK(fallback.wire.find("HTTP/1.1 200") == 0);
    Client pending(server, request("tools/call", {{"name", "settings_apply_patch"}, {"arguments", json::object()}}));
    REQUIRE(wait_for([&] { pending.poll(); return pending.streaming(); }, [] {}));
    const auto start = std::chrono::steady_clock::now();
    server.stop();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    REQUIRE(wait_for([&] { return pending.done(); }, [] {}));
    auto calls = server.take_calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]->cancelled.load());
    server.stop();
    CHECK(pending.messages().empty());
}

TEST_CASE("MCP bounded concurrency and timeouts leave the GUI unblocked", "[mcp][network][limits]")
{
    Mcp::ServerOptions options;
    options.max_connections = 2;
    options.request_timeout = std::chrono::milliseconds(100);
    Mcp::McpServer server(options);
    Client first(server, request("tools/call", {{"name", "settings_apply_patch"}}));
    Client second(server, request("tools/call", {{"name", "settings_apply_patch"}}));
    REQUIRE(wait_for([&] { first.poll(); second.poll(); return first.streaming() && second.streaming(); }, [] {}));
    Client excess(server, request("tools/list"));
    REQUIRE(wait_for([&] { return excess.done(); }, [] {}));
    CHECK(excess.wire.empty());
    REQUIRE(wait_for([&] { return first.done() && second.done(); }, [] {}));
    auto calls = server.take_calls();
    REQUIRE(calls.size() == 2);
    CHECK(calls[0]->cancelled.load());
    CHECK(calls[1]->cancelled.load());
}

TEST_CASE("MCP runtime shutdown cancels pending and approved work before destruction", "[mcp][network][lifetime]")
{
    Workspace::FakeWorkspace workspace(fixture());
    Agent::ToolExecutionCoordinator coordinator(workspace);
    McpDirectory directory;
    auto runtime = std::make_unique<Mcp::McpRuntime>(workspace, coordinator, directory.path());
    const auto snapshot = workspace.snapshot();
    Client client(runtime->server(), request("tools/call", {{"name", "settings_apply_patch"},
        {"arguments", {{"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision},
                       {"changes", {{"wall_loops", "4"}}}}}}));
    REQUIRE(wait_for([&] { return !coordinator.activities().empty(); }, [&] { runtime->poll(); }));
    const auto id = coordinator.activities().back().action_id;
    const bool approved = GENERATE(false, true);
    if (approved) REQUIRE(coordinator.approve(id));
    runtime.reset();
    CHECK(coordinator.find(id)->state == Agent::ToolState::Cancelled);
    coordinator.pump();
    CHECK(workspace.read_settings({"wall_loops"}).items[0].value == "2");
    REQUIRE(wait_for([&] { return client.done(); }, [] {}));
}

TEST_CASE("MCP rejects HTTP abuse without creating coordinator activity", "[mcp][network][security]")
{
    RuntimeHarness h;
    const int scenario = GENERATE(0, 1, 2, 3);
    std::string headers = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    unsigned expected;
    if (scenario == 0) { headers += "Origin: null\r\n"; expected = 403; }
    else if (scenario == 1) { headers += "Origin: https://foreign.example\r\n"; expected = 403; }
    else if (scenario == 2) { headers += "X-Oversized: " + std::string(9000, 'x') + "\r\n"; expected = 431; }
    else { headers += "Mcp-Method: tools/list\r\nMcp-Method: tools/call\r\n"; expected = 400; }
    Client client(h.runtime.server(), request("tools/list"), headers);
    REQUIRE(h.finish(client));
    CHECK(client.wire.find("HTTP/1.1 " + std::to_string(expected)) == 0);
    CHECK(h.coordinator.activities().empty());
}

TEST_CASE("MCP host survives page reset and persists the same activity", "[mcp][host][reload]")
{
    Workspace::FakeWorkspace workspace(fixture());
    Agent::ProjectPersistence persistence(workspace, {});
    Agent::AgentHost host(workspace, persistence, Agent::AgentAvailability::Unavailable, false);
    persistence.attach();
    CHECK(host.mcp() == nullptr);
    McpDirectory directory;
    host.start_mcp(directory.path().u8string());
    host.reset_page();
    Client client(host.mcp()->server(), request("tools/call", {{"name", "settings_get"}, {"arguments", {{"keys", {"wall_loops"}}}}}));
    REQUIRE(wait_for([&] { return client.done(); }, [&] { host.pump_tools(); }));
    CHECK_FALSE(host.handshake_complete());
    CHECK(client.messages()[0]["result"]["isError"] == false);
    REQUIRE(host.tools().activities().size() == 1);
    CHECK(host.tools().activities()[0].state == Agent::ToolState::Succeeded);
    CHECK(host.tools().activities()[0].source == Agent::ToolSource::Mcp);
    CHECK(persistence.document().activities().size() == 1);
    CHECK(persistence.document().activities()[0].source == Agent::ToolSource::Mcp);
}

TEST_CASE("MCP workspace summaries are bounded schema-validated and explicit", "[mcp][registry]")
{
    auto snapshot = fixture();
    for (unsigned i = 0; i < 1000; ++i) {
        auto object = snapshot.plates[0].objects[0];
        object.id = Workspace::ObjectId(Workspace::ProjectSessionId(1), i + 100);
        object.name.assign(1024, 'x');
        snapshot.plates[0].objects.push_back(object);
        snapshot.selected_objects.push_back(object.id);
    }
    auto result = Agent::workspace_inspection(snapshot);
    const auto& registry = Agent::ToolRegistry::instance();
    CHECK(registry.validate_output(*registry.find("workspace_inspect"), result));
    CHECK(result["truncated"] == true);
    CHECK(result["objectCount"] == 1001);
    CHECK(result["plates"]["items"][0]["objects"]["items"].size() == Agent::kToolListLimit);
    CHECK(result.dump().size() < Mcp::kBodyLimit);
    auto selection = Agent::selection_inspection(snapshot);
    CHECK(selection["truncated"] == true);
    CHECK(selection["selection"].size() == Agent::kToolListLimit);
    CHECK(registry.validate_output(*registry.find("inspect_selection"), selection));
    result["revision"] = "not a number";
    CHECK_FALSE(registry.validate_output(*registry.find("workspace_inspect"), result));
}

TEST_CASE("MCP project replacement completes the request before records are cleared", "[mcp][host][lifetime]")
{
    Workspace::FakeWorkspace workspace(fixture());
    Agent::ProjectPersistence persistence(workspace, {});
    Agent::AgentHost host(workspace, persistence, Agent::AgentAvailability::Unavailable, false);
    persistence.attach();
    McpDirectory directory;
    host.start_mcp(directory.path().u8string());
    const auto snapshot = workspace.snapshot();
    Client client(host.mcp()->server(), request("tools/call", {{"name", "settings_apply_patch"},
        {"arguments", {{"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision},
                       {"changes", {{"wall_loops", "4"}}}}}}));
    REQUIRE(wait_for([&] { return !host.tools().activities().empty(); }, [&] { host.pump_tools(); }));
    workspace.replace_project(fixture());
    REQUIRE(wait_for([&] { return client.done(); }, [&] { host.pump_tools(); }));
    CHECK(client.messages().back()["result"]["isError"] == true);
    CHECK(workspace.read_settings({"wall_loops"}).items[0].value == "2");
    CHECK(host.tools().activities().empty());
}
