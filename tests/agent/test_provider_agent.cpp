// The provider-neutral turn loop, driven through a codec with no provider
// behind it: the body it builds is a picture of the transcript the loop hands
// it, and the payloads it parses are the test's own JSON. What the Responses
// API makes of that transcript is test_openai_responses_agent.cpp's.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ProviderAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <vector>

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

// Builds a JSON picture of the transcript; parses the test's scripted events:
// {"text": ...}, {"turn": <opaque>, "calls": [...], "usage": {...}},
// {"incomplete": true}, {"failed": {"code": ..., "retryable": ...}}.
class ScriptedCodec final : public IProviderCodec
{
public:
    ProviderRequest build(const TurnTranscript& transcript, const ProviderProfile& profile, RequestKind kind) const override
    {
        json tools = json::array();
        for (const OfferedTool& tool : transcript.tools)
            tools.push_back(json{{"name", tool.definition->name}, {"description", tool.description}});
        json opening = json::array();
        for (const AgentConversationContext& message : transcript.opening)
            opening.push_back(message.call_id.empty() ? json{{"role", message.role}, {"text", message.text}} :
                                                        json{{"call_id", message.call_id}, {"tool", message.tool}});
        json user = json::array();
        for (const TurnContentPart& part : transcript.user_content)
            user.push_back(json{{"kind", static_cast<int>(part.kind)}, {"text", part.text}, {"mime", part.mime},
                                {"filename", part.filename}, {"base64", part.base64}});
        json steps = json::array();
        for (const TurnStep& step : transcript.steps) {
            json results = json::array();
            for (const TurnStepResult& result : step.results)
                results.push_back(json{{"call_id", result.call_id}, {"output", json::parse(result.output_json)},
                                       {"image", result.image ? result.image->base64 : ""}, {"failed", result.failed}});
            steps.push_back(json{{"assistant_turn", step.assistant_turn}, {"results", std::move(results)}});
        }
        const auto skills = [](const std::vector<AgentSkillUse>& list) {
            json out = json::array();
            for (const AgentSkillUse& skill : list)
                out.push_back(json{{"name", skill.name}, {"text", skill.text}, {"picture", skill.picture != nullptr}});
            return out;
        };
        // The skills fired for this request; the record of every request's
        // stays in the transcript for a codec that keeps them in place.
        json once = skills(newest_once(transcript));
        json fired = json::array({skills(transcript.opening_once)});
        for (const TurnStep& step : transcript.steps)
            fired.push_back(skills(step.once));
        ProviderRequest request;
        request.body = json{{"kind", kind == RequestKind::ConversationTitle ? "title" : "reply"},
                            {"model", profile.model},
                            {"instructions", transcript.instructions},
                            {"tools", std::move(tools)},
                            {"opening", std::move(opening)},
                            {"user", std::move(user)},
                            {"steps", std::move(steps)},
                            {"once", std::move(once)},
                            {"fired", std::move(fired)},
                            {"request_id", transcript.request_id},
                            {"sequence", transcript.request_sequence}};
        request.headers.push_back({"X-Scripted-Sequence", std::to_string(transcript.request_sequence)});
        return request;
    }

    void reset() override { ++resets; }

    std::vector<StreamEvent> parse(const std::string& payload) override
    {
        const json script = json::parse(payload, nullptr, false);
        StreamEvent event;
        if (!script.is_object()) {
            event.kind  = StreamEvent::Kind::Failed;
            event.error = {"malformed_response", "unreadable", false};
        } else if (script.contains("text")) {
            event.kind = StreamEvent::Kind::TextDelta;
            event.text = script["text"].get<std::string>();
        } else if (script.contains("turn")) {
            event.kind           = StreamEvent::Kind::TurnComplete;
            event.assistant_turn = script["turn"];
            event.resend         = script.value("resend", false);
            for (const json& call : script.value("calls", json::array()))
                event.calls.push_back({call.value("call_id", ""), call.value("name", ""),
                                       call["arguments"].is_string() ? call["arguments"].get<std::string>() : call["arguments"].dump()});
            if (script.contains("usage")) {
                AgentUsage usage;
                usage.input        = script["usage"].value("input", 0);
                usage.cached_input = script["usage"].value("cached", 0);
                usage.output       = script["usage"].value("output", 0);
                usage.total        = script["usage"].value("total", 0);
                event.usage        = usage;
            }
        } else if (script.contains("incomplete")) {
            event.kind = StreamEvent::Kind::Incomplete;
        } else {
            event.kind  = StreamEvent::Kind::Failed;
            event.error = {script["failed"].value("code", "scripted_failure"), "The script said so.",
                           script["failed"].value("retryable", false)};
        }
        return {event};
    }

    int resets{0};
};

std::string frame(const json& script) { return "data: " + script.dump() + "\n\n"; }

