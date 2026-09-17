#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"

#include <nlohmann/json.hpp>

using namespace Slic3r::GUI::JusPrin::Agent;
namespace Workspace = Slic3r::GUI::JusPrin::Workspace;
using nlohmann::json;

namespace {

class FakeTransport final : public IAgentHttpTransport
{
public:
    bool post(AgentHttpRequest request, EventFn event) override
    {
        requests.push_back(std::move(request));
        callbacks.push_back(std::move(event));
        return accept_posts;
    }
    void cancel() override { cancelled = true; }

    void data(std::string value) { callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Data, std::move(value), {}, 0}); }
    void complete() { callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, 200}); }
    void error(unsigned status, std::string detail = {})
    {
        callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Error, {}, std::move(detail), status});
    }

    std::vector<AgentHttpRequest> requests;
    std::vector<EventFn> callbacks;
    bool cancelled{false};
    bool accept_posts{true};
};

std::string sse(const json& event) { return "event: message\ndata: " + event.dump() + "\n\n"; }

AgentRequest request_fixture()
{
    AgentRequest request;
    request.request_id = "assistant-7-attempt-1";
    request.user_text = "Duplicate the selected cube.";
    request.workspace.session = Workspace::ProjectSessionId(41);
    request.workspace.revision = 9;
    request.workspace.setup.project_name = "Fixture";
    Workspace::WorkspaceObject object;
    object.id = Workspace::ObjectId(request.workspace.session, 72);
    object.name = "cube";
    Workspace::WorkspacePlate plate;
    plate.id = Workspace::PlateId(request.workspace.session, 3);
    plate.name = "Plate 1";
    plate.active = true;
    plate.objects.push_back(object);
    request.workspace.plates.push_back(plate);
    request.workspace.selected_objects.push_back(object.id);
    request.conversation.push_back({"assistant", "Earlier answer"});
    request.attachments.push_back({"att-1", "notes.txt", "text", "text/plain", "", "do not scale", "", false});
    return request;
}

