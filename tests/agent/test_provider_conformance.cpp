// One turn, told the same way by every provider, must reach the app as the
// same events. This keeps the turn loop neutral as codecs are added: each
// codec is fed its own provider's stream for the same scripted turn, and the
// AgentEvents the loop hands the host are compared.

#include <catch2/catch_all.hpp>
#include <catch2/catch_message.hpp>

#include "kept_turn.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderFactory.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>

using namespace Slic3r::GUI::JusPrin::Agent;
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
    void complete() { callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, 200}); }

    std::vector<AgentHttpRequest> requests;
    std::vector<EventFn>          callbacks;
};

std::string sse(const json& event) { return "data: " + event.dump() + "\n\n"; }

// A scripted response: some text, then these calls.
struct Response
{
    std::string                                       text;
    std::vector<std::pair<std::string, json>>         calls; // call id, arguments
};

const std::string kTool = "workspace_inspect";

std::string responses_stream(const Response& response)
{
    json output = json::array();
    if (!response.text.empty())
        output.push_back(json{{"type", "message"}, {"role", "assistant"},
                              {"content", json::array({json{{"type", "output_text"}, {"text", response.text}}})}});
    for (const auto& [id, arguments] : response.calls)
        output.push_back(json{{"type", "function_call"}, {"call_id", id}, {"name", kTool}, {"arguments", arguments.dump()}});
    std::string out;
    if (!response.text.empty())
        out += sse(json{{"type", "response.output_text.delta"}, {"delta", response.text}});
    out += sse(json{{"type", "response.completed"},
                    {"response", json{{"output", output}, {"usage", json{{"input_tokens", 100}, {"output_tokens", 10}, {"total_tokens", 110}}}}}});
    return out;
}

std::string anthropic_stream(const Response& response)
{
    std::string out = sse(json{{"type", "message_start"},
                               {"message", json{{"id", "msg_1"}, {"usage", json{{"input_tokens", 100}, {"output_tokens", 1}}}}}});
    int index = 0;
    out += sse(json{{"type", "content_block_start"}, {"index", index}, {"content_block", json{{"type", "thinking"}, {"thinking", ""}, {"signature", ""}}}});
    out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "signature_delta"}, {"signature", "sig"}}}});
    out += sse(json{{"type", "content_block_stop"}, {"index", index++}});
    if (!response.text.empty()) {
        out += sse(json{{"type", "content_block_start"}, {"index", index}, {"content_block", json{{"type", "text"}, {"text", ""}}}});
        out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "text_delta"}, {"text", response.text}}}});
        out += sse(json{{"type", "content_block_stop"}, {"index", index++}});
    }
    for (const auto& [id, arguments] : response.calls) {
        out += sse(json{{"type", "content_block_start"}, {"index", index},
                        {"content_block", json{{"type", "tool_use"}, {"id", id}, {"name", kTool}, {"input", json::object()}}}});
        const std::string input = arguments.dump();
        const std::size_t split = input.size() / 2;
        for (const std::string& piece : {input.substr(0, split), input.substr(split)})
            out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "input_json_delta"}, {"partial_json", piece}}}});
        out += sse(json{{"type", "content_block_stop"}, {"index", index++}});
    }
    out += sse(json{{"type", "message_delta"}, {"delta", json{{"stop_reason", response.calls.empty() ? "end_turn" : "tool_use"}}},
                    {"usage", json{{"output_tokens", 10}}}});
    out += sse(json{{"type", "message_stop"}});
    return out;
}

// What the host sees of an event, as text that compares exactly.
std::string describe(const AgentEvent& event)
{
    switch (event.kind) {
    case AgentEventKind::TextDelta: return "text " + event.text;
    case AgentEventKind::ToolCall:
        return "call " + event.tool->call_id + " " + event.tool->request.tool + " " + event.tool->request.arguments_json +
               (event.tool->await_result ? " await" : "");
    case AgentEventKind::Completed: return "completed";
    case AgentEventKind::Failed: return "failed " + event.error->code;
    }
    return "?";
}

struct Run
{
    std::vector<std::string> events;
    std::size_t              requests{0};
    std::size_t              usage_reports{0};
};

