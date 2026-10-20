// Anthropic's Messages API wire format: what an agent built from the
// anthropic profile puts in a request, and what it makes of the stream. The
// turn loop's own rules are test_provider_agent.cpp's; that the two codecs
// drive the loop alike is test_provider_conformance.cpp's.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AnthropicCodec.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderFactory.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.hpp"

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
        return true;
    }
    void cancel() override {}

    void data(std::string value) { callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Data, std::move(value), {}, 0}); }
    json body(std::size_t index) const { return json::parse(requests.at(index).body); }
    json last_body() const { return json::parse(requests.back().body); }

    std::vector<AgentHttpRequest> requests;
    std::vector<EventFn>          callbacks;
};

std::string sse(const json& event) { return "event: " + event.value("type", "") + "\ndata: " + event.dump() + "\n\n"; }

// One streamed response: the given content blocks, then its end.
std::string stream(const json& blocks, const std::string& stop_reason, json usage = json{{"input_tokens", 10}, {"output_tokens", 1}},
                   json stop_details = nullptr)
{
    std::string out = sse(json{{"type", "message_start"},
                               {"message", json{{"id", "msg_1"}, {"type", "message"}, {"role", "assistant"}, {"content", json::array()},
                                                {"usage", usage}}}});
    out += sse(json{{"type", "ping"}});
    int index = 0;
    for (const json& block : blocks) {
        const std::string type = block["type"];
        json start = block;
        if (type == "text")
            start["text"] = "";
        if (type == "thinking") {
            start["thinking"]  = "";
            start["signature"] = "";
        }
        if (type == "tool_use")
            start["input"] = json::object();
        out += sse(json{{"type", "content_block_start"}, {"index", index}, {"content_block", start}});
        if (type == "text")
            out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "text_delta"}, {"text", block["text"]}}}});
        if (type == "thinking") {
            out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "thinking_delta"}, {"thinking", block["thinking"]}}}});
            out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "signature_delta"}, {"signature", block["signature"]}}}});
        }
        if (type == "tool_use") {
            // The arguments arrive in pieces, split mid-token.
            const std::string input = block["input"].dump();
            const std::size_t half  = input.size() / 2;
            for (const std::string& piece : {input.substr(0, half), input.substr(half)})
                out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "input_json_delta"}, {"partial_json", piece}}}});
        }
        out += sse(json{{"type", "content_block_stop"}, {"index", index}});
        ++index;
    }
    json delta{{"stop_reason", stop_reason}, {"stop_sequence", nullptr}};
    if (!stop_details.is_null())
        delta["stop_details"] = stop_details;
    out += sse(json{{"type", "message_delta"}, {"delta", delta}, {"usage", json{{"output_tokens", usage.value("output_tokens", 1)}}}});
    out += sse(json{{"type", "message_stop"}});
    return out;
}

