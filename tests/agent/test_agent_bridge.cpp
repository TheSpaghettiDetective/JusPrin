// Contract tests for the Agent bridge host against the fake workspace: the
// versioned handshake, capability report, envelope identity, duplicate and
// stale handling, deterministic conversation streaming, reload
// reconstruction, and workspace context propagation including project-session
// invalidation. GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/DeterministicMockAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

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

ProjectPersistence::Config test_persistence_config()
{
    static std::random_device random;

    ProjectPersistence::Config config;
    for (int attempt = 0; attempt < 10; ++attempt) {
        const auto candidate =
            std::filesystem::temp_directory_path() /
            ("jusprin-bridge-recovery-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
             std::to_string(random()));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            config.recovery_root = candidate.string();
            break;
        }
        if (error && error != std::errc::file_exists)
            throw std::runtime_error("Unable to create a test recovery directory: " + error.message());
    }
    if (config.recovery_root.empty())
        throw std::runtime_error("Unable to allocate a unique test recovery directory");

    config.clock = []() { return "2026-08-30T00:00:00Z"; };
    config.uuid  = []() {
        static int next_uuid = 0;
        return "test-" + std::to_string(++next_uuid);
    };
    return config;
}

struct Harness
{
    Workspace::FakeWorkspace workspace;
    ProjectPersistence       persistence;
    AgentHost                host;
    std::vector<json>        sent;

    explicit Harness(AgentAvailability availability = AgentAvailability::Ready)
        : workspace(two_object_snapshot()), persistence(workspace, test_persistence_config()),
          host(workspace, persistence, availability, false, std::make_unique<DeterministicMockAgent>())
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
        persistence.attach();
    }

    explicit Harness(AgentServicePtr agent)
        : workspace(two_object_snapshot()), persistence(workspace, test_persistence_config()),
          host(workspace, persistence, AgentAvailability::Ready, false, std::move(agent))
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
        persistence.attach();
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

namespace {

class ToolCallingAgent final : public IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return active; }
    bool start(const AgentRequest& request) override
    {
        last_request = request;
        ToolRequest tool;
        tool.tool = "duplicate_object";
        tool.title = "Duplicate selected object";
        tool.action_class = ActionClass::Mutation;
        tool.run_ticks = 1;
        tool.arguments_json = json{{"sessionId", std::to_string(request.workspace.session.value())},
                                   {"objectId", std::to_string(request.workspace.selected_objects.front().value())}}.dump();
        events.push_back(AgentEvent::delta("I can do that."));
        events.push_back(AgentEvent::tool_call({"provider-call-1", std::move(tool), true}));
        active = true;
        return true;
    }
    bool continue_after_tool(const AgentToolResult& result) override
    {
        continuation = result;
        events.push_back(AgentEvent::delta(" The native duplicate succeeded."));
        events.push_back(AgentEvent::completed());
        return true;
    }
    void cancel() override { active = false; events.clear(); }
    std::optional<AgentEvent> poll() override
    {
        if (events.empty())
            return std::nullopt;
        AgentEvent event = std::move(events.front());
        events.pop_front();
        if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed)
            active = false;
        return event;
    }

    AgentRequest last_request;
    std::optional<AgentToolResult> continuation;
    std::deque<AgentEvent> events;
    bool active{false};
};