std::optional<AgentEvent> poll_until(OpenAIResponsesAgent& agent, AgentEventKind kind)
{
    for (int i = 0; i < 20; ++i) {
        auto event = agent.poll();
        if (event && event->kind == kind)
            return event;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("OpenAI request preserves canonical schemas with compatible strictness", "[agent][openai]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"secret-key", "gpt-5.4-mini", "https://api.openai.com/v1/responses"},
                               std::move(transport));

    REQUIRE(agent.start(request_fixture()));
    REQUIRE(fake->requests.size() == 1);
    const AgentHttpRequest& wire = fake->requests.front();
    CHECK(wire.authorization == "Bearer secret-key");
    CHECK(wire.idempotency_key == "assistant-7-attempt-1-request-1");
    CHECK(wire.body.find("secret-key") == std::string::npos);
    const json body = json::parse(wire.body);
    CHECK(body["model"] == "gpt-5.4-mini");
    CHECK(body["stream"] == true);
    CHECK(body["store"] == false);
    CHECK(body["parallel_tool_calls"] == false);
    REQUIRE(body["tools"].size() == 25);
    std::vector<std::string> emitted_names;
    for (const json& tool : body["tools"]) {
        const std::string name = tool["name"];
        // Strict mode cannot express an optional argument, so a tool gains one by
        // giving it up: workspace_inspect did when it gained sections, settings_get
        // when it gained a target.
        CHECK(tool["strict"] == (name == "history_restore" || name == "printer_list" || name == "object_merge" ||
                                 name == "object_repair"));
        CHECK(tool["parameters"]["additionalProperties"] == false);
        const ToolDefinition* definition = ToolRegistry::instance().find(tool["name"].get<std::string>());
        REQUIRE(definition != nullptr);
        emitted_names.push_back(definition->name);
        CHECK(has_exposure(definition->exposure, ToolExposure::InApp));
        CHECK(tool["description"] == definition->description);
        CHECK(tool["parameters"] == definition->input_schema);
    }
    CHECK(emitted_names == std::vector<std::string>{"history_restore", "intent_update", "object_analyze", "object_divide", "object_divide_preview", "object_merge", "object_place", "object_repair", "plan_set", "plate_layout", "presets_list", "printer_list", "printer_setup", "printer_setup_preview", "project_delete_items", "project_open", "project_save", "region_annotate", "settings_apply_patch", "settings_get", "settings_preview_patch", "settings_search", "slice_report", "slice_start", "workspace_inspect"});
    const std::string serialized = body["input"].dump();
    CHECK(serialized.find("sessionId") != std::string::npos);
    CHECK(serialized.find("72") != std::string::npos);
    CHECK(serialized.find("do not scale") != std::string::npos);
}

TEST_CASE("OpenAI SSE deltas and completion become typed agent events", "[agent][openai]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));

    const std::string first = sse(json{{"type", "response.output_text.delta"}, {"delta", "Done"}});
    fake->data(first.substr(0, 17));
    CHECK_FALSE(agent.poll().has_value());
    fake->data(first.substr(17) + sse(json{{"type", "response.completed"},
                                          {"response", json{{"output", json::array()}}}}));
    fake->complete();
    const auto delta = poll_until(agent, AgentEventKind::TextDelta);
    REQUIRE(delta);
    CHECK(delta->text == "Done");
    REQUIRE(poll_until(agent, AgentEventKind::Completed));
    CHECK_FALSE(agent.busy());
}

TEST_CASE("automatic chat titles use the configured model without tools or workspace data", "[agent][openai][conversations]")
{
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    OpenAIResponsesAgent agent({"secret-key", "configured-model"}, std::move(transport));
    auto request = request_fixture();
    request.purpose = AgentRequest::Purpose::ConversationTitle;
    request.conversation = {{"user", "Help me print a backpack frame"}, {"assistant", "Use five walls."}};
    REQUIRE(agent.start(request));
    const auto body = json::parse(fake->requests.back().body);
    CHECK(body["model"] == "configured-model");
    CHECK(body["store"] == false);
    CHECK_FALSE(body.contains("tools"));
    CHECK(body["input"].size() == 2);
    CHECK(body.dump().find("sessionId") == std::string::npos);
    CHECK(body.dump().find("notes.txt") == std::string::npos);
    fake->data(sse(json{{"type", "response.output_text.delta"}, {"delta", "Backpack frame print"}}) +
               sse(json{{"type", "response.completed"}, {"response", json{{"output", json::array()}}}}));
    REQUIRE(poll_until(agent, AgentEventKind::TextDelta));
    REQUIRE(poll_until(agent, AgentEventKind::Completed));
    CHECK_FALSE(agent.busy());
}

TEST_CASE("OpenAI exposes attachment import only when its registered availability is satisfied",
          "[agent][openai][tools]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    AgentRequest request = request_fixture();
    request.attachments.push_back({"model-1", "part.stl", "model", "model/stl", "", "", "", true});
    REQUIRE(agent.start(request));

    const json tools = json::parse(fake->requests.front().body)["tools"];
    std::vector<std::string> names;
    for (const json& tool : tools)
        names.push_back(tool["name"].get<std::string>());
    CHECK(names == std::vector<std::string>{"history_restore", "intent_update", "object_analyze", "object_divide", "object_divide_preview", "object_import", "object_merge", "object_place", "object_repair", "plan_set", "plate_layout", "presets_list", "printer_list", "printer_setup", "printer_setup_preview", "project_delete_items", "project_open", "project_save", "region_annotate", "settings_apply_patch", "settings_get", "settings_preview_patch", "settings_search", "slice_report", "slice_start", "workspace_inspect"});

    const json call{{"type", "function_call"},
                    {"call_id", "call-import"},
                    {"name", "object_import"},
                    {"arguments", json{{"sessionId", "41"}, {"attachmentId", "model-1"}}.dump()}};
    fake->data(sse(json{{"type", "response.completed"},
                        {"response", json{{"output", json::array({call})}}}}));
    fake->complete();
    const auto event = poll_until(agent, AgentEventKind::ToolCall);
    REQUIRE(event);
    REQUIRE(event->tool);
    CHECK(event->tool->request.tool == "object_import");
}

TEST_CASE("OpenAI consumes a final SSE frame without a trailing delimiter", "[agent][openai]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));
    const json completed{{"type", "response.completed"}, {"response", json{{"output", json::array()}}}};
    fake->data("event: response.completed\r\ndata: " + completed.dump());
    fake->complete();
    REQUIRE(poll_until(agent, AgentEventKind::Completed));
    CHECK_FALSE(agent.busy());
}

TEST_CASE("OpenAI tool continuation retains user context and every prior tool result", "[agent][openai][tools]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));

    const json reasoning{{"type", "reasoning"}, {"id", "reasoning-1"}, {"summary", json::array()}};
    const json call{{"type", "function_call"}, {"id", "item-1"}, {"call_id", "call-9"},
                    {"name", "plate_layout"},
                    {"arguments", json{{"sessionId", "41"}, {"objects", json::array({json{{"objectId", "72"}, {"quantity", 2}}})}}.dump()}};
    fake->data(sse(json{{"type", "response.completed"},
                        {"response", json{{"output", json::array({reasoning, call})}}}}));
    fake->complete();
    const auto event = poll_until(agent, AgentEventKind::ToolCall);
    REQUIRE(event);
    REQUIRE(event->tool);
    CHECK(event->tool->call_id == "call-9");
    CHECK(event->tool->request.tool == "plate_layout");
    CHECK(agent.busy());

    REQUIRE(agent.continue_after_tool({"call-9", "succeeded", R"({"state":"succeeded","result":{"objectId":"73"}})"}));
    REQUIRE(fake->requests.size() == 2);
    CHECK(fake->requests.back().idempotency_key == "assistant-7-attempt-1-request-2");
    const json continuation = json::parse(fake->requests.back().body)["input"];
    const json initial = json::parse(fake->requests.front().body)["input"];
    REQUIRE(continuation.size() == initial.size() + 3);
    for (std::size_t i = 0; i < initial.size(); ++i) CHECK(continuation[i] == initial[i]);
    CHECK(continuation[initial.size()] == reasoning);
    CHECK(continuation[initial.size() + 1] == call);
    CHECK(continuation.back()["type"] == "function_call_output");
    CHECK(continuation.back()["call_id"] == "call-9");

    const json next_call{{"type", "function_call"}, {"call_id", "call-10"}, {"name", "settings_get"},
                         {"arguments", R"({"keys":["wall_loops"]})"}};
    fake->data(sse(json{{"type", "response.completed"}, {"response", {{"output", json::array({next_call})}}}}));
    const auto next_event = poll_until(agent, AgentEventKind::ToolCall);
    REQUIRE(next_event);
    REQUIRE(next_event->tool);
    CHECK(next_event->tool->call_id == "call-10");
    REQUIRE(agent.continue_after_tool({"call-10", "succeeded", R"({"items":[{"key":"wall_loops","value":"2"}]})"}));
    const json third_input = json::parse(fake->requests.back().body)["input"];
    REQUIRE(third_input.size() == continuation.size() + 2);
    for (std::size_t i = 0; i < continuation.size(); ++i) CHECK(third_input[i] == continuation[i]);
    CHECK(third_input[continuation.size()] == next_call);
    CHECK(third_input.back()["call_id"] == "call-10");
}

TEST_CASE("a late completion from the tool-call request cannot end its continuation", "[agent][openai][tools]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));

    const json call{{"type", "function_call"}, {"call_id", "call-9"}, {"name", "plate_layout"},
                    {"arguments", json{{"sessionId", "41"}, {"objects", json::array({json{{"objectId", "72"}, {"quantity", 2}}})}}.dump()}};
    fake->data(sse(json{{"type", "response.completed"}, {"response", json{{"output", json::array({call})}}}}));
    REQUIRE(poll_until(agent, AgentEventKind::ToolCall));
    REQUIRE(agent.continue_after_tool({"call-9", "succeeded", R"({"state":"succeeded"})"}));

    // The first request's libcurl completion can race with page approval and
    // arrive after the continuation has already started.
    fake->callbacks.front()(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, 200});
    CHECK_FALSE(agent.poll());
    CHECK(agent.busy());

    fake->data(sse(json{{"type", "response.completed"}, {"response", json{{"output", json::array()}}}}));
    fake->complete();
    REQUIRE(poll_until(agent, AgentEventKind::Completed));
    CHECK_FALSE(agent.busy());
}