json text(const std::string& value) { return json{{"type", "text"}, {"text", value}}; }
json tool_use(const std::string& id, const std::string& name, json input = json::object())
{
    return json{{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", std::move(input)}};
}

struct Agent
{
    explicit Agent(ProviderListeners listeners = {})
    {
        auto transport = std::make_unique<FakeTransport>();
        fake           = transport.get();
        service = make_provider_agent(*profile_for("anthropic"), "sk-ant-secret", "", "", std::move(transport), std::move(listeners));
    }
    std::optional<AgentEvent> poll_until(AgentEventKind kind)
    {
        for (int i = 0; i < 40; ++i)
            if (auto event = service->poll(); event && event->kind == kind)
                return event;
        return std::nullopt;
    }
    FakeTransport*  fake{nullptr};
    AgentServicePtr service;
};

AgentRequest request_fixture()
{
    AgentRequest request;
    request.request_id           = "assistant-7-attempt-1";
    request.session.instructions = "You are the project assistant, as the page wrote it.";
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
    request.conversation = {{"user", "Hello"}, {"assistant", "Earlier answer"}};
    request.attachments.push_back({"att-1", "notes.txt", "text", "text/plain", "", "do not scale", "", false});
    return request;
}

AgentSkillUse file_loaded_skill(const std::string& text = "A file was loaded.")
{
    return AgentSkillUse{"file-loaded", text, std::make_shared<const ToolImage>(ToolImage{"image/png", "iVBORw0KGgo=", 2, 1}),
                         "file_loaded"};
}

// The JSON without its cache breakpoints: those may move between requests
// without changing what the model was told.
json without_cache_control(json value)
{
    if (value.is_object()) {
        value.erase("cache_control");
        for (auto& [key, child] : value.items())
            child = without_cache_control(child);
    } else if (value.is_array()) {
        for (json& child : value)
            child = without_cache_control(child);
    }
    return value;
}

void check_alternates(const json& messages)
{
    for (std::size_t i = 1; i < messages.size(); ++i) {
        CAPTURE(i, messages[i - 1]["role"], messages[i]["role"]);
        CHECK(messages[i]["role"] != messages[i - 1]["role"]);
    }
    for (std::size_t i = 0; i < messages.size(); ++i)
        if (messages[i]["role"] == "system") {
            CAPTURE(i);
            REQUIRE(i > 0);
            CHECK(messages[i - 1]["role"] == "user");
            if (i + 1 < messages.size())
                CHECK(messages[i + 1]["role"] == "assistant");
        }
}

// How many schema nodes use a keyword. Property names are not keywords, so
// a property's own schema is walked but its name is not counted.
std::size_t count_keyword(const json& schema, const std::string& keyword)
{
    if (schema.is_array()) {
        std::size_t found = 0;
        for (const json& child : schema)
            found += count_keyword(child, keyword);
        return found;
    }
    if (!schema.is_object())
        return 0;
    std::size_t found = schema.contains(keyword) ? 1 : 0;
    for (const char* child : {"properties", "$defs", "definitions"})
        if (schema.contains(child) && schema[child].is_object())
            for (const auto& [name, value] : schema[child].items())
                found += count_keyword(value, keyword);
    for (const char* child : {"items", "additionalProperties", "anyOf", "oneOf", "allOf", "not"})
        if (schema.contains(child))
            found += count_keyword(schema[child], keyword);
    return found;
}

} // namespace

// A chat's chosen thinking level: effort, with thinking left adaptive. A
// conversation title never carries it.
TEST_CASE("an Anthropic request carries the chat's thinking level as effort", "[agent][anthropic]")
{
    ProviderProfile profile = *profile_for("anthropic");
    profile.effort          = "high";
    auto transport          = std::make_unique<FakeTransport>();
    FakeTransport* fake     = transport.get();
    AgentServicePtr service = make_provider_agent(profile, "sk-ant-secret", "claude-sonnet-5-5", "", std::move(transport));
    REQUIRE(service->start(request_fixture()));
    const json body = json::parse(fake->requests.front().body);
    CHECK(body["model"] == "claude-sonnet-5-5");
    CHECK(body["thinking"] == json{{"type", "adaptive"}});
    CHECK(body["output_config"] == json{{"effort", "high"}});
}

TEST_CASE("an Anthropic request: the key header, the version, a cached system block and tools, and no thinking or tool choice",
          "[agent][anthropic]")
{
    Agent agent;
    REQUIRE(agent.service->ready());
    REQUIRE(agent.service->start(request_fixture()));
    REQUIRE(agent.fake->requests.size() == 1);
    const AgentHttpRequest& wire = agent.fake->requests.front();
    CHECK(wire.url == "https://api.anthropic.com/v1/messages");
    CHECK(wire.header("x-api-key") == "sk-ant-secret");
    CHECK(wire.header("anthropic-version") == "2023-06-01");
    CHECK(wire.header("Authorization").empty());
    CHECK(wire.header("Idempotency-Key").empty());
    CHECK(wire.body.find("sk-ant-secret") == std::string::npos);

    const json body = json::parse(wire.body);
    CHECK(body["model"] == "claude-opus-5-5");
    CHECK(body["max_tokens"] == 64000);
    CHECK(body["stream"] == true);
    CHECK_FALSE(body.contains("thinking"));
    CHECK_FALSE(body.contains("tool_choice"));
    CHECK_FALSE(body.contains("store"));
    CHECK(body["system"] == json::array({json{{"type", "text"},
                                              {"text", "You are the project assistant, as the page wrote it."},
                                              {"cache_control", json{{"type", "ephemeral"}}}}}));
    // The project chat's tools, in the registry's order, with only the last
    // carrying the breakpoint.
    const json& tools = body["tools"];
    REQUIRE(tools.size() == 28);
    for (std::size_t i = 0; i < tools.size(); ++i) {
        CAPTURE(tools[i]["name"]);
        const ToolDefinition* definition = ToolRegistry::instance().find(tools[i]["name"].get<std::string>());
        REQUIRE(definition != nullptr);
        CHECK(tools[i]["description"] == definition->description);
        CHECK(tools[i]["input_schema"] == anthropic_strict_schema(definition->input_schema));
        CHECK(tools[i]["strict"].is_boolean());
        CHECK(tools[i].contains("cache_control") == (i + 1 == tools.size()));
    }

    // The earlier conversation, then this turn's message: the person's words,
    // the workspace in its tag, the attachment listing.
    const json& messages = body["messages"];
    REQUIRE(messages.size() == 3);
    CHECK(messages[0] == json{{"role", "user"}, {"content", json::array({text("Hello")})}});
    CHECK(messages[1] == json{{"role", "assistant"}, {"content", json::array({text("Earlier answer")})}});
    CHECK(messages[2]["role"] == "user");
    const json& content = messages[2]["content"];
    REQUIRE(content.size() == 3);
    CHECK(content[0] == text("Duplicate the selected cube."));
    CHECK(content[1]["text"].get<std::string>().rfind("<workspace>\n{", 0) == 0);
    CHECK(content[2]["text"].get<std::string>().find("<attachment id=\"att-1\">\ndo not scale\n</attachment>") != std::string::npos);
    // The breakpoint that caches the turn so far sits on its newest block.
    CHECK(content[2]["cache_control"] == json{{"type", "ephemeral"}});
    CHECK_FALSE(content[0].contains("cache_control"));
}

TEST_CASE("strict mode gets every registry schema without the keywords it rejects", "[agent][anthropic][schema]")
{
    const json schema = json::parse(R"({
        "type": "object",
        "additionalProperties": false,
        "required": ["count", "name", "ids", "pair", "factor", "options"],
        "properties": {
            "count":  {"type": "integer", "minimum": 1, "maximum": 100, "description": "How many."},
            "name":   {"type": "string", "minLength": 1, "maxLength": 64, "format": "uuid"},
            "ids":    {"type": "array", "minItems": 1, "maxItems": 55, "items": {"type": "string", "enum": ["a", "b"], "maxLength": 1}},
            "pair":   {"type": "array", "minItems": 2, "items": {"type": "number", "multipleOf": 0.5}},
            "factor": {"type": "number", "multipleOf": 0.25, "description": "Scale"},
            "options": {"type": "object", "additionalProperties": false, "required": [], "properties": {}, "minProperties": 0, "maxProperties": 3}
        }
    })");
    const json projected = anthropic_strict_schema(schema);
    const json& p        = projected["properties"];
    CHECK(p["count"] == json{{"type", "integer"}, {"description", "How many. (minimum 1, maximum 100)"}});
    CHECK(p["name"] == json{{"type", "string"}, {"format", "uuid"}, {"description", "(minLength 1, maxLength 64)"}});
    // minItems 0 or 1 is the one array constraint strict mode accepts.
    CHECK(p["ids"]["minItems"] == 1);
    CHECK_FALSE(p["ids"].contains("maxItems"));
    CHECK(p["ids"]["description"] == "(maxItems 55)");
    CHECK(p["ids"]["items"] == json{{"type", "string"}, {"enum", json::array({"a", "b"})}, {"description", "(maxLength 1)"}});
    CHECK_FALSE(p["pair"].contains("minItems"));
    CHECK(p["pair"]["description"] == "(minItems 2)");
    CHECK(p["pair"]["items"]["description"] == "(multipleOf 0.5)");
    CHECK(p["factor"]["description"] == "Scale (multipleOf 0.25)");
    CHECK(p["options"]["description"] == "(minProperties 0, maxProperties 3)");
    // Everything else is the registry's, and the registry is unchanged.
    CHECK(projected["required"] == schema["required"]);
    CHECK(projected["additionalProperties"] == false);
    CHECK(schema["properties"]["count"]["minimum"] == 1);

    // No tool the app can offer reaches Anthropic with a rejected keyword.
    for (const ToolDefinition& definition : ToolRegistry::instance().definitions()) {
        CAPTURE(definition.name);
        const json tool = anthropic_strict_schema(definition.input_schema);
        for (const char* keyword : {"minimum", "maximum", "multipleOf", "minLength", "maxLength", "maxItems", "minProperties", "maxProperties"})
            CHECK(count_keyword(tool, keyword) == 0);
        CHECK(tool.dump().find("\"minItems\":2") == std::string::npos);
    }
}