class RetryingAgent final : public IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return active; }
    bool start(const AgentRequest& request) override
    {
        requests.push_back(request);
        active = true;
        if (request.attempt == 1)
            events.push_back(AgentEvent::failed({"provider_timeout", "The provider timed out.", true}));
        else {
            events.push_back(AgentEvent::delta("Recovered without duplicating the turn."));
            events.push_back(AgentEvent::completed());
        }
        return true;
    }
    bool continue_after_tool(const AgentToolResult&) override { return false; }
    void cancel() override { active = false; events.clear(); }
    std::optional<AgentEvent> poll() override
    {
        if (events.empty())
            return std::nullopt;
        AgentEvent event = std::move(events.front());
        events.pop_front();
        if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed)
            active = false;
        return event;
    }

    std::vector<AgentRequest> requests;
    std::deque<AgentEvent> events;
    bool active{false};
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
                                              Protocol::kStopGeneration, Protocol::kRetryMessage, Protocol::kToolDecision,
                                              Protocol::kToolCancel, Protocol::kCreateConversation,
                                              Protocol::kSwitchConversation, Protocol::kRevertToRevision,
                                              Protocol::kDraftUpdate, Protocol::kAttachFile, Protocol::kRemoveAttachment});

    const std::set<std::string> host_types(shared["hostMessageTypes"].begin(), shared["hostMessageTypes"].end());
    CHECK(host_types == std::set<std::string>{Protocol::kHelloAck, Protocol::kHelloReject, Protocol::kState,
                                              Protocol::kContext, Protocol::kAppearance, Protocol::kAgentStatus,
                                              Protocol::kMessageAdded, Protocol::kAssistantStarted, Protocol::kAssistantDelta,
                                              Protocol::kAssistantCompleted, Protocol::kAssistantFailed,
                                              Protocol::kAssistantStopped, Protocol::kToolActivity, Protocol::kRevisionAdded,
                                              Protocol::kBridgeError, Protocol::kAttachmentUpdated});
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
    const ConversationMessage reply = harness.host.conversation().back();
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

namespace {

std::size_t workspace_object_count(const Workspace::FakeWorkspace& workspace)
{
    std::size_t count = 0;
    for (const Workspace::WorkspacePlate& plate : workspace.snapshot().plates)
        count += plate.objects.size();
    return count;
}

// Streams the current reply to completion and returns the pending tool
// activity it proposed.
json propose_duplicate(Harness& harness, const std::string& client_id)
{
    harness.send_user_message("please duplicate the selected object", client_id);
    harness.pump_all();
    REQUIRE(harness.last_of_type("assistant_completed") != nullptr);
    const json* activity_event = harness.last_of_type("tool_activity");
    REQUIRE(activity_event != nullptr);
    return (*activity_event)["payload"]["activity"];
}

void pump_tools_to_completion(Harness& harness, int limit = 1000)
{
    while (harness.host.tools().any_running() && limit-- > 0)
        harness.host.pump_tools();
}

} // namespace

