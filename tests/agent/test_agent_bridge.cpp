// Contract tests for the Agent bridge host against the fake workspace: the
// versioned handshake, capability report, envelope identity, duplicate and
// stale handling, deterministic conversation streaming, reload
// reconstruction, and workspace context propagation including project-session
// invalidation. GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

Workspace::WorkspaceSnapshot two_object_snapshot()
{
    Workspace::WorkspaceSnapshot snapshot;
    snapshot.setup.project_name   = "Fixture Project";
    snapshot.setup.printer_preset = "Test Printer 0.4 nozzle";
    snapshot.setup.filament_preset = "Generic PLA";

    Workspace::WorkspacePlate plate;
    plate.id     = Workspace::PlateId(Workspace::ProjectSessionId(1), 11);
    plate.name   = "Plate 1";
    plate.active = true;

    Workspace::WorkspaceObject cube_a;
    cube_a.id   = Workspace::ObjectId(Workspace::ProjectSessionId(1), 21);
    cube_a.name = "cube-a";
    cube_a.instances.push_back({});
    Workspace::WorkspaceObject cube_b;
    cube_b.id   = Workspace::ObjectId(Workspace::ProjectSessionId(1), 22);
    cube_b.name = "cube-b";
    cube_b.instances.push_back({});
    plate.objects = {cube_a, cube_b};

    snapshot.plates       = {plate};
    snapshot.active_plate = plate.id;
    return snapshot;
}

struct Harness
{
    Workspace::FakeWorkspace workspace;
    AgentHost                host;
    std::vector<json>        sent;

    explicit Harness(AgentAvailability availability = AgentAvailability::Ready)
        : workspace(two_object_snapshot()), host(workspace, availability, false)
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
    }

    json page_envelope(const std::string& type, json payload, int version = Protocol::kVersion)
    {
        static int next_id = 1;
        return json{{"protocol", Protocol::kName},
                    {"version", version},
                    {"id", "w-" + std::to_string(next_id++)},
                    {"type", type},
                    {"payload", std::move(payload)}};
    }

    void deliver(const std::string& type, json payload = json::object(), int version = Protocol::kVersion)
    {
        host.on_page_message(page_envelope(type, std::move(payload), version).dump());
    }

    void handshake()
    {
        deliver("hello", json{{"protocolVersions", json::array({Protocol::kVersion})},
                              {"capabilities", json::array({"streaming"})}});
    }

    std::vector<json> of_type(const std::string& type) const
    {
        std::vector<json> result;
        for (const json& envelope : sent)
            if (envelope["type"] == type)
                result.push_back(envelope);
        return result;
    }

    const json* last_of_type(const std::string& type) const
    {
        const json* found = nullptr;
        for (const json& envelope : sent)
            if (envelope["type"] == type)
                found = &envelope;
        return found;
    }

    void pump_all(int limit = 1000)
    {
        while (host.stream_active() && limit-- > 0)
            host.pump_stream();
    }

    std::string send_user_message(const std::string& text, const std::string& client_id)
    {
        deliver("user_message", json{{"clientMessageId", client_id}, {"text", text}});
        const json* added = last_of_type("message_added");
        REQUIRE(added != nullptr);
        return (*added)["payload"]["message"]["id"].get<std::string>();
    }
};

} // namespace