json call(const std::string& call_id, const std::string& name, const json& arguments = json::object())
{
    return json{{"call_id", call_id}, {"name", name}, {"arguments", arguments}};
}

ProviderProfile test_profile()
{
    return ProviderProfile{"test", ProviderFamily::Responses, "https://provider.test/v1/turns", AuthStyle::Bearer, "model-x", "Test Provider"};
}

struct Harness
{
    explicit Harness(ProviderAgentConfig config = {})
    {
        if (config.profile.id.empty())
            config.profile = test_profile();
        if (config.api_key.empty())
            config.api_key = "secret-key";
        auto transport = std::make_unique<FakeTransport>();
        auto codec     = std::make_unique<ScriptedCodec>();
        fake           = transport.get();
        scripted       = codec.get();
        agent          = std::make_unique<ProviderAgent>(std::move(config), std::move(codec), std::move(transport));
    }

    json body(std::size_t index) const { return json::parse(fake->requests.at(index).body); }
    json last_body() const { return json::parse(fake->requests.back().body); }
    json last_results() const { return last_body()["steps"].back()["results"]; }

    std::optional<AgentEvent> poll_until(AgentEventKind kind)
    {
        for (int i = 0; i < 20; ++i) {
            auto event = agent->poll();
            if (event && event->kind == kind)
                return event;
        }
        return std::nullopt;
    }

    FakeTransport*                 fake{nullptr};
    ScriptedCodec*                 scripted{nullptr};
    std::unique_ptr<ProviderAgent> agent;
};

// The project chat's turn: its page's instructions, the workspace, one cube.
AgentRequest request_fixture()
{
    AgentRequest request;
    request.request_id           = "assistant-7-attempt-1";
    request.session.instructions = "You are the project assistant.";
    request.user_text            = "Duplicate the selected cube.";
    request.workspace.session    = Workspace::ProjectSessionId(41);
    Workspace::WorkspaceObject object;
    object.id   = Workspace::ObjectId(request.workspace.session, 72);
    object.name = "cube";
    Workspace::WorkspacePlate plate;
    plate.id     = Workspace::PlateId(request.workspace.session, 3);
    plate.name   = "Plate 1";
    plate.active = true;
    plate.objects.push_back(object);
    request.workspace.plates.push_back(plate);
    request.conversation.push_back({"assistant", "Earlier answer"});
    return request;
}

AgentSkillUse file_loaded_skill(const std::string& text = "A file was loaded.", bool picture = true)
{
    return AgentSkillUse{"file-loaded", text,
                         picture ? std::make_shared<const ToolImage>(ToolImage{"image/png", "iVBORw0KGgo=", 2, 1}) : nullptr,
                         "file_loaded"};
}

// As AgentHost fires it: it gives way to the file-loaded skill.
AgentSkillUse setup_skill()
{
    return AgentSkillUse{"project-setup-changed", "Check the setup.", nullptr, "setup_changed", {"file_loaded"}};
}

const json kLayoutArguments{{"sessionId", "41"}, {"objects", json::array({json{{"objectId", "72"}, {"quantity", 2}}})}};

} // namespace