// The scripted turn: a fired skill on the first request; a response with
// text and two calls; the first call returns a picture and fires a skill,
// the second fails; then a response of text alone.
Run run(const std::string& provider, const std::function<std::string(const Response&)>& encode)
{
    Run               result;
    ProviderListeners listeners;
    listeners.usage = [&result](const AgentUsage&) { ++result.usage_reports; };
    auto           transport = std::make_unique<FakeTransport>();
    FakeTransport* fake      = transport.get();
    AgentServicePtr agent    = make_provider_agent(*profile_for(provider), "key", "", "", std::move(transport), std::move(listeners));

    AgentRequest request;

    JusPrinTest::kept_nowhere(request);
    request.request_id           = "conformance";
    request.session.instructions = "Instructions.";
    request.user_text            = "Check the plate.";
    request.skills = {AgentSkillUse{"file-loaded", "A file was loaded.",
                                    std::make_shared<const ToolImage>(ToolImage{"image/png", "QUJD", 2, 1}), "file_loaded"}};
    REQUIRE(agent->start(request));

    const auto drain = [&]() {
        for (int i = 0; i < 40; ++i)
            if (auto event = agent->poll())
                result.events.push_back(describe(*event));
    };
    fake->data(encode(Response{"Let me look.", {{"call_1", json{{"sections", json::array({"summary"})}}}, {"call_2", json::object()}}}));
    fake->complete();
    drain();
    AgentToolResult rendered{"call_1", "succeeded", R"({"state":"succeeded","result":{"summary":{}}})"};
    rendered.image = std::make_shared<ToolImage>(ToolImage{"image/png", "REVG", 2, 1});
    rendered.skills.push_back(AgentSkillUse{"project-setup-changed", "Check the setup.", nullptr, "setup_changed"});
    REQUIRE(agent->continue_after_tool(rendered));
    drain();
    REQUIRE(agent->continue_after_tool({"call_2", "failed", R"({"state":"failed","error":{"code":"stale_workspace"}})"}));
    drain();
    fake->data(encode(Response{"Done.", {}}));
    fake->complete();
    drain();
    result.requests = fake->requests.size();
    return result;
}

} // namespace

TEST_CASE("one turn told by the Responses API and by Anthropic reaches the app as the same events", "[agent][conformance]")
{
    const Run responses = run("openai", responses_stream);
    const Run anthropic = run("anthropic", anthropic_stream);

    CHECK(responses.events == std::vector<std::string>{
                                  "text Let me look.",
                                  R"(call call_1 workspace_inspect {"sections":["summary"]} await)",
                                  "call call_2 workspace_inspect {} await",
                                  "text Done.",
                                  "completed",
                              });
    CHECK(anthropic.events == responses.events);
    CHECK(responses.requests == 2);
    CHECK(anthropic.requests == responses.requests);
    CHECK(responses.usage_reports == 2);
    CHECK(anthropic.usage_reports == responses.usage_reports);
}

// OBICO-SERVER-B2N. When one Anthropic reply asks for a web search and an
// app tool at once, Anthropic runs the search only after the app's results
// come back, and its next reply starts with the search's result. A turn can
// stop at any point of that; the next message must still be one Anthropic
// takes: every call has a result, and a search either has its result or,
// having never run, is left out.
namespace {

struct StoppedTurn
{
    std::vector<RecordItem> kept;
    FakeTransport*          fake{nullptr};
    AgentServicePtr         agent;

    StoppedTurn()
    {
        auto transport = std::make_unique<FakeTransport>();
        fake           = transport.get();
        agent          = make_provider_agent(*profile_for("anthropic"), "key", "", "", std::move(transport), {});
    }

    AgentRequest request(const std::string& id, const std::string& words)
    {
        AgentRequest request;
        request.on_recorded = [this](const std::string&, std::vector<RecordItem> items) {
            kept.insert(kept.end(), items.begin(), items.end());
        };
        request.keep_blob            = [](const std::string&) -> std::string { return "chats/test/blob"; };
        request.request_id           = id;
        request.chat_key             = "chat";
        request.record.format        = "anthropic";
        request.record.items         = kept;
        request.session.instructions = "Instructions.";
        request.user_text            = words;
        return request;
    }

    std::vector<std::string> drain()
    {
        std::vector<std::string> events;
        for (int i = 0; i < 40; ++i)
            if (auto event = agent->poll())
                events.push_back(describe(*event));
        return events;
    }

    static std::string block(int index, const json& content, const std::string& input = {})
    {
        std::string out = sse(json{{"type", "content_block_start"}, {"index", index}, {"content_block", content}});
        if (!input.empty())
            out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", json{{"type", "input_json_delta"}, {"partial_json", input}}}});
        return out + sse(json{{"type", "content_block_stop"}, {"index", index}});
    }

    static std::string call(int index, const std::string& id) { return block(index, json{{"type", "tool_use"}, {"id", id}, {"name", kTool}, {"input", json::object()}}, "{}"); }

    void reply(const std::string& blocks)
    {
        fake->data(sse(json{{"type", "message_start"}, {"message", json{{"id", "msg"}, {"usage", json{{"input_tokens", 100}, {"output_tokens", 1}}}}}}) +
                   block(0, json{{"type", "thinking"}, {"thinking", "Searching first."}, {"signature", "sig"}}) + blocks +
                   sse(json{{"type", "message_delta"}, {"delta", json{{"stop_reason", "tool_use"}}}, {"usage", json{{"output_tokens", 10}}}}) +
                   sse(json{{"type", "message_stop"}}));
        fake->complete();
    }

