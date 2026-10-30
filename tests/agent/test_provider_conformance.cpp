// One turn, told the same way by every provider, must reach the app as the
// same events. This keeps the turn loop neutral as codecs are added: each
// codec is fed its own provider's stream for the same scripted turn, and the
// AgentEvents the loop hands the host are compared.

#include <catch2/catch_all.hpp>

#include "kept_turn.hpp"
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