TEST_CASE("a proposed duplicate waits for approval and executes authoritatively", "[agent][tools]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t objects_before = workspace_object_count(harness.workspace);

    const json proposed = propose_duplicate(harness, "c-t1");
    CHECK(proposed["state"] == "pending");
    CHECK(proposed["tool"] == "duplicate_object");
    CHECK(proposed["requiresApproval"] == true);
    CHECK(proposed["actionClass"] == "mutation");
    CHECK(proposed["server"] == "jusprin-native");
    // The activity correlates with the assistant reply that proposed it.
    const json* completed = harness.last_of_type("assistant_completed");
    CHECK(proposed["correlationId"] == (*completed)["payload"]["messageId"]);
    const std::string action_id = proposed["actionId"].get<std::string>();

    SECTION("rejecting executes nothing") {
        harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "reject"}});
        CHECK((*harness.last_of_type("tool_activity"))["payload"]["activity"]["state"] == "rejected");
        pump_tools_to_completion(harness);
        CHECK(workspace_object_count(harness.workspace) == objects_before);
        CHECK_FALSE(harness.workspace.snapshot().can_undo);
    }

    SECTION("approving runs the native command and reports the result") {
        const std::size_t context_events_before = harness.of_type("context").size();
        harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
        pump_tools_to_completion(harness);

        const json done = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(done["state"] == "succeeded");
        CHECK(done["result"].contains("newObjectId"));
        CHECK(workspace_object_count(harness.workspace) == objects_before + 1);
        CHECK(harness.workspace.snapshot().can_undo);
        // The executed change pushed fresh context like any native change.
        CHECK(harness.of_type("context").size() > context_events_before);

        // A replayed approval acknowledges the record without a second run.
        harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
        pump_tools_to_completion(harness);
        CHECK(workspace_object_count(harness.workspace) == objects_before + 1);
        CHECK((*harness.last_of_type("tool_activity"))["payload"]["activity"]["state"] == "succeeded");
    }

    SECTION("an unknown action id is a bridge error") {
        harness.deliver("tool_decision", json{{"actionId", "t-999"}, {"decision", "approve"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "unknown_action");
    }
}

TEST_CASE("cancellation and deterministic failure surface over the bridge", "[agent][tools]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t objects_before = workspace_object_count(harness.workspace);

    SECTION("a slow run reports progress and can be cancelled before execution") {
        harness.send_user_message("/toolslow", "c-t2");
        harness.pump_all();
        const std::string action_id =
            (*harness.last_of_type("tool_activity"))["payload"]["activity"]["actionId"].get<std::string>();
        harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
        harness.host.pump_tools();
        harness.host.pump_tools();
        const json running = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(running["state"] == "running");
        CHECK(running["progress"]["current"].get<int>() > 0);

        harness.deliver("tool_cancel", json{{"actionId", action_id}});
        CHECK((*harness.last_of_type("tool_activity"))["payload"]["activity"]["state"] == "cancelled");
        pump_tools_to_completion(harness);
        CHECK(workspace_object_count(harness.workspace) == objects_before);
        CHECK_FALSE(harness.workspace.snapshot().can_undo);
    }

    SECTION("/toolfail fails during execution and changes nothing") {
        harness.send_user_message("/toolfail", "c-t3");
        harness.pump_all();
        const std::string action_id =
            (*harness.last_of_type("tool_activity"))["payload"]["activity"]["actionId"].get<std::string>();
        harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
        pump_tools_to_completion(harness);
        const json failed = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(failed["state"] == "failed");
        CHECK(failed["error"]["code"] == "missing_object");
        CHECK(workspace_object_count(harness.workspace) == objects_before);
    }

    SECTION("a native change before the decision marks the proposal stale") {
        propose_duplicate(harness, "c-t4");
        REQUIRE(harness.workspace.rename_object(harness.workspace.snapshot().plates[0].objects[1].id, "renamed").succeeded());
        const json stale = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(stale["state"] == "failed");
        CHECK(stale["error"]["code"] == "stale_revision");
        pump_tools_to_completion(harness);
        CHECK(workspace_object_count(harness.workspace) == objects_before);
    }
}

TEST_CASE("tool activities reconstruct after a reload and pause while disconnected", "[agent][tools][reload]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t objects_before = workspace_object_count(harness.workspace);

    const json proposed = propose_duplicate(harness, "c-t5");
    const std::string action_id = proposed["actionId"].get<std::string>();
    harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
    harness.host.pump_tools();
    REQUIRE(harness.host.tools().any_running());

    // The page reloads mid-run: execution pauses while disconnected.
    harness.host.reset_page();
    for (int i = 0; i < 10; ++i)
        harness.host.pump_tools();
    CHECK(harness.host.tools().any_running());
    CHECK(workspace_object_count(harness.workspace) == objects_before);

    // The new handshake reconstructs the activity from native state and the
    // run resumes to completion.
    harness.handshake();
    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    REQUIRE((*state)["payload"]["toolActivities"].size() == 1);
    CHECK((*state)["payload"]["toolActivities"][0]["actionId"] == action_id);
    CHECK((*state)["payload"]["toolActivities"][0]["state"] == "running");

    pump_tools_to_completion(harness);
    CHECK((*harness.last_of_type("tool_activity"))["payload"]["activity"]["state"] == "succeeded");
    CHECK(workspace_object_count(harness.workspace) == objects_before + 1);
}

TEST_CASE("a read-only tool runs without approval over the bridge", "[agent][tools][policy]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());

    harness.send_user_message("/inspect", "c-t6");
    harness.pump_all();
    const json proposed = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
    CHECK(proposed["requiresApproval"] == false);
    CHECK(proposed["actionClass"] == "read_only");

    pump_tools_to_completion(harness);
    const json done = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
    CHECK(done["state"] == "succeeded");
    CHECK(done["result"]["selection"] == json::array({"cube-a"}));
    CHECK_FALSE(harness.workspace.snapshot().can_undo);
}

TEST_CASE("conversations are created and switched over the bridge", "[agent][conversations]")
{
    Harness harness;
    harness.handshake();

    // The adopted fresh project starts with one conversation.
    const json* initial = harness.last_of_type("state");
    REQUIRE(initial != nullptr);
    REQUIRE((*initial)["payload"]["conversations"].size() == 1);
    const std::string first = (*initial)["payload"]["activeConversationId"].get<std::string>();

    harness.send_user_message("hello in the first conversation", "c-c1");
    harness.pump_all();

    harness.deliver("create_conversation", json{{"title", "Second"}});
    const json* created = harness.last_of_type("state");
    REQUIRE((*created)["payload"]["conversations"].size() == 2);
    const std::string second = (*created)["payload"]["activeConversationId"].get<std::string>();
    CHECK(second != first);
    CHECK((*created)["payload"]["conversation"].empty());

    harness.send_user_message("hello in the second conversation", "c-c2");
    harness.pump_all();
    CHECK(harness.host.conversation().size() == 2);

    harness.deliver("switch_conversation", json{{"conversationId", first}});
    const json* switched = harness.last_of_type("state");
    CHECK((*switched)["payload"]["activeConversationId"] == first);
    CHECK((*switched)["payload"]["conversation"][0]["text"] == "hello in the first conversation");

    SECTION("switching to an unknown conversation is refused") {
        harness.deliver("switch_conversation", json{{"conversationId", "c-999"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "unknown_conversation");
    }

    SECTION("changing conversations is refused while a reply streams") {
        harness.send_user_message("/slow tell me more", "c-c3");
        REQUIRE(harness.host.stream_active());
        harness.deliver("create_conversation", json::object());
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "busy");
        harness.deliver("switch_conversation", json{{"conversationId", second}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "busy");
        harness.pump_all();
    }
}

TEST_CASE("the draft lives in the recovery store and clears when sent", "[agent][draft]")
{
    Harness harness;
    harness.handshake();

    harness.deliver("draft_update", json{{"text", "half-typed thought"}});
    CHECK(harness.persistence.draft() == "half-typed thought");

    harness.deliver("state_request");
    CHECK((*harness.last_of_type("state"))["payload"]["draft"] == "half-typed thought");

    harness.send_user_message("half-typed thought, finished", "c-d1");
    CHECK(harness.persistence.draft().empty());
    harness.pump_all();
}

TEST_CASE("manufacturing changes surface as revisions over the bridge", "[agent][revisions]")
{
    Harness harness;
    harness.handshake();

    // Adoption captured the initial revision before the handshake.
    const json* state = harness.last_of_type("state");
    REQUIRE((*state)["payload"]["revisions"].size() == 1);
    CHECK((*state)["payload"]["revisions"][0]["cause"] == "initial");
    CHECK((*state)["payload"]["revisions"][0]["current"] == true);

    const Workspace::WorkspaceSnapshot before = harness.workspace.snapshot();
    REQUIRE(harness.workspace.rename_object(before.plates[0].objects[0].id, "renamed-cube").succeeded());

    const json* added = harness.last_of_type("revision_added");
    REQUIRE(added != nullptr);
    CHECK((*added)["payload"]["revision"]["current"] == true);
    CHECK((*added)["payload"]["revision"]["revertible"] == true);

    SECTION("selection changes do not create revisions") {
        const std::size_t revision_events = harness.of_type("revision_added").size();
        REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[1].id).succeeded());
        CHECK(harness.of_type("revision_added").size() == revision_events);
    }
}

TEST_CASE("revert here restores native state and truncates every conversation", "[agent][revert]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("first message", "c-r1");
    harness.pump_all();

    // The revert target: the revision created by the first change. The first
    // exchange happened before it and survives; everything after it goes.
    REQUIRE(harness.workspace.rename_object(harness.workspace.snapshot().plates[0].objects[0].id, "renamed-once").succeeded());
    const std::string target_revision = harness.persistence.document().current_revision_id();

    REQUIRE(harness.workspace.rename_object(harness.workspace.snapshot().plates[0].objects[0].id, "renamed-twice").succeeded());
    harness.send_user_message("second message, after the change", "c-r2");
    harness.pump_all();
    harness.deliver("create_conversation", json{{"title", "Later"}});
    harness.send_user_message("third message in a later conversation", "c-r3");
    harness.pump_all();
    REQUIRE(harness.persistence.document().conversations().size() == 2);

    harness.deliver("revert_to_revision", json{{"revisionId", target_revision}});

    // The native project is back at the target revision's state...
    CHECK(harness.workspace.snapshot().plates[0].objects[0].name == "renamed-once");
    // ...the reconstructed state was pushed...
    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    // ...later editable entries are gone across every conversation: the
    // post-change message and the whole later conversation.
    CHECK((*state)["payload"]["conversations"].size() == 1);
    const json& conversation = (*state)["payload"]["conversation"];
    REQUIRE(conversation.size() == 2);
    CHECK(conversation[0]["text"] == "first message");
    // ...and later revisions are removed with the target current again.
    for (const json& revision : (*state)["payload"]["revisions"])
        CHECK(revision["current"] == (revision["id"] == target_revision));
    CHECK(harness.persistence.document().current_revision_id() == target_revision);
    // Native history keeps no redo path back to the removed state.
    CHECK_FALSE(harness.workspace.snapshot().can_redo);

    SECTION("reverting to an unknown revision fails without changes") {
        const std::size_t conversation_count = harness.host.conversation().size();
        harness.deliver("revert_to_revision", json{{"revisionId", "r-999"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "revert_failed");
        CHECK(harness.host.conversation().size() == conversation_count);
    }
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

namespace {

// Minimal base64 encoder for building attach_file payloads in tests.
std::string b64(const std::string& in)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int         val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

json attach(Harness& h, const std::string& client_id, const std::string& name, const std::string& bytes,
            const std::string& mime = "", const std::string& source = "picker")
{
    json payload{{"clientAttachmentId", client_id}, {"name", name}, {"source", source}, {"dataBase64", b64(bytes)}};
    if (!mime.empty())
        payload["mime"] = mime;
    h.deliver("attach_file", payload);
    const json* updated = h.last_of_type("attachment_updated");
    REQUIRE(updated != nullptr);
    return (*updated)["payload"]["attachment"];
}

} // namespace

TEST_CASE("a text attachment is staged, decoded, and previewed", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json a = attach(h, "ca-1", "notes.txt", "hello world");
    CHECK(a["id"] == "a-1");
    CHECK(a["kind"] == "text");
    CHECK(a["state"] == "staged");
    CHECK(a["name"] == "notes.txt");
    CHECK(a["previewText"] == "hello world");
    // The blob is on disk under the project's auxiliary directory.
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::path(h.persistence.jusprin_data_dir()) / "attachments" / "a-1" / "notes.txt"));
}

TEST_CASE("an image attachment gets an inline preview data URL", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json a = attach(h, "ca-img", "pic.png", std::string("\x89PNG\r\n", 6), "image/png");
    CHECK(a["kind"] == "image");
    CHECK(a["state"] == "staged");
    REQUIRE(a.contains("previewDataUrl"));
    CHECK(a["previewDataUrl"].get<std::string>().rfind("data:image/png;base64,", 0) == 0);
    // The large data URL is not persisted in state.json; it is rebuilt on demand.
    std::ifstream     in(h.persistence.state_file_path());
    std::stringstream ss;
    ss << in.rdbuf();
    const json on_disk = json::parse(ss.str());
    CHECK_FALSE(on_disk["attachments"][0].contains("previewDataUrl"));
}

TEST_CASE("an unsupported binary attachment is rejected visibly", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json a = attach(h, "ca-bin", "firmware.bin", std::string("\x00\x01\x02\x03", 4));
    CHECK(a["kind"] == "unsupported");
    CHECK(a["state"] == "error");
    CHECK(a["error"]["code"] == "unsupported_type");
}

TEST_CASE("resending the same client attachment id is idempotent", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    attach(h, "ca-dup", "a.txt", "one");
    attach(h, "ca-dup", "a.txt", "one");
    CHECK(h.persistence.document().attachments().size() == 1);
}

TEST_CASE("a project-reference attachment carries a summary and no blob", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    h.deliver("attach_file", json{{"clientAttachmentId", "ca-ref"},
                                  {"source", "project"},
                                  {"refKind", "model"},
                                  {"label", "cube-a (on Plate 1)"}});
    const json* updated = h.last_of_type("attachment_updated");
    REQUIRE(updated != nullptr);
    const json a = (*updated)["payload"]["attachment"];
    CHECK(a["kind"] == "model");
    CHECK(a["summary"] == "cube-a (on Plate 1)");
    CHECK(a["state"] == "staged");
}

TEST_CASE("a message carries staged attachments and the mock acknowledges them", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    attach(h, "ca-1", "notes.txt", "hello world");
    h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", "what is this"}, {"attachmentIds", json::array({"a-1"})}});

    const json* added = h.last_of_type("message_added");
    REQUIRE(added != nullptr);
    CHECK((*added)["payload"]["message"]["attachments"] == json::array({"a-1"}));
    // The attachment is now durable history, not staged working state.
    REQUIRE(h.persistence.document().find_attachment("a-1").has_value());
    CHECK(h.persistence.document().find_attachment("a-1")->state == "sent");

    h.pump_all();
    const json* completed = h.last_of_type("assistant_completed");
    REQUIRE(completed != nullptr);
    // The streamed reply names the attachment, proving the context reached the Agent.
    const std::vector<ConversationMessage> conversation =
        h.persistence.document().messages(h.persistence.document().active_conversation_id());
    REQUIRE(conversation.size() >= 2);
    CHECK(conversation.back().text.find("notes.txt") != std::string::npos);
}

TEST_CASE("an attachment-only message is accepted but an empty one is not", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    attach(h, "ca-1", "notes.txt", "hello");

    SECTION("text may be empty when an attachment is present") {
        h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", ""}, {"attachmentIds", json::array({"a-1"})}});
        CHECK(h.last_of_type("message_added") != nullptr);
    }
    SECTION("a message with neither text nor attachment is refused") {
        const std::size_t before = h.of_type("bridge_error").size();
        h.deliver("user_message", json{{"clientMessageId", "c-2"}, {"text", ""}});
        REQUIRE(h.of_type("bridge_error").size() == before + 1);
        CHECK(h.of_type("bridge_error").back()["payload"]["code"] == "invalid_payload");
    }
}

TEST_CASE("removing a staged attachment deletes its record and blob", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    attach(h, "ca-1", "notes.txt", "hello");
    const auto blob = std::filesystem::path(h.persistence.jusprin_data_dir()) / "attachments" / "a-1" / "notes.txt";
    REQUIRE(std::filesystem::is_regular_file(blob));

    h.deliver("remove_attachment", json{{"attachmentId", "a-1"}});
    CHECK(h.persistence.document().attachments().empty());
    CHECK_FALSE(std::filesystem::exists(blob));
    const json* state = h.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["attachments"].empty());
}

TEST_CASE("reload reconstructs staged attachments from native state", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    attach(h, "ca-1", "notes.txt", "hello world");

    // The page reloads: a fresh handshake must rebuild the staged attachment
    // (with its decoded preview) from authoritative native state.
    h.host.reset_page();
    h.handshake();
    h.deliver("state_request");
    const json* state = h.last_of_type("state");
    REQUIRE(state != nullptr);
    const json attachments = (*state)["payload"]["attachments"];
    REQUIRE(attachments.size() == 1);
    CHECK(attachments[0]["id"] == "a-1");
    CHECK(attachments[0]["previewText"] == "hello world");
}

TEST_CASE("a sent model attachment is imported through an approved tool action", "[agent][attachments][import]")
{
    Harness h;
    h.handshake();

    // Attach a model file; it is stored but reaches the Agent only as a summary.
    const json a = attach(h, "ca-model", "widget.stl", "solid s\nendsolid s\n");
    CHECK(a["kind"] == "model");
    REQUIRE(a.contains("summary"));

    const std::size_t objects_before = h.workspace.snapshot().plates.at(0).objects.size();

    h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", "add this"}, {"attachmentIds", json::array({"a-1"})}});
    h.pump_all(); // finish the streamed reply, which proposes the import

    const json* activity = h.last_of_type("tool_activity");
    REQUIRE(activity != nullptr);
    const std::string action_id = (*activity)["payload"]["activity"]["actionId"].get<std::string>();
    CHECK((*activity)["payload"]["activity"]["tool"] == "import_model");
    CHECK((*activity)["payload"]["activity"]["requiresApproval"] == true);

    // Approve; the coordinator resolves the attachment to its blob and imports
    // it through the workspace, adding one object.
    h.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
    for (int i = 0; i < 100 && h.host.tools().any_running(); ++i)
        h.host.pump_tools();

    CHECK(h.workspace.snapshot().plates.at(0).objects.size() == objects_before + 1);
    const ToolActivity* record = h.host.tools().find(action_id);
    REQUIRE(record != nullptr);
    CHECK(record->state == ToolState::Succeeded);
}

TEST_CASE("a provider tool call continues from the structured native result", "[agent][provider][tools]")
{
    auto provider = std::make_unique<ToolCallingAgent>();
    ToolCallingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t objects_before = harness.workspace.snapshot().plates[0].objects.size();

    harness.send_user_message("duplicate it", "provider-c-1");
    for (int i = 0; i < 10 && harness.host.tools().activities().empty(); ++i)
        harness.host.pump_stream();
    REQUIRE(harness.host.tools().activities().size() == 1);
    const std::string action_id = harness.host.tools().activities().back().action_id;
    CHECK(harness.host.tools().activities().back().state == ToolState::Pending);
    CHECK(scripted->last_request.workspace.selected_objects.size() == 1);
    CHECK(scripted->last_request.request_id == harness.persistence.document().project_id() + "-" +
                                                   harness.persistence.document().active_conversation_id() +
                                                   "-m-2-attempt-1");

    harness.deliver("tool_decision", json{{"actionId", action_id}, {"decision", "approve"}});
    for (int i = 0; i < 20 && !scripted->continuation; ++i)
        harness.host.pump_tools();
    REQUIRE(scripted->continuation);
    CHECK(scripted->continuation->call_id == "provider-call-1");
    CHECK(scripted->continuation->state == "succeeded");
    const json result = json::parse(scripted->continuation->output_json);
    CHECK(result["state"] == "succeeded");
    CHECK(result.contains("workspaceRevision"));
    CHECK(result["workspace"]["plates"][0]["objects"].size() == objects_before + 1);

    for (int i = 0; i < 20 && harness.host.stream_active(); ++i)
        harness.host.pump_stream();
    CHECK(harness.workspace.snapshot().plates[0].objects.size() == objects_before + 1);
    REQUIRE(harness.host.conversation().size() == 3);
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
    CHECK(harness.host.conversation().back().text.find("native duplicate succeeded") != std::string::npos);
}

TEST_CASE("a provider retry reuses the native message without duplicating the turn", "[agent][provider][retry]")
{
    auto provider = std::make_unique<RetryingAgent>();
    RetryingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();

    harness.send_user_message("try the provider", "provider-retry-c-1");
    harness.pump_all();
    REQUIRE(scripted->requests.size() == 1);
    REQUIRE(harness.host.conversation().size() == 2);
    REQUIRE(harness.host.conversation().back().state == MessageState::Failed);
    const std::string assistant_id = harness.host.conversation().back().id;

    harness.deliver("retry_message", json{{"messageId", assistant_id}});
    harness.pump_all();
    REQUIRE(scripted->requests.size() == 2);
    CHECK(scripted->requests[0].request_id != scripted->requests[1].request_id);
    CHECK(scripted->requests[0].request_id.find(assistant_id + "-attempt-1") != std::string::npos);
    CHECK(scripted->requests[1].request_id.find(assistant_id + "-attempt-2") != std::string::npos);
    REQUIRE(harness.host.conversation().size() == 2);
    CHECK(harness.host.conversation().back().id == assistant_id);
    CHECK(harness.host.conversation().back().attempt == 2);
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
    CHECK(harness.host.conversation().back().text == "Recovered without duplicating the turn.");
}

TEST_CASE("provider attachment context is size bounded on a UTF-8 boundary", "[agent][provider][attachments]")
{
    auto provider = std::make_unique<ToolCallingAgent>();
    ToolCallingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());

    std::string text(256u * 1024u - 1u, 'a');
    text += "\xE2\x82\xACtail";
    attach(harness, "large-text", "large.txt", text);
    harness.deliver("user_message", json{{"clientMessageId", "bounded-c-1"},
                                          {"text", "use the attachment"},
                                          {"attachmentIds", json::array({"a-1"})}});

    REQUIRE(scripted->last_request.attachments.size() == 1);
    const auto& supplied = scripted->last_request.attachments.front();
    CHECK(supplied.text.size() == 256u * 1024u - 1u);
    CHECK(supplied.text.back() == 'a');
    CHECK(supplied.summary.find("truncated") != std::string::npos);
}