TEST_CASE("OpenAI errors are safe and actionable", "[agent][openai][errors]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));
    fake->error(429, "sensitive upstream body");
    const auto event = poll_until(agent, AgentEventKind::Failed);
    REQUIRE(event);
    REQUIRE(event->error);
    CHECK(event->error->code == "rate_limited");
    CHECK(event->error->retryable);
    CHECK(event->error->message.find("sensitive") == std::string::npos);

    REQUIRE(agent.start(request_fixture()));
    fake->data("data: not-json\n\n");
    const auto malformed = poll_until(agent, AgentEventKind::Failed);
    REQUIRE(malformed);
    CHECK(malformed->error->code == "malformed_response");

    REQUIRE(agent.start(request_fixture()));
    agent.cancel();
    CHECK(fake->cancelled);
    CHECK_FALSE(agent.busy());
}

TEST_CASE("OpenAI maps credential timeout and service errors", "[agent][openai][errors]")
{
    struct Case { unsigned status; const char* detail; const char* code; bool retryable; };
    const Case cases[] = {{401, "body omitted", "invalid_credentials", false},
                          {0, "Operation timed out", "timeout", true},
                          {503, "body omitted", "service_unavailable", true}};
    for (const Case& item : cases) {
        auto transport = std::make_unique<FakeTransport>();
        FakeTransport* fake = transport.get();
        OpenAIResponsesAgent agent({"key"}, std::move(transport));
        REQUIRE(agent.start(request_fixture()));
        fake->error(item.status, item.detail);
        const auto event = poll_until(agent, AgentEventKind::Failed);
        REQUIRE(event);
        REQUIRE(event->error);
        CHECK(event->error->code == item.code);
        CHECK(event->error->retryable == item.retryable);
    }
}