TEST_CASE("protocol constants agree with the shared protocol.json", "[agent][protocol]")
{
    std::ifstream file(std::string(JUSPRIN_SOURCE_DIR) + "/resources/jusprin/agent/protocol.json");
    REQUIRE(file.is_open());
    const json shared = json::parse(file);

    CHECK(shared["name"] == Protocol::kName);
    CHECK(shared["version"].get<int>() == Protocol::kVersion);

    std::vector<std::string> shared_capabilities = shared["capabilities"].get<std::vector<std::string>>();
    CHECK(shared_capabilities == Protocol::capabilities());

    const std::set<std::string> page_types(shared["pageMessageTypes"].begin(), shared["pageMessageTypes"].end());
    CHECK(page_types == std::set<std::string>{Protocol::kHello, Protocol::kStateRequest, Protocol::kUserMessage,
                                              Protocol::kStopGeneration, Protocol::kRetryMessage});

    const std::set<std::string> host_types(shared["hostMessageTypes"].begin(), shared["hostMessageTypes"].end());
    CHECK(host_types == std::set<std::string>{Protocol::kHelloAck, Protocol::kHelloReject, Protocol::kState,
                                              Protocol::kContext, Protocol::kAppearance, Protocol::kAgentStatus,
                                              Protocol::kMessageAdded, Protocol::kAssistantStarted, Protocol::kAssistantDelta,
                                              Protocol::kAssistantCompleted, Protocol::kAssistantFailed,
                                              Protocol::kAssistantStopped, Protocol::kBridgeError});
}

TEST_CASE("handshake negotiates version and reports capabilities, agent status, and state", "[agent][bridge]")
{
    Harness harness;
    REQUIRE_FALSE(harness.host.handshake_complete());

    harness.handshake();
    REQUIRE(harness.host.handshake_complete());

    const json* ack = harness.last_of_type("hello_ack");
    REQUIRE(ack != nullptr);
    CHECK((*ack)["payload"]["version"].get<int>() == Protocol::kVersion);
    CHECK((*ack)["payload"]["capabilities"].get<std::vector<std::string>>() == Protocol::capabilities());
    CHECK((*ack)["payload"]["agent"]["status"] == "ready");

    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["conversation"].empty());
    CHECK((*state)["payload"]["context"]["projectName"] == "Fixture Project");
    CHECK((*state)["payload"]["context"]["printer"]["preset"] == "Test Printer 0.4 nozzle");
    CHECK((*state)["payload"]["context"]["plates"][0]["objects"].size() == 2);
    // The envelope carries the workspace session identity.
    CHECK((*state)["sessionId"].get<std::string>() == std::to_string(harness.workspace.snapshot().session.value()));
}

TEST_CASE("an unsupported page version is rejected and other traffic requires the handshake", "[agent][bridge]")
{
    Harness harness;

    SECTION("hello with a foreign version list is rejected") {
        harness.deliver("hello", json{{"protocolVersions", json::array({99})}});
        REQUIRE_FALSE(harness.host.handshake_complete());
        const json* reject = harness.last_of_type("hello_reject");
        REQUIRE(reject != nullptr);
        CHECK((*reject)["payload"]["supportedVersions"] == json::array({Protocol::kVersion}));
    }

    SECTION("messages before hello get handshake_required") {
        harness.deliver("state_request");
        const json* error = harness.last_of_type("bridge_error");
        REQUIRE(error != nullptr);
        CHECK((*error)["payload"]["code"] == "handshake_required");
    }

    SECTION("a non-hello envelope with a wrong version is refused") {
        harness.handshake();
        harness.deliver("state_request", json::object(), 99);
        const json* error = harness.last_of_type("bridge_error");
        REQUIRE(error != nullptr);
        CHECK((*error)["payload"]["code"] == "unsupported_version");
    }

    SECTION("malformed and unknown messages produce bridge errors") {
        harness.handshake();
        harness.host.on_page_message("this is not json");
        REQUIRE(harness.last_of_type("bridge_error") != nullptr);
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "malformed_json");

        harness.deliver("launch_missiles");
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "unknown_type");
    }
}