    // Reply 1: the search, with no result yet, and a call.
    void first_reply()
    {
        reply(block(1, json{{"type", "server_tool_use"}, {"id", "srvtoolu_1"}, {"name", "web_search"}, {"input", json::object()}},
                    R"({"query":"nozzle"})") +
              call(2, "toolu_1"));
    }

    // Reply 2: the search's result, then two calls.
    void second_reply()
    {
        reply(block(1, json{{"type", "web_search_tool_result"}, {"tool_use_id", "srvtoolu_1"},
                            {"content", json::array({json{{"type", "web_search_result"}, {"url", "https://example.test"},
                                                          {"title", "Nozzle"}, {"encrypted_content", "abc"}}})}}) +
              call(2, "toolu_2") + call(3, "toolu_3"));
    }

    json sent(std::size_t request) const { return json::parse(fake->requests.at(request).body)["messages"]; }
};

// Every block of a type in the messages, by the id it names.
std::vector<std::string> ids_of(const json& messages, const std::string& type, const char* id_key)
{
    std::vector<std::string> ids;
    for (const json& message : messages)
        if (message["content"].is_array())
            for (const json& content : message["content"])
                if (content.value("type", "") == type)
                    ids.push_back(content.value(id_key, ""));
    return ids;
}

// The tool_result answering a call, as sent.
json result_for(const json& messages, const std::string& call_id)
{
    for (const json& message : messages)
        if (message["content"].is_array())
            for (const json& content : message["content"])
                if (content.value("type", "") == "tool_result" && content.value("tool_use_id", "") == call_id)
                    return content;
    return json();
}

} // namespace

TEST_CASE("a search whose result came in a reply the turn stopped during keeps its result, and every call has one",
          "[agent][anthropic][record]")
{
    StoppedTurn t;
    REQUIRE(t.agent->start(t.request("turn-1", "Find this printer's nozzle size.")));
    t.first_reply();
    CHECK(t.drain() == std::vector<std::string>{"call toolu_1 workspace_inspect {} await"});
    REQUIRE(t.agent->continue_after_tool({"toolu_1", "succeeded", R"({"state":"succeeded","result":{}})"}));
    t.drain();
    REQUIRE(t.fake->requests.size() == 2);
    // Within the turn the search waits for the app's results: it is sent
    // without one, which is what makes Anthropic run it.
    CHECK(ids_of(t.sent(1), "server_tool_use", "id") == std::vector<std::string>{"srvtoolu_1"});
    CHECK(ids_of(t.sent(1), "web_search_tool_result", "tool_use_id").empty());

    t.second_reply();
    CHECK(t.drain() == std::vector<std::string>{"call toolu_2 workspace_inspect {} await"});
    t.agent->cancel();

    REQUIRE(t.agent->start(t.request("turn-2", "Go on.")));
    const json messages = t.sent(2);
    INFO(messages.dump(2));
    CHECK(ids_of(messages, "server_tool_use", "id") == std::vector<std::string>{"srvtoolu_1"});
    CHECK(ids_of(messages, "web_search_tool_result", "tool_use_id") == std::vector<std::string>{"srvtoolu_1"});
    CHECK(ids_of(messages, "tool_use", "id") == std::vector<std::string>{"toolu_1", "toolu_2", "toolu_3"});
    CHECK(ids_of(messages, "tool_result", "tool_use_id") == std::vector<std::string>{"toolu_1", "toolu_2", "toolu_3"});
    for (const char* unanswered : {"toolu_2", "toolu_3"}) {
        const json result = result_for(messages, unanswered);
        CHECK(result.value("is_error", false));
        CHECK(result.dump().find("turn_ended") != std::string::npos);
    }
}

TEST_CASE("a search the turn stopped before it ran is left out, and its reply's call has a result", "[agent][anthropic][record]")
{
    StoppedTurn t;
    REQUIRE(t.agent->start(t.request("turn-1", "Find this printer's nozzle size.")));
    t.first_reply();
    CHECK(t.drain() == std::vector<std::string>{"call toolu_1 workspace_inspect {} await"});
    t.agent->cancel();

    // A turn the app starts on its own note carries no words of the person.
    AgentRequest next = t.request("turn-2", "");
    next.note         = "The project was saved.";
    next.source       = "note-1";
    REQUIRE(t.agent->start(next));
    const json messages = t.sent(1);
    INFO(messages.dump(2));
    CHECK(ids_of(messages, "server_tool_use", "id").empty());
    CHECK(ids_of(messages, "tool_use", "id") == std::vector<std::string>{"toolu_1"});
    CHECK(ids_of(messages, "tool_result", "tool_use_id") == std::vector<std::string>{"toolu_1"});
    CHECK(result_for(messages, "toolu_1").value("is_error", false));
}