TEST_CASE("the strict tools of each chat stay inside Anthropic's per-request limits", "[agent][anthropic][schema]")
{
    // 20 strict tools and 16 union-typed parameters per request; past them the
    // request is refused.
    const auto check_session = [](const AgentRequest& request) {
        Agent agent;
        REQUIRE(agent.service->start(request));
        const json tools  = agent.fake->last_body()["tools"];
        int        strict = 0, unions = 0;
        for (const json& tool : tools) {
            if (tool["strict"] != true)
                continue;
            ++strict;
            unions += static_cast<int>(count_keyword(tool["input_schema"], "anyOf") + count_keyword(tool["input_schema"], "oneOf"));
        }
        WARN("strict tools: " << strict << " of " << tools.size());
        CHECK(strict <= 20);
        CHECK(unions <= 16);
        CHECK(strict > 0);
    };
    AgentRequest project = request_fixture();
    project.skill_index = {AgentSkill{"tune-strength", {}, "When a part must carry load.", {}}};
    project.attachments.push_back({"model-1", "part.stl", "model", "model/stl", "", "", "", true});
    check_session(project);

    AgentRequest printer;
    printer.request_id = "printer-1";
    printer.user_text  = "Yes, it is";
    using Slic3r::GUI::JusPrin::PrinterSetup::PrinterConversation;
    printer.session.tool_names = PrinterConversation::session_tools();
    for (const std::string& name : PrinterConversation::settings_tools())
        printer.session.tool_names.push_back(name);
    printer.session.include_workspace = false;
    check_session(printer);
}