TEST_CASE("a user message streams a deterministic reply to completion", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    const std::string user_id = harness.send_user_message("what am I printing?", "c-1");
    REQUIRE(harness.host.stream_active());
    const json* started = harness.last_of_type("assistant_started");
    REQUIRE(started != nullptr);
    CHECK((*started)["payload"]["inReplyTo"] == user_id);

    harness.pump_all();

    REQUIRE_FALSE(harness.host.stream_active());
    REQUIRE(harness.last_of_type("assistant_completed") != nullptr);

    // Deltas are sequenced from zero without gaps.
    const std::vector<json> deltas = harness.of_type("assistant_delta");
    REQUIRE_FALSE(deltas.empty());
    for (std::size_t i = 0; i < deltas.size(); ++i)
        CHECK(deltas[i]["payload"]["seq"].get<int>() == static_cast<int>(i));

    // The native conversation is authoritative and mentions the fixture.
    REQUIRE(harness.host.conversation().size() == 2);
    const ConversationMessage& reply = harness.host.conversation().back();
    CHECK(reply.role == MessageRole::Assistant);
    CHECK(reply.state == MessageState::Complete);
    CHECK_THAT(reply.text, Catch::Matchers::ContainsSubstring("Fixture Project"));
    CHECK_THAT(reply.text, Catch::Matchers::ContainsSubstring("cube-a"));

    // Every host envelope has a unique ID.
    std::set<std::string> ids;
    for (const json& envelope : harness.sent)
        ids.insert(envelope["id"].get<std::string>());
    CHECK(ids.size() == harness.sent.size());
}

TEST_CASE("a duplicated clientMessageId cannot create a second message", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("hello", "c-dup");
    harness.pump_all();
    const std::size_t message_count = harness.host.conversation().size();

    harness.deliver("user_message", json{{"clientMessageId", "c-dup"}, {"text", "hello"}});
    CHECK(harness.host.conversation().size() == message_count);
    CHECK_FALSE(harness.host.stream_active());
    // The duplicate is still acknowledged for the page's benefit.
    CHECK(harness.of_type("message_added").size() == 2);
}

TEST_CASE("stop ends the stream and the reply is marked stopped", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("/slow tell me everything", "c-slow");
    harness.host.pump_stream();
    harness.host.pump_stream();
    REQUIRE(harness.host.stream_active());

    const std::string reply_id = (*harness.last_of_type("assistant_started"))["payload"]["messageId"].get<std::string>();
    harness.deliver("stop_generation", json{{"messageId", reply_id}});

    CHECK_FALSE(harness.host.stream_active());
    REQUIRE(harness.last_of_type("assistant_stopped") != nullptr);
    CHECK(harness.host.conversation().back().state == MessageState::Stopped);

    const std::size_t deltas_after_stop = harness.of_type("assistant_delta").size();
    harness.host.pump_stream();
    CHECK(harness.of_type("assistant_delta").size() == deltas_after_stop);
}