TEST_CASE("the loop hands the codec the turn's transcript and sends what it builds", "[agent][loop]")
{
    Harness h;
    AgentRequest request = request_fixture();
    request.attachments.push_back({"att-1", "notes.txt", "text", "text/plain", "", "do not scale", "", false});
    request.attachments.push_back({"img-1", "photo.png", "image", "image/png", "", "", "\x89PNG", false});
    request.attachments.push_back({"pdf-1", "spec.pdf", "pdf", "", "", "", "%PDF", false});
    request.attachments.push_back({"stl-1", "part.stl", "model", "model/stl", "", "", "solid", true});
    REQUIRE(h.agent->start(request));
    REQUIRE(h.fake->requests.size() == 1);
    const AgentHttpRequest& wire = h.fake->requests.front();
    CHECK(wire.url == "https://provider.test/v1/turns");
    // The credential as the profile's auth style says, then the codec's own
    // headers; the body never carries the key.
    REQUIRE(wire.headers.size() == 2);
    CHECK(wire.headers[0].name == "Authorization");
    CHECK(wire.headers[0].value == "Bearer secret-key");
    CHECK(wire.header("X-Scripted-Sequence") == "1");
    CHECK(wire.body.find("secret-key") == std::string::npos);
    CHECK(h.scripted->resets == 1);

    const json body = h.body(0);
    CHECK(body["kind"] == "reply");
    CHECK(body["model"] == "model-x");
    CHECK(body["instructions"] == "You are the project assistant.");
    CHECK(body["request_id"] == "assistant-7-attempt-1");
    CHECK(body["sequence"] == 1);
    CHECK(body["opening"] == json::array({json{{"role", "assistant"}, {"text", "Earlier answer"}}}));
    CHECK(body["steps"].empty());
    CHECK(body["once"].empty());
    // Every in-app tool the registry offers with an importable attachment,
    // in the registry's order, with the registry's descriptions.
    std::vector<std::string> names;
    for (const json& tool : body["tools"]) {
        names.push_back(tool["name"].get<std::string>());
        CHECK(tool["description"] == ToolRegistry::instance().find(tool["name"].get<std::string>())->description);
    }
    CHECK(names == std::vector<std::string>{"activity_cancel", "calibration_generate", "export_file", "object_analyze", "object_divide", "object_divide_preview", "object_import", "object_merge", "object_place", "object_repair", "plate_layout", "presets_list", "printer_list", "printer_setup", "printer_setup_preview", "project_attachment_read", "project_delete_items", "project_open", "region_annotate", "settings_apply_patch", "settings_get", "settings_preview_patch", "settings_search", "setup_record", "slice_inspect", "slice_report", "slice_start", "view_render", "workspace_inspect"});
    // The person's words, the workspace in its tag, the attachment listing,
    // then the image and the PDF as parts of their own; the model file has
    // no part, it is listed only.
    const json& user = body["user"];
    REQUIRE(user.size() == 5);
    CHECK(user[0]["kind"] == static_cast<int>(TurnContentPart::Kind::Text));
    CHECK(user[0]["text"] == "Duplicate the selected cube.");
    CHECK(user[1]["text"].get<std::string>().rfind("<workspace>\n{", 0) == 0);
    CHECK(user[2]["text"].get<std::string>().find("Attachment att-1: notes.txt [text]\n<attachment id=\"att-1\">\ndo not scale\n</attachment>") != std::string::npos);
    CHECK(user[2]["text"].get<std::string>().find("Attachment stl-1: part.stl [model]") != std::string::npos);
    CHECK(user[3]["kind"] == static_cast<int>(TurnContentPart::Kind::Image));
    CHECK(user[3]["mime"] == "image/png");
    CHECK(user[3]["base64"] == "iVBORw==");
    CHECK(user[4]["kind"] == static_cast<int>(TurnContentPart::Kind::Pdf));
    CHECK(user[4]["mime"] == "application/octet-stream");
    CHECK(user[4]["filename"] == "spec.pdf");

    SECTION("a title request offers no tools and says nothing new")
    {
        h.agent->cancel();
        request.purpose = AgentRequest::Purpose::ConversationTitle;
        request.skills  = {file_loaded_skill()};
        REQUIRE(h.agent->start(request));
        const json title = h.last_body();
        CHECK(title["kind"] == "title");
        CHECK(title["tools"].empty());
        CHECK(title["user"].empty());
        CHECK(title["once"].empty());
        CHECK(title["opening"].size() == 1);
    }

    SECTION("a turn the app started with nothing to say has no user message")
    {
        h.agent->cancel();
        request.user_text.clear();
        request.attachments.clear();
        request.session.include_workspace = false;
        REQUIRE(h.agent->start(request));
        CHECK(h.last_body()["user"].empty());
        CHECK(h.last_body()["tools"].size() == 28);
    }

    SECTION("the request sequence counts every request of the turn")
    {
        h.fake->data(frame(json{{"turn", "step-1"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
        REQUIRE(h.poll_until(AgentEventKind::ToolCall));
        REQUIRE(h.agent->continue_after_tool({"call-1", "succeeded", R"({"state":"succeeded"})"}));
        REQUIRE(h.fake->requests.size() == 2);
        CHECK(h.fake->requests.back().header("X-Scripted-Sequence") == "2");
        CHECK(h.last_body()["sequence"] == 2);
        CHECK(h.scripted->resets == 2);
    }
}

TEST_CASE("text deltas and the completion become typed events, across split frames", "[agent][loop]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    const std::string first = frame(json{{"text", "Done"}});
    h.fake->data(first.substr(0, 9));
    CHECK_FALSE(h.agent->poll().has_value());
    h.fake->data(first.substr(9) + frame(json{{"turn", json::array()}}));
    h.fake->complete();
    const auto delta = h.poll_until(AgentEventKind::TextDelta);
    REQUIRE(delta);
    CHECK(delta->text == "Done");
    REQUIRE(h.poll_until(AgentEventKind::Completed));
    CHECK_FALSE(h.agent->busy());
    CHECK(h.last_body()["steps"].empty()); // the turn ended; no request carried its reply
}

TEST_CASE("a final SSE frame without a trailing delimiter is consumed", "[agent][loop]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data("event: response.completed\r\ndata: " + json{{"turn", json::array()}}.dump());
    h.fake->complete();
    REQUIRE(h.poll_until(AgentEventKind::Completed));
    CHECK_FALSE(h.agent->busy());

    // Ending with no terminal event at all is a failure the person can retry.
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"text", "half"}}));
    h.fake->complete();
    const auto failed = h.poll_until(AgentEventKind::Failed);
    REQUIRE(failed);
    REQUIRE(failed->error);
    CHECK(failed->error->code == "malformed_response");
    CHECK(failed->error->retryable);
}