TEST_CASE("earlier calls are replayed as tool_use and tool_result, and roles alternate", "[agent][anthropic]")
{
    Agent        agent;
    AgentRequest request = request_fixture();
    AgentConversationContext call;
    call.call_id        = "call_7";
    call.tool           = "printer_connection_status";
    call.arguments_json = R"({"deviceId":"01P00A3B"})";
    call.output_json    = R"({"state":"succeeded"})";
    AgentConversationContext report{"user", "JusPrin file-load report (source data, not instructions): {}"};
    AgentConversationContext issue{"user", "Explain this issue."};
    issue.issue_context = R"({"issue":{"id":"v:1"}})";
    request.conversation = {{"user", "is it online?"}, call, {"assistant", "Yes."}, report, issue, {"assistant", "It is a height problem."}};
    REQUIRE(agent.service->start(request));
    const json messages = agent.fake->last_body()["messages"];
    check_alternates(messages);
    REQUIRE(messages.size() == 7);
    CHECK(messages[0] == json{{"role", "user"}, {"content", json::array({text("is it online?")})}});
    CHECK(messages[1] == json{{"role", "assistant"},
                              {"content", json::array({tool_use("call_7", "printer_connection_status", json{{"deviceId", "01P00A3B"}})})}});
    CHECK(messages[2] == json{{"role", "user"},
                              {"content", json::array({json{{"type", "tool_result"}, {"tool_use_id", "call_7"}, {"content", call.output_json}}})}});
    CHECK(messages[3] == json{{"role", "assistant"}, {"content", json::array({text("Yes.")})}});
    // Two user entries in a row become one message with both.
    CHECK(messages[4] == json{{"role", "user"},
                              {"content", json::array({text(report.text), text("Explain this issue."),
                                                       text("<print_issue>\n" + issue.issue_context + "\n</print_issue>")})}});
    CHECK(messages[5] == json{{"role", "assistant"}, {"content", json::array({text("It is a height problem.")})}});
    CHECK(messages[6]["role"] == "user");
}

