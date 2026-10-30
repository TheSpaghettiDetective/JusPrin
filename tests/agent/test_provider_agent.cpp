// The provider-neutral turn loop, driven through a codec with no provider
// behind it: the body it builds is a picture of the transcript the loop hands
// it, and the payloads it parses are the test's own JSON. What the Responses
// API makes of that transcript is test_openai_responses_agent.cpp's.

#include <catch2/catch_all.hpp>

#include "kept_turn.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "project_chat_tools.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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

json picture(const RecordPart& part)
{
    return json{{"kind", static_cast<int>(part.kind)}, {"text", part.text}, {"mime", part.mime},
                {"filename", part.filename}, {"base64", part.base64}};
}

// One item of the transcript as the tests read it.
json picture(const RecordItem& item)
{
    static const char* const kKinds[] = {"user", "operator", "assistant", "results"};
    json parts = json::array();
    for (const RecordPart& part : item.parts)
        parts.push_back(picture(part));
    json results = json::array();
    for (const RecordResult& result : item.results)
        results.push_back(json{{"call_id", result.call_id}, {"output", json::parse(result.output_json)},
                               {"image", result.image ? result.image->base64 : ""}, {"failed", result.failed}});
    return json{{"kind", kKinds[static_cast<int>(item.kind)]}, {"source", item.source}, {"parts", std::move(parts)},
                {"text", item.text}, {"output", item.output}, {"results", std::move(results)}};
}

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
        json items = json::array();
        for (const RecordItem& item : transcript.items)
            items.push_back(picture(item));
        ProviderRequest request;
        request.body = json{{"kind", kind == RequestKind::ConversationTitle ? "title" : "reply"},
                            {"model", profile.model},
                            {"instructions", transcript.instructions},
                            {"tools", std::move(tools)},
                            {"items", std::move(items)},
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
    // The items of a request of the given kind, in order.
    static json of_kind(const json& body, const std::string& kind)
    {
        json found = json::array();
        for (const json& item : body["items"])
            if (item["kind"] == kind)
                found.push_back(item);
        return found;
    }
    json last_results() const { return of_kind(last_body(), "results").back()["results"]; }

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
    JusPrinTest::kept_nowhere(request);
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
    // What the chat sent before: one reply, and the message this turn answers.
    request.record.format = "responses";
    request.record.items.push_back(RecordItem{RecordItem::Kind::Assistant, {}, {}, {}, json::array({"Earlier answer"})});
    request.source = "m-2";
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

std::vector<std::string> operator_texts(const json& body)
{
    std::vector<std::string> texts;
    for (const json& item : body["items"])
        if (item["kind"] == "operator")
            texts.push_back(item["text"]);
    return texts;
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
    // The record first, then this turn's message, nothing else.
    REQUIRE(body["items"].size() == 2);
    CHECK(body["items"][0]["kind"] == "assistant");
    CHECK(body["items"][0]["output"] == json::array({"Earlier answer"}));
    CHECK(body["items"][1]["kind"] == "user");
    CHECK(body["items"][1]["source"] == "m-2");
    // Every in-app tool the registry offers, in the registry's order, with
    // the registry's descriptions.
    std::vector<std::string> names;
    for (const json& tool : body["tools"]) {
        names.push_back(tool["name"].get<std::string>());
        CHECK(tool["description"] == ToolRegistry::instance().find(tool["name"].get<std::string>())->description);
    }
    CHECK(names == JusPrinTest::project_chat_tool_names());
    // The person's words, the workspace in its tag, the attachment listing,
    // then the image and the PDF as parts of their own; the model file has
    // no part, it is listed only.
    const json& user = body["items"][1]["parts"];
    REQUIRE(user.size() == 5);
    CHECK(user[0]["kind"] == static_cast<int>(RecordPart::Kind::Text));
    CHECK(user[0]["text"] == "Duplicate the selected cube.");
    CHECK(user[1]["text"].get<std::string>().rfind("<workspace>\n{", 0) == 0);
    CHECK(user[2]["text"].get<std::string>().find("Attachment att-1: notes.txt [text]\n<attachment id=\"att-1\">\ndo not scale\n</attachment>") != std::string::npos);
    CHECK(user[2]["text"].get<std::string>().find("Attachment stl-1: part.stl [model]") != std::string::npos);
    CHECK(user[3]["kind"] == static_cast<int>(RecordPart::Kind::Image));
    CHECK(user[3]["mime"] == "image/png");
    CHECK(user[3]["base64"] == "iVBORw==");
    CHECK(user[4]["kind"] == static_cast<int>(RecordPart::Kind::Pdf));
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
        // The person's words alone: no record, workspace or skill.
        REQUIRE(title["items"].size() == 1);
        CHECK(title["items"][0]["parts"] == json::array({json{{"kind", 0}, {"text", "Duplicate the selected cube."}, {"mime", ""},
                                                              {"filename", ""}, {"base64", ""}}}));
    }

    SECTION("a turn the app started with nothing to say has no user message")
    {
        h.agent->cancel();
        request.user_text.clear();
        request.attachments.clear();
        request.session.include_workspace = false;
        REQUIRE(h.agent->start(request));
        CHECK(h.last_body()["items"].size() == 1);
        // The same tools as with the model attached: object_import included.
        const json tools = h.last_body()["tools"];
        CHECK(tools == body["tools"]);
        CHECK(std::any_of(tools.begin(), tools.end(), [](const json& tool) { return tool["name"] == "object_import"; }));
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
    CHECK(h.last_body()["items"].size() == 2); // the turn ended; no request carried its reply
}

// What the model reads when the person says how a print went: the G-code that
// left, where it went, what the printer reported, and whether the plate's
// inputs are still the same. No failure is recorded, so none is sent.
TEST_CASE("the workspace carries the recent print history", "[agent][loop]")
{
    Harness      h;
    AgentRequest request = request_fixture();
    GcodeRecord  record;
    record.hash        = std::string(64, 'a');
    record.created_at  = "2026-10-27T13:06:00Z";
    record.plate_name  = "Plate 1";
    record.printer     = "Bambu Lab X1 Carbon 0.4 nozzle";
    record.material    = "Gray PETG";
    record.statistics  = {9360.0, 0.0, 68.0, 0.0, 0};
    GcodeDestination exported;
    exported.kind = GcodeDestination::Kind::Export;
    exported.at   = "2026-10-27T13:05:00Z";
    exported.path = "C:/prints/bracket-v3.gcode";
    GcodeDestination sent;
    sent.kind            = GcodeDestination::Kind::Send;
    sent.at              = "2026-10-27T13:06:00Z";
    sent.printer_name    = "Garage X1C";
    sent.start_requested = true;
    record.destinations  = {exported, sent};
    GcodePrint finished;
    finished.printer_name = "Garage X1C";
    finished.started_at   = "2026-10-27T13:07:00Z";
    finished.finished_at  = "2026-10-27T15:41:00Z";
    GcodePrint unfinished;
    unfinished.printer_name = "Garage X1C";
    unfinished.started_at   = "2026-10-28T09:00:00Z";
    record.prints           = {finished, unfinished};
    request.gcode_history   = {{record, true}};
    REQUIRE(h.agent->start(request));

    const std::string block = h.body(0)["items"][1]["parts"][1]["text"].get<std::string>();
    const json workspace    = json::parse(block.substr(block.find('\n') + 1, block.rfind('\n') - block.find('\n') - 1));
    REQUIRE(workspace["printHistory"].size() == 1);
    const json& entry = workspace["printHistory"][0];
    CHECK(entry["gcode"] == "aaaaaaaa");
    CHECK(entry["plate"] == "Plate 1");
    CHECK(entry["printerPreset"] == "Bambu Lab X1 Carbon 0.4 nozzle");
    CHECK(entry["filamentPreset"] == "Gray PETG");
    CHECK_FALSE(entry.contains("processPreset"));
    CHECK(entry["sameInputsAsNow"] == true);
    CHECK(entry["estimatedSeconds"] == 9360.0);
    CHECK(entry["exports"] == json::array({json{{"at", "2026-10-27T13:05:00Z"}, {"file", "bracket-v3.gcode"}}}));
    CHECK(entry["sends"] ==
          json::array({json{{"at", "2026-10-27T13:06:00Z"}, {"printer", "Garage X1C"}, {"startRequested", true}}}));
    REQUIRE(entry["prints"].size() == 2);
    CHECK(entry["prints"][0]["finishedAt"] == "2026-10-27T15:41:00Z");
    // A print with no finish says only where and when it started: nothing
    // records whether it is still running or stopped, so no outcome is sent.
    CHECK(entry["prints"][1] == json{{"printer", "Garage X1C"}, {"startedAt", "2026-10-28T09:00:00Z"}});

    // A project nothing left has no history to send.
    h.agent->cancel();
    REQUIRE(h.agent->start(request_fixture()));
    CHECK(h.last_body()["items"][1]["parts"][1]["text"].get<std::string>().find("printHistory") == std::string::npos);
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
                                                        json{{"purpose", "Strong PETG bracket"}})})}}));
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
    // The first request's items, exactly, then the turn and its results.
    CHECK(json(body["items"].begin(), body["items"].begin() + 2) == h.body(0)["items"]);
    CHECK(body["instructions"] == h.body(0)["instructions"]);
    REQUIRE(body["items"].size() == 4);
    CHECK(body["items"][2]["kind"] == "assistant");
    CHECK(body["items"][2]["output"] == turn);
    CHECK(body["items"][3]["results"] ==
          json::array({json{{"call_id", "call-read"}, {"output", json{{"state", "succeeded"}, {"result", json{{"summary", json::object()}}}}}, {"image", ""}, {"failed", false}},
                       json{{"call_id", "call-intent"}, {"output", json{{"state", "succeeded"}}}, {"image", ""}, {"failed", false}}}));

    // A second response, another call: the history keeps every step.
    h.fake->data(frame(json{{"turn", "step-2"}, {"calls", json::array({call("call-3", "settings_get", json{{"scope", "process"}, {"keys", json::array({"wall_loops"})}}) })}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-3", "succeeded", R"({"items":[{"key":"wall_loops","value":"2"}]})"}));
    REQUIRE(h.fake->requests.size() == 3);
    const json third = h.last_body();
    REQUIRE(third["items"].size() == 6);
    CHECK(json(third["items"].begin(), third["items"].begin() + 4) == body["items"]);
    CHECK(third["items"][4]["output"] == "step-2");
    CHECK(third["items"][5]["results"][0]["call_id"] == "call-3");

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
    // The loop answered the refused call itself: no result from the app is
    // waited for.
    CHECK_FALSE(h.agent->continue_after_tool({"call-x", "succeeded", R"({"state":"succeeded"})"}));

    // A tool the turn did not offer is one the model cannot call, even one
    // the registry knows: skill_read with nothing listed.
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

TEST_CASE("a skill fired for a turn's first request follows its message and stays where it was sent", "[agent][loop][skills]")
{
    Harness h;
    AgentRequest request = request_fixture();
    request.skills.push_back(file_loaded_skill());
    REQUIRE(h.agent->start(request));
    // Its picture on the person's side, then its text in the app's voice.
    const json first = h.last_body()["items"];
    REQUIRE(first.size() == 4);
    CHECK(first[2]["kind"] == "user");
    CHECK(first[2]["parts"][0]["kind"] == static_cast<int>(RecordPart::Kind::Image));
    CHECK(first[2]["parts"][0]["base64"] == "iVBORw0KGgo=");
    CHECK(first[3]["kind"] == "operator");
    CHECK(first[3]["text"] == "A file was loaded.");

    // The request that carries a result repeats the first and adds to it.
    h.fake->data(frame(json{{"turn", "t"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-1", "succeeded", R"({"state":"succeeded"})"}));
    const json later = h.last_body()["items"];
    REQUIRE(later.size() == 6);
    CHECK(json(later.begin(), later.begin() + 4) == first);
    CHECK(Harness::of_kind(h.last_body(), "operator").size() == 1);
}

TEST_CASE("a skill a tool result fires follows every result of its response, once", "[agent][loop][skills]")
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

    // The results, then the one skill: its picture, then its text.
    const json body = h.last_body()["items"];
    REQUIRE(body.size() == 6);
    REQUIRE(body[3]["results"].size() == 3);
    CHECK(body[3]["results"][2]["call_id"] == "call-read");
    CHECK(body[4]["parts"][0]["base64"] == "iVBORw0KGgo=");
    CHECK(body[5]["kind"] == "operator");
    CHECK(body[5]["text"] == "The second import.");

    // The next request of the same turn repeats it and fires nothing new.
    h.fake->data(frame(json{{"turn", "t2"}, {"calls", json::array({call("call-4", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-4", "succeeded", R"({"state":"succeeded"})"}));
    const json later = h.last_body()["items"];
    REQUIRE(later.size() == 8);
    CHECK(json(later.begin(), later.begin() + 6) == body);
    CHECK(later[7]["results"][0]["call_id"] == "call-4");
}

TEST_CASE("a skill that yields to another on the same request is left out of it", "[agent][loop][skills]")
{
    SECTION("a turn's first request")
    {
        Harness h;
        AgentRequest request = request_fixture();
        request.skills = {setup_skill()};
        REQUIRE(h.agent->start(request));
        CHECK(operator_texts(h.last_body()) == std::vector<std::string>{"Check the setup."});
        h.agent->cancel();
        request.skills = {file_loaded_skill(), setup_skill()};
        REQUIRE(h.agent->start(request));
        CHECK(operator_texts(h.last_body()) == std::vector<std::string>{"A file was loaded."});
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
        CHECK(operator_texts(h.last_body()) == std::vector<std::string>{"A file was loaded."});
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

TEST_CASE("every request's skills stay where they were sent, and a result that failed is marked so", "[agent][loop][skills]")
{
    Harness h;
    AgentRequest request = request_fixture();
    request.skills.push_back(file_loaded_skill("The turn's own.", false));
    REQUIRE(h.agent->start(request));
    h.fake->data(frame(json{{"turn", "t1"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    AgentToolResult result{"call-1", "succeeded", R"({"state":"succeeded"})"};
    result.skills.push_back(setup_skill());
    REQUIRE(h.agent->continue_after_tool(result));
    h.fake->data(frame(json{{"turn", "t2"}, {"calls", json::array({call("call-2", "workspace_inspect")})}}));
    REQUIRE(h.poll_until(AgentEventKind::ToolCall));
    REQUIRE(h.agent->continue_after_tool({"call-2", "failed", R"({"state":"failed"})"}));

    // This request fired nothing new; the first two skills stay where they went.
    const json body = h.last_body()["items"];
    std::vector<std::string> kinds;
    for (const json& item : body)
        kinds.push_back(item["kind"]);
    CHECK(kinds == std::vector<std::string>{"assistant", "user", "operator", "assistant", "results", "operator", "assistant", "results"});
    CHECK(body[2]["text"] == "The turn's own.");
    CHECK(body[5]["text"] == "Check the setup.");
    CHECK(body[4]["results"][0]["failed"] == false);
    CHECK(body[7]["results"][0]["failed"] == true);
}

// What the loop hands the chat's record, and when: what a request carried,
// once the provider has taken it and a later request can follow on from it
// whatever happens next.
TEST_CASE("the record gets every item sent, never a call without its result nor a skill nothing followed", "[agent][loop][record]")
{
    Harness                  h;
    std::vector<std::string> recorded;
    std::vector<std::string> sources;
    std::vector<std::string> formats;
    AgentRequest             request = request_fixture();
    request.on_recorded = [&](const std::string& format, std::vector<RecordItem> items) {
        formats.push_back(format);
        for (const RecordItem& item : items) {
            recorded.push_back(picture(item)["kind"].get<std::string>() + (item.text.empty() ? "" : " " + item.text));
            sources.push_back(item.source);
        }
    };
    request.skills.push_back(file_loaded_skill("Skill one.", false));

    SECTION("a turn that ends")
    {
        REQUIRE(h.agent->start(request));
        // Nothing until the provider takes the request.
        CHECK(recorded.empty());
        h.fake->data(frame(json{{"turn", "t1"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
        REQUIRE(h.poll_until(AgentEventKind::ToolCall));
        // The person's message is kept once taken; the skill waits for an
        // answer, and a call without its result is not kept.
        CHECK(recorded == std::vector<std::string>{"user"});
        AgentToolResult result{"call-1", "succeeded", R"({"state":"succeeded"})"};
        result.skills.push_back(setup_skill());
        REQUIRE(h.agent->continue_after_tool(result));
        CHECK(recorded.size() == 1);
        h.fake->data(frame(json{{"turn", "t2"}}));
        REQUIRE(h.poll_until(AgentEventKind::Completed));
        CHECK(recorded ==
              std::vector<std::string>{"user", "operator Skill one.", "assistant", "results", "operator Check the setup.", "assistant"});
        CHECK(formats == std::vector<std::string>{"responses", "responses", "responses"});
    }
    SECTION("a turn whose stream the provider took and then broke off")
    {
        REQUIRE(h.agent->start(request));
        h.fake->data(frame(json{{"failed", json{{"code", "service_unavailable"}, {"retryable", true}}}}));
        REQUIRE(h.poll_until(AgentEventKind::Failed));
        // A retry follows on from the message; the skill is fired again then.
        CHECK(recorded == std::vector<std::string>{"user"});
    }
    SECTION("a turn whose request the provider refuses keeps nothing")
    {
        const unsigned status = GENERATE(503u, 400u, 413u);
        REQUIRE(h.agent->start(request));
        h.fake->error(status);
        REQUIRE(h.poll_until(AgentEventKind::Failed));
        CHECK(recorded.empty());
    }
    SECTION("a turn refused after its first request keeps only that request's person side")
    {
        REQUIRE(h.agent->start(request));
        h.fake->data(frame(json{{"turn", "t1"}, {"calls", json::array({call("call-1", "workspace_inspect")})}}));
        REQUIRE(h.poll_until(AgentEventKind::ToolCall));
        REQUIRE(h.agent->continue_after_tool(AgentToolResult{"call-1", "succeeded", R"({"state":"succeeded"})"}));
        h.fake->error(400);
        REQUIRE(h.poll_until(AgentEventKind::Failed));
        CHECK(recorded == std::vector<std::string>{"user"});
    }
    SECTION("a turn the app started on its note keeps the turn's workspace with the note, once answered")
    {
        request.user_text.clear();
        request.source = "n-7";
        request.note   = "The slice finished.";
        REQUIRE(h.agent->start(request));
        CHECK(recorded.empty());
        h.fake->data(frame(json{{"turn", "t1"}}));
        REQUIRE(h.poll_until(AgentEventKind::Completed));
        CHECK(recorded == std::vector<std::string>{"user", "operator The slice finished.", "operator Skill one.", "assistant"});
        CHECK(sources == std::vector<std::string>{"", "n-7", "", ""});
    }
    SECTION("a turn the app started on its note whose request fails keeps nothing")
    {
        request.user_text.clear();
        request.source = "n-7";
        request.note   = "The slice finished.";
        REQUIRE(h.agent->start(request));
        h.fake->data(frame(json{{"failed", json{{"code", "service_unavailable"}, {"retryable", true}}}}));
        REQUIRE(h.poll_until(AgentEventKind::Failed));
        CHECK(recorded.empty());
    }
    SECTION("a request whose picture could not be kept is never sent")
    {
        request.skills    = {file_loaded_skill("Skill one.")};
        request.keep_blob = [](const std::string&) -> std::optional<std::string> { return std::nullopt; };
        CHECK_FALSE(h.agent->start(request));
        CHECK(h.fake->requests.empty());
        CHECK_FALSE(h.agent->busy());
    }
    SECTION("a reply request with nowhere to keep what it sends is a logic error")
    {
        request.on_recorded = nullptr;
        CHECK_THROWS_AS(h.agent->start(request), std::logic_error);
        CHECK(h.fake->requests.empty());
    }
}

TEST_CASE("a title request completes without a record to keep", "[agent][loop][record]")
{
    // The host sends a title request with no record sink: the title belongs
    // to no chat, so nothing of it is kept. Found live 2026-10-29: the
    // completion settled the model's reply into the record and called the
    // absent sink, which threw and ended the app's main loop.
    Harness      h;
    AgentRequest request = request_fixture();
    request.purpose     = AgentRequest::Purpose::ConversationTitle;
    request.on_recorded = nullptr;
    request.record      = {};
    REQUIRE(h.agent->start(request));
    h.fake->data(frame(json{{"text", "Cube duplication"}}));
    h.fake->data(frame(json{{"turn", "title-turn"}}));
    REQUIRE(h.poll_until(AgentEventKind::Completed));
    CHECK_FALSE(h.agent->busy());
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
    const json body = h.last_body()["items"];
    REQUIRE(body.size() == 3);
    CHECK(body[2]["output"] == "paused-1");
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