TEST_CASE("every call of a response runs in order and their results go back together, after the opaque turn",
          "[agent][loop][tools]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));

    // A read and the change that does not wait for it, in one response. The
    // turn is whatever the codec kept: the loop sends it back untouched.
    const json turn = json{{"opaque", json::array({"reasoning", "call", "call"})}};
    h.fake->data(frame(json{{"turn", turn},
                            {"calls", json::array({call("call-read", "workspace_inspect", json{{"sections", json::array({"summary"})}}),
                                                   call("call-intent", "setup_record",
                                                        json{{"fields", json::array({json{{"field", "material"}, {"value", "PETG"}}})},
                                                             {"purpose", "Strong PETG bracket"}})})}}));
    h.fake->complete();

    const auto first = h.poll_until(AgentEventKind::ToolCall);
    REQUIRE(first);
    CHECK(first->tool->call_id == "call-read");
    CHECK(first->tool->request.tool == "workspace_inspect");
    CHECK(first->tool->await_result);
    // The second call waits for the first's result, and nothing is sent yet.
    CHECK_FALSE(h.agent->poll());
    REQUIRE(h.agent->continue_after_tool({"call-read", "succeeded", R"({"state":"succeeded","result":{"summary":{}}})"}));
    CHECK(h.fake->requests.size() == 1);
    const auto second = h.poll_until(AgentEventKind::ToolCall);
    REQUIRE(second);
    CHECK(second->tool->call_id == "call-intent");
    CHECK(second->tool->request.tool == "setup_record");
    CHECK(h.agent->busy());

    REQUIRE(h.agent->continue_after_tool({"call-intent", "succeeded", R"({"state":"succeeded"})"}));
    REQUIRE(h.fake->requests.size() == 2);
    const json body = h.last_body();
    // The opening and the user message are the first request's, exactly.
    CHECK(body["opening"] == h.body(0)["opening"]);
    CHECK(body["user"] == h.body(0)["user"]);
    CHECK(body["instructions"] == h.body(0)["instructions"]);
    REQUIRE(body["steps"].size() == 1);
    CHECK(body["steps"][0]["assistant_turn"] == turn);
    CHECK(body["steps"][0]["results"] ==
          json::array({json{{"call_id", "call-read"}, {"output", json{{"state", "succeeded"}, {"result", json{{"summary", json::object()}}}}}, {"image", ""}, {"failed", false}},
                       json{{"call_id", "call-intent"}, {"output", json{{"state", "succeeded"}}}, {"image", ""}, {"failed", false}}}));

    // A second response, another call: the history keeps every step.
    h.fake->data(frame(json{{"turn", "step-2"}, {"calls", json::array({call("call-3", "settings_get", json{{"scope", "process"}, {"keys", json::array({"wall_loops"})}}) })}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-3", "succeeded", R"({"items":[{"key":"wall_loops","value":"2"}]})"}));
    REQUIRE(h.fake->requests.size() == 3);
    const json third = h.last_body();
    REQUIRE(third["steps"].size() == 2);
    CHECK(third["steps"][0] == body["steps"][0]);
    CHECK(third["steps"][1]["assistant_turn"] == "step-2");
    CHECK(third["steps"][1]["results"][0]["call_id"] == "call-3");

    h.fake->data(frame(json{{"turn", json::array()}}));
    h.fake->complete();
    REQUIRE(h.poll_until(AgentEventKind::Completed));
    CHECK_FALSE(h.agent->busy());
}

TEST_CASE("a call that does not succeed answers the calls after it unrun", "[agent][loop][tools]")
{
    const std::string state = GENERATE("failed", "cancelled");
    CAPTURE(state);
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"},
                            {"calls", json::array({call("call-1", "workspace_inspect"), call("call-2", "plate_layout", kLayoutArguments),
                                                   call("call-3", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    const json failed_output{{"state", state}, {"error", {{"code", "stale_workspace"}}}};
    REQUIRE(h.agent->continue_after_tool({"call-1", state, failed_output.dump()}));

    // Neither later call reaches the app; all three results go back at once.
    CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.fake->requests.size() == 2);
    const json results = h.last_results();
    REQUIRE(results.size() == 3);
    CHECK(results[0]["call_id"] == "call-1");
    CHECK(results[0]["output"] == failed_output);
    for (std::size_t i : {1u, 2u}) {
        CHECK(results[i]["call_id"] == (i == 1 ? "call-2" : "call-3"));
        CHECK(results[i]["output"]["state"] == "failed");
        CHECK(results[i]["output"]["error"]["code"] == "not_run");
        CHECK(results[i]["output"]["error"]["message"].get<std::string>().find("workspace_inspect (call-1)") != std::string::npos);
    }
    CHECK(h.agent->busy());
}

TEST_CASE("a refused call in a response answers the calls after it unrun", "[agent][loop][tools]")
{
    std::vector<std::string> refused;
    ProviderAgentConfig      config;
    config.refusal_listener = [&refused](const std::string& tool, const std::string&, const std::string& code) {
        refused.push_back(tool + " " + code);
    };
    Harness h(std::move(config));
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"},
                            {"calls", json::array({call("call-a", "workspace_inspect"), call("call-b", "support_enable"),
                                                   call("call-c", "plate_layout", json{{"objectId", 72}})})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-a", "succeeded", R"({"state":"succeeded"})"}));
    CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
    CHECK_FALSE(h.poll_until(AgentEventKind::Failed));

    REQUIRE(h.fake->requests.size() == 2);
    const json results = h.last_results();
    REQUIRE(results.size() == 3);
    CHECK(results[0]["call_id"] == "call-a");
    CHECK(results[1]["call_id"] == "call-b");
    CHECK(results[1]["output"]["error"]["code"] == "unknown_tool");
    // The call after the refusal is not checked, so it is not refused too.
    CHECK(results[2]["call_id"] == "call-c");
    CHECK(results[2]["output"]["error"]["code"] == "not_run");
    CHECK(results[2]["output"]["error"]["message"].get<std::string>().find("support_enable (call-b)") != std::string::npos);
    CHECK(refused == std::vector<std::string>{"support_enable unknown_tool"});
}

TEST_CASE("a late completion from the tool-call request cannot end its continuation", "[agent][loop][tools]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"}, {"calls", json::array({call("call-9", "plate_layout", kLayoutArguments)})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-9", "succeeded", R"({"state":"succeeded"})"}));

    // The first request's libcurl completion can race with page handling and
    // arrive after the continuation has already started.
    h.fake->callbacks.front()(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, 200});
    CHECK_FALSE(h.agent->poll());
    CHECK(h.agent->busy());

    h.fake->data(frame(json{{"turn", json::array()}}));
    h.fake->complete();
    REQUIRE(h.poll_until(AgentEventKind::Completed));
    CHECK_FALSE(h.agent->busy());
}

TEST_CASE("HTTP failures are mapped to safe errors that name the provider", "[agent][loop][errors]")
{
    struct Case { unsigned status; const char* detail; const char* code; bool retryable; const char* mention; };
    const Case cases[] = {{401, "body omitted", "invalid_credentials", false, "The Test Provider API key was rejected"},
                          {429, "sensitive upstream body", "rate_limited", true, "The Test Provider service is rate limited"},
                          {0, "Operation timed out", "timeout", true, "timed out"},
                          {503, "body omitted", "service_unavailable", true, "The Test Provider service is temporarily unavailable"},
                          {0, "could not resolve host", "network_error", true, "could not reach the Test Provider service"},
                          {413, "body omitted", "request_too_large", false, "too large for the Test Provider service"},
                          {529, "body omitted", "service_overloaded", true, "The Test Provider service is overloaded"}};
    for (const Case& item : cases) {
        CAPTURE(item.status, item.detail);
        Harness h;
        REQUIRE(h.agent->start(request_fixture()));
        h.fake->error(item.status, item.detail);
        const auto event = h.poll_until(AgentEventKind::Failed);
        REQUIRE(event);
        REQUIRE(event->error);
        CHECK(event->error->code == item.code);
        CHECK(event->error->retryable == item.retryable);
        CHECK(event->error->message.find(item.mention) != std::string::npos);
        CHECK(event->error->message.find("sensitive") == std::string::npos);
        CHECK_FALSE(h.agent->busy());
    }
}

TEST_CASE("what the codec reports ends the turn as it says", "[agent][loop][errors]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"incomplete", true}}));
    auto event = h.poll_until(AgentEventKind::Failed);
    REQUIRE(event);
    CHECK(event->error->code == "incomplete_response");
    CHECK(event->error->retryable);

    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"failed", json{{"code", "refusal"}, {"retryable", false}}}}));
    event = h.poll_until(AgentEventKind::Failed);
    REQUIRE(event);
    CHECK(event->error->code == "refusal");
    CHECK_FALSE(event->error->retryable);

    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data("data: not-json\n\n");
    event = h.poll_until(AgentEventKind::Failed);
    REQUIRE(event);
    CHECK(event->error->code == "malformed_response");

    // Cancelling drops whatever the cancelled request still delivers.
    REQUIRE(h.agent->start(request_fixture()));
    h.agent->cancel();
    CHECK(h.fake->cancelled);
    CHECK_FALSE(h.agent->busy());
    h.fake->data(frame(json{{"text", "late"}}) + frame(json{{"turn", json::array()}}));
    h.fake->complete();
    CHECK_FALSE(h.agent->poll());
}

TEST_CASE("malformed tool arguments go back to the model twice, then end the turn", "[agent][loop][tools]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    const json bad = json{{"turn", "t"}, {"calls", json::array({call("call-bad", "plate_layout", json{{"objectId", 72}})})}};
    // Twice the model is told and asked again; the third time ends the turn.
    for (std::size_t attempt = 1; attempt <= 2; ++attempt) {
        h.fake->data(frame(bad));
        CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
        REQUIRE(h.fake->requests.size() == attempt + 1);
        const json results = h.last_results();
        REQUIRE(results.size() == 1);
        CHECK(results[0]["call_id"] == "call-bad");
        CHECK(results[0]["output"]["error"]["code"] == "invalid_arguments");
        CHECK(h.agent->busy());
    }
    h.fake->data(frame(bad));
    h.fake->complete();
    const auto event = h.poll_until(AgentEventKind::Failed);
    REQUIRE(event);
    REQUIRE(event->error);
    CHECK(event->error->code == "malformed_tool_call");
    CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
}

TEST_CASE("a call to a tool that is not offered is refused back to the model", "[agent][loop][tools]")
{
    std::vector<std::string> refused;
    ProviderAgentConfig      config;
    config.refusal_listener = [&refused](const std::string& tool, const std::string& arguments, const std::string& code) {
        refused.push_back(tool + " " + arguments + " " + code);
    };
    Harness h(std::move(config));
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"}, {"calls", json::array({call("call-x", "support_enable")})}}));
    CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
    CHECK_FALSE(h.poll_until(AgentEventKind::Failed));
    REQUIRE(h.fake->requests.size() == 2);
    const json output = h.last_results().back()["output"];
    CHECK(output["error"]["code"] == "unknown_tool");
    CHECK(output["error"]["message"].get<std::string>().find("support_enable") != std::string::npos);
    CHECK(refused == std::vector<std::string>{"support_enable {} unknown_tool"});

    // A tool the turn did not offer is one the model cannot call, even one
    // the registry knows: skill_read with nothing listed.
    REQUIRE(h.agent->continue_after_tool({"", "", ""}) == false);
    h.fake->data(frame(json{{"turn", "t"}, {"calls", json::array({call("call-skill", "skill_read", json{{"name", "x"}})})}}));
    CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.fake->requests.size() == 3);
    CHECK(h.last_results().back()["output"]["error"]["code"] == "unknown_tool");
}

TEST_CASE("a call's image stays with its result, and usage reaches the listener", "[agent][loop][image][usage]")
{
    std::vector<AgentUsage> reported;
    ProviderAgentConfig     config;
    config.usage_listener = [&reported](const AgentUsage& usage) { reported.push_back(usage); };
    Harness h(std::move(config));
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"},
                            {"calls", json::array({call("call-img", "workspace_inspect"), call("call-text", "workspace_inspect")})},
                            {"usage", json{{"input", 4102}, {"cached", 3840}, {"output", 51}, {"total", 4153}}}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    AgentToolResult result{"call-img", "succeeded", R"({"state":"succeeded"})"};
    result.image = std::make_shared<ToolImage>(ToolImage{"image/png", "iVBORw0KGgo=", 2, 1});
    REQUIRE(h.agent->continue_after_tool(result));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-text", "succeeded", R"({"state":"succeeded"})"}));
    const json results = h.last_results();
    REQUIRE(results.size() == 2);
    CHECK(results[0]["call_id"] == "call-img");
    CHECK(results[0]["image"] == "iVBORw0KGgo=");
    CHECK(results[1]["call_id"] == "call-text");
    CHECK(results[1]["image"] == "");

    REQUIRE(reported.size() == 1);
    CHECK(reported[0].input == 4102);
    CHECK(reported[0].cached_input == 3840);
    CHECK(reported[0].output == 51);
    CHECK(reported[0].total == 4153);
    // A response without a usage report is not a zero report.
    h.fake->data(frame(json{{"turn", json::array()}}));
    REQUIRE(h.poll_until(AgentEventKind::Completed));
    CHECK(reported.size() == 1);
}

TEST_CASE("a skill fired for a turn's first request rides on that request and no other", "[agent][loop][skills]")
{
    Harness h;
    AgentRequest request = request_fixture();
    request.skills.push_back(file_loaded_skill());
    REQUIRE(h.agent->start(request));
    CHECK(h.last_body()["once"] == json::array({json{{"name", "file-loaded"}, {"text", "A file was loaded."}, {"picture", true}}}));

    // The response calls a tool; the request that carries its result is the
    // turn's history without the skill.
    h.fake->data(frame(json{{"turn", "t"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-1", "succeeded", R"({"state":"succeeded"})"}));
    CHECK(h.last_body()["once"].empty());
    CHECK(h.last_body()["steps"].size() == 1);
}

TEST_CASE("a skill a tool result fires follows every result of its response, on that request only", "[agent][loop][skills]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"},
                            {"calls", json::array({call("call-import", "workspace_inspect"), call("call-again", "workspace_inspect"),
                                                   call("call-read", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    AgentToolResult imported{"call-import", "succeeded", R"({"state":"succeeded"})"};
    imported.skills.push_back(file_loaded_skill("The first import."));
    REQUIRE(h.agent->continue_after_tool(imported));
    // Nothing is sent until the last result is in.
    CHECK(h.fake->requests.size() == 1);
    // A second import in the same response: one skill, the later one.
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    AgentToolResult imported_again{"call-again", "succeeded", R"({"state":"succeeded"})"};
    imported_again.skills.push_back(file_loaded_skill("The second import."));
    REQUIRE(h.agent->continue_after_tool(imported_again));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-read", "succeeded", R"({"state":"succeeded"})"}));

    const json body = h.last_body();
    REQUIRE(body["steps"].size() == 1);
    REQUIRE(body["steps"][0]["results"].size() == 3);
    CHECK(body["steps"][0]["results"][2]["call_id"] == "call-read");
    CHECK(body["once"] == json::array({json{{"name", "file-loaded"}, {"text", "The second import."}, {"picture", true}}}));

    // The next request of the same turn has the results and no skill.
    h.fake->data(frame(json{{"turn", "t2"}, {"calls", json::array({call("call-4", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-4", "succeeded", R"({"state":"succeeded"})"}));
    const json later = h.last_body();
    CHECK(later["once"].empty());
    REQUIRE(later["steps"].size() == 2);
    CHECK(later["steps"][0]["results"][0]["call_id"] == "call-import");
    CHECK(later["steps"][1]["results"][0]["call_id"] == "call-4");
}

TEST_CASE("a skill that yields to another on the same request is left out of it", "[agent][loop][skills]")
{
    SECTION("a turn's first request")
    {
        Harness h;
        AgentRequest request = request_fixture();
        request.skills = {setup_skill()};
        REQUIRE(h.agent->start(request));
        CHECK(h.last_body()["once"] == json::array({json{{"name", "project-setup-changed"}, {"text", "Check the setup."}, {"picture", false}}}));
        h.agent->cancel();
        request.skills = {file_loaded_skill(), setup_skill()};
        REQUIRE(h.agent->start(request));
        CHECK(h.last_body()["once"] == json::array({json{{"name", "file-loaded"}, {"text", "A file was loaded."}, {"picture", true}}}));
    }
    SECTION("the results of one response")
    {
        Harness h;
        REQUIRE(h.agent->start(request_fixture()));
        h.fake->data(frame(json{{"turn", "t"},
                                {"calls", json::array({call("call-setup", "workspace_inspect"), call("call-import", "workspace_inspect")})}}));
        REQUIRE(h.poll_until(AgentEventKind::ToolCall));
        AgentToolResult changed{"call-setup", "succeeded", R"({"state":"succeeded"})"};
        changed.skills.push_back(setup_skill());
        REQUIRE(h.agent->continue_after_tool(changed));
        REQUIRE(h.poll_until(AgentEventKind::ToolCall));
        AgentToolResult imported{"call-import", "succeeded", R"({"state":"succeeded"})"};
        imported.skills.push_back(file_loaded_skill());
        REQUIRE(h.agent->continue_after_tool(imported));
        CHECK(h.last_body()["once"] == json::array({json{{"name", "file-loaded"}, {"text", "A file was loaded."}, {"picture", true}}}));
    }
}

TEST_CASE("skill_read is offered only with skills to list, and its description carries the list", "[agent][loop][skills]")
{
    Harness h;
    AgentRequest request = request_fixture();
    REQUIRE(h.agent->start(request));
    const json without = h.last_body()["tools"];
    const auto find = [](const json& tools, const std::string& name) {
        return std::find_if(tools.begin(), tools.end(), [&name](const json& tool) { return tool["name"] == name; });
    };
    CHECK(find(without, "skill_read") == without.end());
    h.agent->cancel();

    request.skill_index = {AgentSkill{"tune-strength", {}, "When a part must carry load.", {}},
                           AgentSkill{"wet-filament", {}, "When stringing or popping suggests moisture.", {}}};
    REQUIRE(h.agent->start(request));
    const json with       = h.last_body()["tools"];
    const auto skill_read = find(with, "skill_read");
    REQUIRE(skill_read != with.end());
    const std::string description = (*skill_read)["description"];
    CHECK(description.rfind(ToolRegistry::instance().find("skill_read")->description, 0) == 0);
    CHECK(description.find("\n\nSkills:\n- tune-strength: When a part must carry load.\n"
                           "- wet-filament: When stringing or popping suggests moisture.") != std::string::npos);
    CHECK(with.size() == without.size() + 1);
}

TEST_CASE("the loop is not ready without a key, a model, an endpoint, a codec or a transport", "[agent][loop]")
{
    ProviderAgentConfig config;
    config.profile = test_profile();
    config.api_key = "k";
    CHECK(ProviderAgent(config, std::make_unique<ScriptedCodec>(), std::make_unique<FakeTransport>()).ready());
    CHECK_FALSE(ProviderAgent(config, nullptr, std::make_unique<FakeTransport>()).ready());
    CHECK_FALSE(ProviderAgent(config, std::make_unique<ScriptedCodec>(), nullptr).ready());
    ProviderAgentConfig no_key = config;
    no_key.api_key.clear();
    CHECK_FALSE(ProviderAgent(no_key, std::make_unique<ScriptedCodec>(), std::make_unique<FakeTransport>()).ready());
    ProviderAgentConfig no_model = config;
    no_model.profile.model.clear();
    CHECK_FALSE(ProviderAgent(no_model, std::make_unique<ScriptedCodec>(), std::make_unique<FakeTransport>()).ready());
    ProviderAgentConfig no_endpoint = config;
    no_endpoint.profile.endpoint.clear();
    CHECK_FALSE(ProviderAgent(no_endpoint, std::make_unique<ScriptedCodec>(), std::make_unique<FakeTransport>()).ready());

    // A transport that refuses the post leaves the loop idle.
    auto transport = std::make_unique<FakeTransport>();
    transport->accept_posts = false;
    ProviderAgent agent(config, std::make_unique<ScriptedCodec>(), std::move(transport));
    CHECK_FALSE(agent.start(request_fixture()));
    CHECK_FALSE(agent.busy());
}

TEST_CASE("every request's skills stay recorded where they were sent, and only the newest are this request's", "[agent][loop][skills]")
{
    Harness h;
    AgentRequest request = request_fixture();
    request.skills.push_back(file_loaded_skill("The turn's own."));
    REQUIRE(h.agent->start(request));
    h.fake->data(frame(json{{"turn", "t1"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    AgentToolResult result{"call-1", "succeeded", R"({"state":"succeeded"})"};
    result.skills.push_back(setup_skill());
    REQUIRE(h.agent->continue_after_tool(result));
    h.fake->data(frame(json{{"turn", "t2"}, {"calls", json::array({call("call-2", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-2", "failed", R"({"state":"failed"})"}));

    const json body = h.last_body();
    // This request fired nothing new, but the record of the first two stays.
    CHECK(body["once"].empty());
    REQUIRE(body["fired"].size() == 3);
    CHECK(body["fired"][0][0]["text"] == "The turn's own.");
    CHECK(body["fired"][1][0]["text"] == "Check the setup.");
    CHECK(body["fired"][2].empty());
    // A result that did not succeed is marked so.
    CHECK(body["steps"][0]["results"][0]["failed"] == false);
    CHECK(body["steps"][1]["results"][0]["failed"] == true);
}

TEST_CASE("calls answered without running are marked failed", "[agent][loop][tools]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"turn", "t"}, {"calls", json::array({call("call-a", "support_enable"), call("call-b", "workspace_inspect")})}}));
    CHECK_FALSE(h.poll_until(AgentEventKind::ToolCall));
    const json results = h.last_results();
    REQUIRE(results.size() == 2);
    CHECK(results[0]["failed"] == true); // refused
    CHECK(results[1]["failed"] == true); // not run
}

TEST_CASE("a paused response is sent again with its turn appended, a bounded number of times", "[agent][loop]")
{
    Harness h;
    REQUIRE(h.agent->start(request_fixture()));
    h.fake->data(frame(json{{"text", "Part one."}}) + frame(json{{"turn", "paused-1"}, {"resend", true}}));
    CHECK(h.poll_until(AgentEventKind::TextDelta));
    CHECK_FALSE(h.agent->poll());
    REQUIRE(h.fake->requests.size() == 2);
    const json body = h.last_body();
    REQUIRE(body["steps"].size() == 1);
    CHECK(body["steps"][0]["assistant_turn"] == "paused-1");
    CHECK(body["steps"][0]["results"].empty());
    CHECK(h.agent->busy());

    // The resumed response ends the turn as any other.
    h.fake->data(frame(json{{"turn", "done"}}));
    REQUIRE(h.poll_until(AgentEventKind::Completed));

    // A provider that never stops pausing ends the turn after four resends.
    REQUIRE(h.agent->start(request_fixture()));
    const std::size_t before = h.fake->requests.size();
    for (int i = 0; i < 4; ++i) {
        h.fake->data(frame(json{{"turn", "again"}, {"resend", true}}));
        CHECK_FALSE(h.agent->poll());
    }
    CHECK(h.fake->requests.size() == before + 4);
    h.fake->data(frame(json{{"turn", "again"}, {"resend", true}}));
    const auto failed = h.poll_until(AgentEventKind::Failed);
    REQUIRE(failed);
    CHECK(failed->error->code == "agent_paused_too_often");
    CHECK(h.fake->requests.size() == before + 4);
}