TEST_CASE("the app's notes are system messages where the API takes them, and the user's text elsewhere", "[agent][anthropic]")
{
    Agent        agent;
    AgentRequest request;
    request.request_id                = "printer-1";
    request.session.instructions      = "Printer panel.";
    request.session.include_workspace = false;
    request.session.tool_names        = {"settings_get"};
    request.user_text                 = "Go on.";
    // After the person and before an answer: a system message. After an
    // answer and before the person: the person's message carries it.
    request.conversation = {{"user", "Add my printer."},
                            {"developer", "The printer is on the network."},
                            {"assistant", "Found it."},
                            {"developer", "The person closed the dialog."}};
    REQUIRE(agent.service->start(request));
    const json messages = agent.fake->last_body()["messages"];
    check_alternates(messages);
    REQUIRE(messages.size() == 4);
    CHECK(messages[1] == json{{"role", "system"}, {"content", "The printer is on the network."}});
    CHECK(messages[3]["role"] == "user");
    CHECK(without_cache_control(messages[3]["content"]) == json::array({text("The person closed the dialog."), text("Go on.")}));
}

TEST_CASE("a title request: the title instructions, the conversation, no tools, and never ending on the assistant",
          "[agent][anthropic][conversations]")
{
    Agent        agent;
    AgentRequest request = request_fixture();
    request.purpose      = AgentRequest::Purpose::ConversationTitle;
    request.skills       = {file_loaded_skill()};
    REQUIRE(agent.service->start(request));
    const json body = agent.fake->last_body();
    CHECK(body["system"][0]["text"] == kConversationTitleInstructions);
    CHECK_FALSE(body.contains("tools"));
    const json messages = without_cache_control(body["messages"]);
    REQUIRE(messages.size() == 3);
    CHECK(messages[0]["content"] == json::array({text("Hello")}));
    CHECK(messages[1]["content"] == json::array({text("Earlier answer")}));
    CHECK(messages[2] == json{{"role", "user"}, {"content", json::array({text("Write the title.")})}});
    CHECK(body.dump().find("sessionId") == std::string::npos);
    CHECK(body.dump().find("A file was loaded.") == std::string::npos);
}