TEST_CASE("failure and retry follow the deterministic scenarios", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    SECTION("/fail fails on every attempt") {
        harness.send_user_message("/fail", "c-f");
        harness.pump_all();
        const json* failed = harness.last_of_type("assistant_failed");
        REQUIRE(failed != nullptr);
        CHECK((*failed)["payload"]["error"]["retryable"] == true);
        const std::string reply_id = (*failed)["payload"]["messageId"].get<std::string>();

        harness.deliver("retry_message", json{{"messageId", reply_id}});
        harness.pump_all();
        CHECK(harness.of_type("assistant_failed").size() == 2);
        CHECK(harness.host.conversation().back().attempt == 2);
    }

    SECTION("/flaky succeeds on the retry") {
        harness.send_user_message("/flaky", "c-y");
        harness.pump_all();
        const std::string reply_id = (*harness.last_of_type("assistant_failed"))["payload"]["messageId"].get<std::string>();

        harness.deliver("retry_message", json{{"messageId", reply_id}});
        harness.pump_all();
        REQUIRE(harness.last_of_type("assistant_completed") != nullptr);
        CHECK(harness.host.conversation().back().state == MessageState::Complete);
        CHECK_THAT(harness.host.conversation().back().text, Catch::Matchers::ContainsSubstring("attempt 2"));
    }

    SECTION("retrying a completed message is refused") {
        harness.send_user_message("hello", "c-ok");
        harness.pump_all();
        const std::string reply_id = (*harness.last_of_type("assistant_completed"))["payload"]["messageId"].get<std::string>();
        harness.deliver("retry_message", json{{"messageId", reply_id}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_retry");
    }
}

TEST_CASE("workspace changes push fresh context to the page", "[agent][context]")
{
    Harness harness;
    harness.handshake();
    const std::size_t context_events_before = harness.of_type("context").size();

    const Workspace::WorkspaceSnapshot before = harness.workspace.snapshot();
    REQUIRE(harness.workspace.select_object(before.plates[0].objects[1].id).succeeded());

    const std::vector<json> contexts = harness.of_type("context");
    REQUIRE(contexts.size() == context_events_before + 1);
    const json& context = contexts.back()["payload"]["context"];
    CHECK(context["selection"]["status"] == "objects");
    CHECK(context["selection"]["objectIds"] == json::array({"22"}));
    // The pushed context carries the committed revision.
    CHECK(context["revision"].get<std::uint64_t>() == harness.workspace.snapshot().revision);

    SECTION("changes made outside the bridge propagate the same way") {
        REQUIRE(harness.workspace.rename_object(before.plates[0].objects[0].id, "renamed-cube").succeeded());
        const std::vector<json> renamed_contexts = harness.of_type("context");
        const json renamed = renamed_contexts.back()["payload"]["context"];
        CHECK(renamed["plates"][0]["objects"][0]["name"] == "renamed-cube");
    }
}

TEST_CASE("project replacement invalidates the session in pushed context", "[agent][context]")
{
    Harness harness;
    harness.handshake();
    const std::string old_session = std::to_string(harness.workspace.snapshot().session.value());

    Workspace::WorkspaceSnapshot replacement;
    replacement.setup.project_name = "Replacement";
    harness.workspace.replace_project(replacement);

    const std::vector<json> contexts = harness.of_type("context");
    const json context = contexts.back()["payload"]["context"];
    CHECK(context["sessionId"].get<std::string>() != old_session);
    CHECK(context["projectName"] == "Replacement");
    CHECK(context["plates"].empty());
}

TEST_CASE("reload reconstructs the page from native state, mid-stream included", "[agent][reload]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("/slow walk me through it", "c-r");
    harness.host.pump_stream();
    harness.host.pump_stream();
    REQUIRE(harness.host.stream_active());
    const std::string partial_text = harness.host.conversation().back().text;
    REQUIRE_FALSE(partial_text.empty());

    // The page reloads: handshake is required again, and the paused stream
    // sends nothing while disconnected.
    harness.host.reset_page();
    REQUIRE_FALSE(harness.host.handshake_complete());
    const std::size_t deltas_before = harness.of_type("assistant_delta").size();
    harness.host.pump_stream();
    CHECK(harness.of_type("assistant_delta").size() == deltas_before);

    harness.deliver("state_request");
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "handshake_required");

    harness.handshake();
    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["conversation"].size() == 2);
    CHECK((*state)["payload"]["conversation"][1]["text"] == partial_text);
    CHECK((*state)["payload"]["streamingMessageId"] == harness.host.conversation().back().id);

    // The stream resumes after reconnection and completes.
    harness.pump_all();
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
}

TEST_CASE("agent availability is a separate, honest state", "[agent][availability]")
{
    Harness harness(AgentAvailability::Unavailable);
    harness.handshake();

    CHECK((*harness.last_of_type("hello_ack"))["payload"]["agent"]["status"] == "unavailable");

    harness.send_user_message("anyone home?", "c-u");
    const json* failed = harness.last_of_type("assistant_failed");
    REQUIRE(failed != nullptr);
    CHECK((*failed)["payload"]["error"]["code"] == "agent_unavailable");

    harness.host.set_availability(AgentAvailability::Ready);
    CHECK((*harness.last_of_type("agent_status"))["payload"]["status"] == "ready");
}

TEST_CASE("appearance changes reach a connected page", "[agent][appearance]")
{
    Harness harness;
    harness.handshake();
    CHECK((*harness.last_of_type("hello_ack"))["payload"]["appearance"] == "light");

    harness.host.set_appearance(true);
    REQUIRE(harness.last_of_type("appearance") != nullptr);
    CHECK((*harness.last_of_type("appearance"))["payload"]["appearance"] == "dark");
}