TEST_CASE("OpenAI returns malformed tool arguments to the model before native presentation", "[agent][openai][tools]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));
    const json call{{"type", "function_call"}, {"call_id", "call-bad"}, {"name", "plate_layout"},
                    {"arguments", json{{"objectId", 72}}.dump()}};
    // Twice the model is told and asked again; the third time ends the turn.
    for (std::size_t attempt = 1; attempt <= 2; ++attempt) {
        fake->data(sse(json{{"type", "response.completed"},
                            {"response", json{{"output", json::array({call})}}}}));
        CHECK_FALSE(poll_until(agent, AgentEventKind::ToolCall));
        REQUIRE(fake->requests.size() == attempt + 1);
        const json input = json::parse(fake->requests.back().body)["input"];
        CHECK(input.back()["type"] == "function_call_output");
        CHECK(input.back()["call_id"] == "call-bad");
        CHECK(json::parse(input.back()["output"].get<std::string>())["error"]["code"] == "invalid_arguments");
        CHECK(agent.busy());
    }
    fake->data(sse(json{{"type", "response.completed"},
                        {"response", json{{"output", json::array({call})}}}}));
    fake->complete();
    const auto event = poll_until(agent, AgentEventKind::Failed);
    REQUIRE(event);
    REQUIRE(event->error);
    CHECK(event->error->code == "malformed_tool_call");
    CHECK_FALSE(poll_until(agent, AgentEventKind::ToolCall));
}