TEST_CASE("pictures, PDFs and fired skills go where Anthropic reads them, and every request extends the one before",
          "[agent][anthropic][skills][image]")
{
    Agent        agent;
    AgentRequest request = request_fixture();
    request.attachments.push_back({"img-1", "photo.png", "image", "image/png", "", "", "\x89PNG", false});
    request.attachments.push_back({"pdf-1", "spec.pdf", "pdf", "application/pdf", "", "", "%PDF", false});
    request.skills = {file_loaded_skill("The turn's skill.")};
    REQUIRE(agent.service->start(request));
    json first = agent.fake->last_body()["messages"];
    check_alternates(first);
    // The turn's message ends with the image and the document, then the
    // skill's picture; its text is a system message, last.
    const json& turn = first[first.size() - 2]["content"];
    CHECK(turn[turn.size() - 3] == json{{"type", "image"}, {"source", json{{"type", "base64"}, {"media_type", "image/png"}, {"data", "iVBORw=="}}}});
    CHECK(turn[turn.size() - 2] == json{{"type", "document"}, {"source", json{{"type", "base64"}, {"media_type", "application/pdf"}, {"data", "JVBERg=="}}}});
    CHECK(without_cache_control(turn.back()) ==
          json{{"type", "image"}, {"source", json{{"type", "base64"}, {"media_type", "image/png"}, {"data", "iVBORw0KGgo="}}}});
    CHECK(first.back() == json{{"role", "system"}, {"content", "The turn's skill."}});

    // Two calls; the first returns a picture and fires a skill, the second fails.
    const json thinking{{"type", "thinking"}, {"thinking", ""}, {"signature", "sig-1"}};
    agent.fake->data(stream(json::array({thinking, text("Looking."), tool_use("toolu_1", "workspace_inspect"),
                                         tool_use("toolu_2", "workspace_inspect", json{{"sections", json::array({"summary"})}})}),
                            "tool_use"));
    REQUIRE(agent.poll_until(AgentEventKind::ToolCall));
    AgentToolResult rendered{"toolu_1", "succeeded", R"({"state":"succeeded"})"};
    rendered.image = std::make_shared<ToolImage>(ToolImage{"image/png", "QUJD", 2, 1});
    rendered.skills.push_back(file_loaded_skill("The result's skill."));
    REQUIRE(agent.service->continue_after_tool(rendered));
    REQUIRE(agent.poll_until(AgentEventKind::ToolCall));
    REQUIRE(agent.service->continue_after_tool({"toolu_2", "failed", R"({"state":"failed"})"}));
    REQUIRE(agent.fake->requests.size() == 2);
    json second = agent.fake->last_body()["messages"];
    check_alternates(second);

    // The response's content, verbatim; then one user message with both
    // results in call order, the picture inside its result; then the
    // result's skill.
    const json& assistant = second[first.size()];
    CHECK(assistant == json{{"role", "assistant"},
                            {"content", json::array({thinking, text("Looking."), tool_use("toolu_1", "workspace_inspect"),
                                                     tool_use("toolu_2", "workspace_inspect", json{{"sections", json::array({"summary"})}})})}});
    const json results = without_cache_control(second[first.size() + 1]["content"]);
    REQUIRE(results.size() == 3);
    CHECK(results[0] == json{{"type", "tool_result"},
                             {"tool_use_id", "toolu_1"},
                             {"content", json::array({text(R"({"state":"succeeded"})"),
                                                      json{{"type", "image"}, {"source", json{{"type", "base64"}, {"media_type", "image/png"}, {"data", "QUJD"}}}}})}});
    CHECK(results[1] == json{{"type", "tool_result"}, {"tool_use_id", "toolu_2"}, {"content", json::array({text(R"({"state":"failed"})")})}, {"is_error", true}});
    CHECK(results[2]["type"] == "image");
    CHECK(second.back() == json{{"role", "system"}, {"content", "The result's skill."}});

    // Append-only: the first request's messages, skill included, open the
    // second unchanged, as the second's open the third. Removing an earlier
    // message would invalidate the thinking after it.
    for (std::size_t i = 0; i < first.size(); ++i) {
        CAPTURE(i);
        CHECK(without_cache_control(second[i]) == without_cache_control(first[i]));
    }
    agent.fake->data(stream(json::array({tool_use("toolu_3", "workspace_inspect")}), "tool_use"));
    REQUIRE(agent.poll_until(AgentEventKind::ToolCall));
    REQUIRE(agent.service->continue_after_tool({"toolu_3", "succeeded", R"({"state":"succeeded"})"}));
    const json third = agent.fake->last_body()["messages"];
    check_alternates(third);
    REQUIRE(third.size() > second.size());
    for (std::size_t i = 0; i < second.size(); ++i) {
        CAPTURE(i);
        CHECK(without_cache_control(third[i]) == without_cache_control(second[i]));
    }
    CHECK(third.back()["role"] == "user");
}

TEST_CASE("the stream becomes typed events: text, calls with split arguments, and usage with the cache", "[agent][anthropic][stream]")
{
    std::vector<AgentUsage> usage;
    ProviderListeners       listeners;
    listeners.usage = [&usage](const AgentUsage& reported) { usage.push_back(reported); };
    Agent agent(std::move(listeners));
    REQUIRE(agent.service->start(request_fixture()));

    const json arguments{{"sessionId", "41"}, {"objects", json::array({json{{"objectId", "72"}, {"quantity", 2}}})}};
    agent.fake->data(stream(json::array({text("Making a copy."), tool_use("toolu_9", "plate_layout", arguments)}), "tool_use",
                            json{{"input_tokens", 120}, {"cache_read_input_tokens", 9000}, {"cache_creation_input_tokens", 300},
                                 {"output_tokens", 1}}));
    const auto delta = agent.poll_until(AgentEventKind::TextDelta);
    REQUIRE(delta);
    CHECK(delta->text == "Making a copy.");
    const auto call = agent.poll_until(AgentEventKind::ToolCall);
    REQUIRE(call);
    CHECK(call->tool->call_id == "toolu_9");
    CHECK(call->tool->request.tool == "plate_layout");
    CHECK(json::parse(call->tool->request.arguments_json) == arguments);
    REQUIRE(usage.size() == 1);
    CHECK(usage[0].input == 9420);
    CHECK(usage[0].cached_input == 9000);
    CHECK(usage[0].cache_creation == 300);
    CHECK(usage[0].output == 1);
    CHECK(usage[0].total == 9421);

    REQUIRE(agent.service->continue_after_tool({"toolu_9", "succeeded", R"({"state":"succeeded"})"}));
    agent.fake->data(stream(json::array({text("Done.")}), "end_turn"));
    REQUIRE(agent.poll_until(AgentEventKind::Completed));
    CHECK_FALSE(agent.service->busy());
}

TEST_CASE("each stop reason ends the response as it should", "[agent][anthropic][stream]")
{
    struct Case
    {
        const char*    stop_reason;
        AgentEventKind kind;
        const char*    code;
        bool           retryable;
        json           details;
    };
    const Case cases[] = {
        {"end_turn", AgentEventKind::Completed, "", false, nullptr},
        {"stop_sequence", AgentEventKind::Completed, "", false, nullptr},
        {"max_tokens", AgentEventKind::Failed, "incomplete_response", true, nullptr},
        {"model_context_window_exceeded", AgentEventKind::Failed, "incomplete_response", true, nullptr},
        {"refusal", AgentEventKind::Failed, "refusal", false, json{{"type", "refusal"}, {"category", "cyber"}, {"explanation", "x"}}},
        {"something_new", AgentEventKind::Failed, "malformed_response", false, nullptr},
    };
    for (const Case& item : cases) {
        CAPTURE(item.stop_reason);
        Agent agent;
        REQUIRE(agent.service->start(request_fixture()));
        agent.fake->data(stream(json::array({text("Partly.")}), item.stop_reason, json{{"input_tokens", 10}, {"output_tokens", 1}}, item.details));
        const auto event = agent.poll_until(item.kind);
        REQUIRE(event);
        if (item.kind == AgentEventKind::Failed) {
            CHECK(event->error->code == item.code);
            CHECK(event->error->retryable == item.retryable);
        }
        if (std::string(item.stop_reason) == "refusal")
            CHECK(event->error->message == "The Agent service declined to answer (cyber).");
        CHECK_FALSE(agent.service->busy());
    }

    SECTION("a paused turn is sent again with what it said so far")
    {
        Agent agent;
        REQUIRE(agent.service->start(request_fixture()));
        agent.fake->data(stream(json::array({text("First half.")}), "pause_turn"));
        CHECK(agent.poll_until(AgentEventKind::TextDelta));
        REQUIRE(agent.fake->requests.size() == 2);
        const json messages = agent.fake->last_body()["messages"];
        CHECK(messages.back() == json{{"role", "assistant"}, {"content", json::array({text("First half.")})}});
        agent.fake->data(stream(json::array({text("Second half.")}), "end_turn"));
        REQUIRE(agent.poll_until(AgentEventKind::Completed));
    }

    SECTION("an error event, and arguments that are not JSON")
    {
        Agent agent;
        REQUIRE(agent.service->start(request_fixture()));
        agent.fake->data(sse(json{{"type", "error"}, {"error", json{{"type", "overloaded_error"}, {"message", "Overloaded"}}}}));
        auto event = agent.poll_until(AgentEventKind::Failed);
        REQUIRE(event);
        CHECK(event->error->code == "agent_service_error");
        CHECK(event->error->retryable);

        REQUIRE(agent.service->start(request_fixture()));
        agent.fake->data(sse(json{{"type", "content_block_start"}, {"index", 0}, {"content_block", tool_use("toolu_x", "workspace_inspect")}}) +
                         sse(json{{"type", "content_block_delta"}, {"index", 0}, {"delta", json{{"type", "input_json_delta"}, {"partial_json", "{\"sect"}}}}) +
                         sse(json{{"type", "content_block_stop"}, {"index", 0}}));
        event = agent.poll_until(AgentEventKind::Failed);
        REQUIRE(event);
        CHECK(event->error->code == "malformed_response");
    }
}