TEST_CASE("OpenAI tells the model when it calls a tool that is not offered", "[agent][openai][tools]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    std::vector<std::string> refused;
    OpenAIResponsesConfig    config{"key"};
    config.refusal_listener = [&refused](const std::string& tool, const std::string& arguments, const std::string& code) {
        refused.push_back(tool + " " + arguments + " " + code);
    };
    OpenAIResponsesAgent agent(std::move(config), std::move(transport));
    REQUIRE(agent.start(request_fixture()));
    const json call{{"type", "function_call"}, {"call_id", "call-x"}, {"name", "support_enable"}, {"arguments", "{}"}};
    fake->data(sse(json{{"type", "response.completed"}, {"response", json{{"output", json::array({call})}}}}));
    CHECK_FALSE(poll_until(agent, AgentEventKind::ToolCall));
    CHECK_FALSE(poll_until(agent, AgentEventKind::Failed));
    REQUIRE(fake->requests.size() == 2);
    const json output = json::parse(json::parse(fake->requests.back().body)["input"].back()["output"].get<std::string>());
    CHECK(output["error"]["code"] == "unknown_tool");
    CHECK(output["error"]["message"].get<std::string>().find("support_enable") != std::string::npos);
    CHECK(refused == std::vector<std::string>{"support_enable {} unknown_tool"});
}

TEST_CASE("reported usage carries the cached share of the input", "[agent][openai][usage]")
{
    std::vector<AgentUsage> reported;
    OpenAIResponsesConfig   config{"key"};
    config.usage_listener = [&reported](const AgentUsage& usage) { reported.push_back(usage); };
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent(std::move(config), std::move(transport));
    REQUIRE(agent.start(request_fixture()));

    fake->data(sse(json{{"type", "response.completed"},
                        {"response", json{{"output", json::array()},
                                          {"usage", json{{"input_tokens", 4102},
                                                         {"input_tokens_details", json{{"cached_tokens", 3840}}},
                                                         {"output_tokens", 51},
                                                         {"total_tokens", 4153}}}}}}));
    fake->complete();
    REQUIRE(poll_until(agent, AgentEventKind::Completed));
    REQUIRE(reported.size() == 1);
    CHECK(reported[0].input == 4102);
    CHECK(reported[0].cached_input == 3840);
    CHECK(reported[0].output == 51);
    CHECK(reported[0].total == 4153);

    // A provider that reports no cache detail is zero cached, not a gap: the
    // deferral decision reads the log as a number either way.
    auto second = std::make_unique<FakeTransport>();
    FakeTransport* second_fake = second.get();
    OpenAIResponsesConfig plain{"key"};
    plain.usage_listener = [&reported](const AgentUsage& usage) { reported.push_back(usage); };
    OpenAIResponsesAgent bare(std::move(plain), std::move(second));
    REQUIRE(bare.start(request_fixture()));
    second_fake->data(sse(json{{"type", "response.completed"},
                               {"response", json{{"output", json::array()},
                                                 {"usage", json{{"input_tokens", 803}, {"output_tokens", 20},
                                                                {"total_tokens", 823}}}}}}));
    second_fake->complete();
    REQUIRE(poll_until(bare, AgentEventKind::Completed));
    REQUIRE(reported.size() == 2);
    CHECK(reported[1].input == 803);
    CHECK(reported[1].cached_input == 0);
}

TEST_CASE("the in-app catalog's per-turn size is recorded", "[agent][openai][budget]")
{
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* fake = transport.get();
    OpenAIResponsesAgent agent({"key"}, std::move(transport));
    REQUIRE(agent.start(request_fixture()));
    REQUIRE(fake->requests.size() == 1);

    // The number the deferral decision rests on, alongside the cached-token
    // share the live regression logs. Printed on every run; the bounds are an
    // alarm for runaway growth, not a target to grow into.
    const json  body  = json::parse(fake->requests.front().body);
    const auto  bytes = body["tools"].dump().size();
    WARN("in-app tool definitions: " << body["tools"].size() << ", " << bytes << " bytes");
    CHECK(body["tools"].size() <= 40);
    CHECK(bytes <= 32 * 1024);
}
