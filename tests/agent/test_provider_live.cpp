// Live smoke for the providers: real requests through the app's own HTTP
// transport, the agent the app builds, and the project chat's tools.
//
// Every case here spends money, so every case is hidden: nothing runs unless
// it is asked for by tag, and a case without its provider's key skips
// (OPENROUTER_API_KEY, ANTHROPIC_API_KEY). Run it only for a batch Kenneth
// has approved, one provider at a time:
//
//   ( . "$HOME/.config/jusprin/prompt-test.env"; agent_live_tests "[openrouter][live]" )
//   ( export ANTHROPIC_API_KEY=...; agent_live_tests "[anthropic][live]" )
//   ( export OPENAI_API_KEY=...; agent_live_tests "[models][live]" )
//
// JUSPRIN_AGENT_ENDPOINT points it at a local stand-in instead, for a dry
// run of the metering that spends nothing.
//
// Each request is logged on stderr as "LIVE REQUEST n": what was sent, what
// came back, its cost and the running total, and none is sent that could
// take the process past $0.50.
//
// OpenRouter: whether its Responses endpoint honours `instructions`, accepts
// `strict` and `parallel_tool_calls`, replays a turn's own output on a
// continuation, takes an image the way the app sends it (a user message
// after the results), and takes an image inside a function_call_output.
// Anthropic: a reply under the instructions with the project tools on their
// strict-mode projections, and a tool call whose result carries an image the
// model must name the colour of.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ModelCatalogFetch.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderFactory.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <condition_variable>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

// A 1x1 PNG.
constexpr const char* kPixel = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

// A 16x16 solid red PNG: a picture whose colour a model can only name if
// it saw the image.
constexpr const char* kRed = "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAF0lEQVR4nGP4z8BAEiJN9aiGUQ1DSgMAkPn/Afnh+ngAAAAASUVORK5CYII=";

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string openrouter_key() { return environment("OPENROUTER_API_KEY"); }
std::string anthropic_key() { return environment("ANTHROPIC_API_KEY"); }

// A provider's listed prices, in US dollars per token.
struct Pricing
{
    const char* provider;
    double      input;
    double      output;
    double      cache_read;
    double      cache_write;
    // The most output tokens one request may produce: the request's own
    // max_tokens where it sends one, else an allowance (OpenRouter's
    // requests set no limit, so a longer reply is the one way past the cap).
    double      output_allowance;
};
// Listed on 2026-10-19: openai/gpt-5.4-mini on OpenRouter's models API,
// claude-opus-5-5 in Anthropic's price list (5-minute cache writes).
constexpr Pricing kOpenRouter{"openrouter", 0.75e-6, 4.5e-6, 0.075e-6, 0.75e-6, 16000};
constexpr Pricing kAnthropic{"anthropic", 4e-6, 20e-6, 0.2e-6, 5e-6, 4000};
// OpenAI direct, priced high on purpose (a GPT-5.4-class rate) because the
// model-choice smoke picks whichever small model the list offers.
constexpr Pricing kOpenAI{"openai", 2.5e-6, 15e-6, 0.25e-6, 2.5e-6, 16000};

// What this process has spent, in US dollars, and the most it may. A
// request's cost is known only once it returns, so one is refused unless
// the total plus a bound on its cost stays under the cap. The bound takes
// every two bytes of the body as an input token (JSON averages nearer four),
// priced as a cache write, plus the most output the request may produce.
constexpr double kSpendCap = 0.50;
std::mutex       g_spend_mutex;
double           g_spent{0};
int              g_requests{0};

std::vector<json> data_events(const std::string& stream)
{
    std::vector<json> events;
    std::size_t       at = 0;
    while ((at = stream.find("data:", at)) != std::string::npos) {
        at += 5;
        const std::size_t end   = stream.find('\n', at);
        const json        event = json::parse(stream.substr(at, end == std::string::npos ? std::string::npos : end - at), nullptr, false);
        if (event.is_object())
            events.push_back(event);
    }
    return events;
}

// Reads one streamed response and logs what it cost. OpenRouter reports
// usage.cost; Anthropic reports tokens, priced here at the listed rates
// (cache reads and writes apart from the rest of the input).
void meter(int number, const Pricing& pricing, const std::string& stream, unsigned status, const std::string& error)
{
    std::string   terminal = "(none)", id, how = "none reported";
    double        cost = 0;
    std::uint64_t input = 0, cache_read = 0, cache_write = 0, output = 0;
    for (const json& event : data_events(stream)) {
        const std::string type = event.value("type", "");
        if (type == "response.completed" || type == "response.failed" || type == "response.incomplete") {
            terminal         = type;
            const json usage = event["response"].value("usage", json::object());
            id               = event["response"].value("id", "");
            input            = usage.value("input_tokens", 0);
            output           = usage.value("output_tokens", 0);
            if (usage.contains("cost") && usage["cost"].is_number()) {
                cost = usage["cost"].get<double>();
                how  = "usage.cost";
            } else {
                cost = input * pricing.input + output * pricing.output;
                how  = "computed from tokens";
            }
        } else if (type == "message_start") {
            const json usage = event["message"].value("usage", json::object());
            id               = event["message"].value("id", "");
            input            = usage.value("input_tokens", 0);
            cache_read       = usage.value("cache_read_input_tokens", 0);
            cache_write      = usage.value("cache_creation_input_tokens", 0);
            output           = usage.value("output_tokens", 0);
        } else if (type == "message_delta") {
            output   = event.value("usage", json::object()).value("output_tokens", output);
            terminal = "stop_reason " + event.value("delta", json::object()).value("stop_reason", "");
        } else if (type == "error") {
            terminal = "error " + event.dump().substr(0, 300);
        }
    }
    if (how == "none reported" && (input > 0 || output > 0)) {
        cost = input * pricing.input + cache_read * pricing.cache_read + cache_write * pricing.cache_write + output * pricing.output;
        how  = "computed from tokens";
    }
    double total;
    {
        std::lock_guard<std::mutex> lock(g_spend_mutex);
        g_spent += cost;
        total = g_spent;
    }
    std::cerr << "LIVE REQUEST " << number << " status=" << status << (error.empty() ? "" : " error=" + error) << " terminal=" << terminal
              << " id=" << id << " input_tokens=" << input << " cache_read=" << cache_read << " cache_write=" << cache_write
              << " output_tokens=" << output << " cost=$" << cost << " (" << how << ") running_total=$" << total << '\n';
    if (terminal != "response.completed" && terminal.rfind("stop_reason end_turn", 0) != 0 && terminal.rfind("stop_reason tool_use", 0) != 0)
        std::cerr << "LIVE REQUEST " << number << " stream: " << stream.substr(0, 1500) << '\n';
}

// Sends one request through the app's real transport unless it could take
// the spend past the cap, logs what went out, and meters what came back.
bool metered_post(IAgentHttpTransport& transport, const Pricing& pricing, AgentHttpRequest request, IAgentHttpTransport::EventFn event)
{
    const json   body        = json::parse(request.body);
    const double max_output  = body.contains("max_tokens") ? body["max_tokens"].get<double>() : pricing.output_allowance;
    const double bound       = request.body.size() / 2.0 * pricing.cache_write + max_output * pricing.output;
    int          number;
    {
        std::lock_guard<std::mutex> lock(g_spend_mutex);
        if (g_spent + bound > kSpendCap) {
            std::cerr << "LIVE REQUEST refused: $" << g_spent << " spent, this one could cost $" << bound << ", cap $" << kSpendCap
                      << '\n';
            return false;
        }
        number = ++g_requests;
    }
    std::cerr << "LIVE REQUEST " << number << " (" << pricing.provider << ") sent " << request.body.size() << " bytes to " << request.url
              << " model=" << body.value("model", "") << " tools=" << body.value("tools", json::array()).size()
              << " items=" << body.value("input", body.value("messages", json::array())).size()
              << " parallel_tool_calls=" << body.value("parallel_tool_calls", json()).dump() << '\n';
    auto stream = std::make_shared<std::string>();
    return transport.post(std::move(request), [number, &pricing, stream, event = std::move(event)](AgentHttpEvent http) {
        if (http.kind == AgentHttpEvent::Kind::Data)
            *stream += http.data;
        else
            meter(number, pricing, *stream, http.status, http.error);
        event(std::move(http));
    });
}

// The real transport, metered, with every request it posts kept for the test
// to read.
class RecordingTransport final : public IAgentHttpTransport
{
public:
    RecordingTransport(std::vector<AgentHttpRequest>& sent, const Pricing& pricing, bool enforce_history_check = false)
        : m_sent(sent), m_pricing(pricing), m_enforce_history_check(enforce_history_check)
    {}
    bool post(AgentHttpRequest request, EventFn event) override
    {
        if (m_enforce_history_check) {
            // Opus 5.5 checks that replayed thinking follows an unchanged
            // history by default only for accounts created on or after
            // 2026-08-31; any other request is checked only when it says so.
            // Adaptive thinking is what omitting `thinking` means on it.
            json body       = json::parse(request.body);
            body["thinking"] = json{{"type", "adaptive"}, {"block_binding", json{{"prefix_mismatch_behavior", "error"}}}};
            request.body    = body.dump();
            request.headers.push_back({"anthropic-beta", "thinking-binding-controls-2026-08-01"});
        }
        m_sent.push_back(request);
        return metered_post(*m_inner, m_pricing, std::move(request), std::move(event));
    }
    void cancel() override { m_inner->cancel(); }

private:
    std::vector<AgentHttpRequest>&       m_sent;
    const Pricing&                       m_pricing;
    bool                                 m_enforce_history_check;
    std::unique_ptr<IAgentHttpTransport> m_inner{make_openai_http_transport()};
};

struct Turn
{
    std::string                  text;
    std::optional<AgentToolCall> call;
    std::optional<AgentError>    error;
    bool                         completed{false};
};

// Polls until the turn completes, fails or asks for a tool, as the GUI tick would.
Turn run_until_settled(IAgentService& agent)
{
    Turn       turn;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (std::chrono::steady_clock::now() < deadline) {
        while (std::optional<AgentEvent> event = agent.poll()) {
            switch (event->kind) {
            case AgentEventKind::TextDelta: turn.text += event->text; break;
            case AgentEventKind::ToolCall: turn.call = event->tool; return turn;
            case AgentEventKind::Completed: turn.completed = true; return turn;
            case AgentEventKind::Failed: turn.error = event->error; return turn;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    FAIL("no answer from OpenRouter within 180 s");
    return turn;
}

AgentServicePtr live_agent(std::vector<AgentHttpRequest>& sent, std::vector<AgentUsage>& usage)
{
    ProviderListeners listeners;
    listeners.usage = [&usage](const AgentUsage& reported) { usage.push_back(reported); };
    return make_provider_agent(*profile_for("openrouter"), openrouter_key(), "", agent_endpoint_override(), std::make_unique<RecordingTransport>(sent, kOpenRouter),
                               std::move(listeners));
}

void report(const std::vector<AgentUsage>& usage)
{
    for (const AgentUsage& one : usage)
        WARN("usage: input " << one.input << " (cached " << one.cached_input << "), output " << one.output);
}

} // namespace

TEST_CASE("OpenRouter honours the instructions with the project tools offered, strict and parallel calls on",
          "[agent][openrouter][live][.]")
{
    if (openrouter_key().empty())
        SKIP("OPENROUTER_API_KEY is not set");
    std::vector<AgentHttpRequest> sent;
    std::vector<AgentUsage>       usage;
    AgentServicePtr               agent = live_agent(sent, usage);

    AgentRequest request;
    request.request_id                = "live-instructions";
    request.session.instructions      = "You are a connectivity check. Whatever the user writes, reply with exactly the word "
                                        "PINEAPPLE and nothing else. Do not call any tool.";
    request.session.include_workspace = false;
    request.user_text                 = "Hello, are you there?";
    REQUIRE(agent->start(request));
    const Turn turn = run_until_settled(*agent);
    report(usage);

    INFO("reply: " << turn.text << (turn.error ? " error: " + turn.error->code + " " + turn.error->message : ""));
    // Accepted at all: the body carried strict tools and parallel_tool_calls.
    REQUIRE_FALSE(turn.error);
    REQUIRE(turn.completed);
    const json body = json::parse(sent.front().body);
    CHECK(body["parallel_tool_calls"] == true);
    CHECK(std::any_of(body["tools"].begin(), body["tools"].end(), [](const json& tool) { return tool["strict"] == true; }));
    CHECK(body["tools"].size() == 28);
    // Honoured: the instructions, not the person, decided the words.
    CHECK(turn.text.find("PINEAPPLE") != std::string::npos);
    CHECK(usage.size() == 1);
}

TEST_CASE("OpenRouter makes a tool call and continues from its result and the image after it", "[agent][openrouter][live][.]")
{
    if (openrouter_key().empty())
        SKIP("OPENROUTER_API_KEY is not set");
    std::vector<AgentHttpRequest> sent;
    std::vector<AgentUsage>       usage;
    AgentServicePtr               agent = live_agent(sent, usage);

    AgentRequest request;
    request.request_id                = "live-tool-call";
    request.session.instructions      = "You are a connectivity check. First call the workspace_inspect tool once, with sections "
                                        "[\"summary\"]. When its result arrives, reply with one short sentence saying you received it "
                                        "and whether an image came with it.";
    request.session.include_workspace = false;
    request.user_text                 = "Run the check.";
    REQUIRE(agent->start(request));
    const Turn first = run_until_settled(*agent);
    INFO("first reply: " << first.text << (first.error ? " error: " + first.error->code : ""));
    REQUIRE_FALSE(first.error);
    REQUIRE(first.call);
    CHECK(first.call->request.tool == "workspace_inspect");

    // The app's way of sending a picture: the result as text, the image in a
    // user message after it. The continuation also replays the response's own
    // output items, reasoning included.
    AgentToolResult result{first.call->call_id, "succeeded", R"({"state":"succeeded","result":{"summary":{"objects":1}}})"};
    result.image = std::make_shared<ToolImage>(ToolImage{"image/png", kPixel, 1, 1});
    REQUIRE(agent->continue_after_tool(result));
    const Turn second = run_until_settled(*agent);
    report(usage);
    INFO("second reply: " << second.text << (second.error ? " error: " + second.error->code + " " + second.error->message : ""));
    REQUIRE_FALSE(second.error);
    CHECK(second.completed);
    CHECK_FALSE(second.text.empty());
    REQUIRE(sent.size() == 2);
    CHECK(sent.back().body.find("input_image") != std::string::npos);
}

// Not what the app sends today: a probe for a later codec. A failure here is
// the answer "OpenRouter does not take an image inside a function result".
TEST_CASE("OpenRouter takes an image inside a function_call_output", "[agent][openrouter][live][.]")
{
    if (openrouter_key().empty())
        SKIP("OPENROUTER_API_KEY is not set");
    const ProviderProfile& profile = *profile_for("openrouter");
    const json tool{{"type", "function"},
                    {"name", "look"},
                    {"description", "Look at the picture."},
                    {"strict", true},
                    {"parameters", json{{"type", "object"}, {"properties", json::object()}, {"required", json::array()},
                                        {"additionalProperties", false}}}};
    const json input = json::array(
        {json{{"role", "user"}, {"content", "Look at the picture and say in one word what you see."}},
         json{{"type", "function_call"}, {"call_id", "call_probe"}, {"name", "look"}, {"arguments", "{}"}},
         json{{"type", "function_call_output"},
              {"call_id", "call_probe"},
              {"output", json::array({json{{"type", "input_text"}, {"text", "The picture:"}},
                                      json{{"type", "input_image"}, {"image_url", std::string("data:image/png;base64,") + kPixel}}})}}});
    AgentHttpRequest request;
    request.url     = agent_endpoint_override().empty() ? profile.endpoint : agent_endpoint_override();
    request.headers = {{"Authorization", "Bearer " + openrouter_key()}};
    request.headers.insert(request.headers.end(), profile.headers.begin(), profile.headers.end());
    request.body = json{{"model", profile.model}, {"store", false}, {"stream", true}, {"tools", json::array({tool})},
                        {"input", input}}
                       .dump();

    std::mutex              mutex;
    std::condition_variable done;
    std::string             stream;
    std::string             error;
    unsigned                status{0};
    bool                    finished{false};
    std::unique_ptr<IAgentHttpTransport> transport = make_openai_http_transport();
    REQUIRE(metered_post(*transport, kOpenRouter, std::move(request), [&](AgentHttpEvent event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (event.kind == AgentHttpEvent::Kind::Data) {
            stream += event.data;
            return;
        }
        status   = event.status;
        error    = event.error;
        finished = true;
        done.notify_all();
    }));
    {
        std::unique_lock<std::mutex> lock(mutex);
        REQUIRE(done.wait_for(lock, std::chrono::seconds(180), [&finished] { return finished; }));
    }
    INFO("status " << status << " " << error << "\n" << stream.substr(0, 2000));
    CHECK(status == 200);
    CHECK(stream.find("response.completed") != std::string::npos);
}

namespace {

// The Anthropic agent the app would build, its output capped at what these
// checks need so a run cannot pass the spend cap (Pricing::output_allowance).
AgentServicePtr live_anthropic_agent(std::vector<AgentHttpRequest>& sent, std::vector<AgentUsage>& usage,
                                     bool enforce_history_check = false)
{
    ProviderProfile profile = *profile_for("anthropic");
    profile.max_tokens      = static_cast<unsigned>(kAnthropic.output_allowance);
    ProviderListeners listeners;
    listeners.usage = [&usage](const AgentUsage& reported) { usage.push_back(reported); };
    return make_provider_agent(profile, anthropic_key(), "", agent_endpoint_override(),
                               std::make_unique<RecordingTransport>(sent, kAnthropic, enforce_history_check), std::move(listeners));
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

TEST_CASE("Anthropic honours the instructions with the project tools offered, strict on the projected schemas",
          "[agent][anthropic][live][.]")
{
    if (anthropic_key().empty())
        SKIP("ANTHROPIC_API_KEY is not set");
    std::vector<AgentHttpRequest> sent;
    std::vector<AgentUsage>       usage;
    AgentServicePtr               agent = live_anthropic_agent(sent, usage);

    AgentRequest request;
    request.request_id                = "live-anthropic-instructions";
    request.session.instructions      = "You are a connectivity check. Whatever the user writes, reply with exactly the word "
                                        "PINEAPPLE and nothing else. Do not call any tool.";
    request.session.include_workspace = false;
    request.user_text                 = "Hello, are you there?";
    REQUIRE(agent->start(request));
    const Turn turn = run_until_settled(*agent);
    report(usage);

    INFO("reply: " << turn.text << (turn.error ? " error: " + turn.error->code + " " + turn.error->message : ""));
    // Accepted at all: 28 tools on projected schemas, the closed ones strict.
    REQUIRE_FALSE(turn.error);
    REQUIRE(turn.completed);
    const json body = json::parse(sent.front().body);
    CHECK(body["tools"].size() == 28);
    CHECK(std::any_of(body["tools"].begin(), body["tools"].end(), [](const json& tool) { return tool["strict"] == true; }));
    for (const char* keyword : {"\"minimum\":", "\"maximum\":", "\"minLength\":", "\"maxLength\":", "\"maxItems\":"})
        CHECK(body["tools"].dump().find(keyword) == std::string::npos);
    CHECK(turn.text.find("PINEAPPLE") != std::string::npos);
    CHECK(usage.size() == 1);
}

TEST_CASE("Anthropic makes a tool call and sees the image inside its tool_result", "[agent][anthropic][live][.]")
{
    if (anthropic_key().empty())
        SKIP("ANTHROPIC_API_KEY is not set");
    std::vector<AgentHttpRequest> sent;
    std::vector<AgentUsage>       usage;
    AgentServicePtr               agent = live_anthropic_agent(sent, usage);

    AgentRequest request;
    request.request_id                = "live-anthropic-tool-call";
    request.session.instructions      = "You are a connectivity check. First call the workspace_inspect tool once, with sections "
                                        "[\"summary\"]. Its result comes with an image. When the result arrives, reply with one word: "
                                        "the colour of that image.";
    request.session.include_workspace = false;
    request.user_text                 = "Run the check.";
    REQUIRE(agent->start(request));
    const Turn first = run_until_settled(*agent);
    INFO("first reply: " << first.text << (first.error ? " error: " + first.error->code + " " + first.error->message : ""));
    REQUIRE_FALSE(first.error);
    REQUIRE(first.call);
    CHECK(first.call->request.tool == "workspace_inspect");

    // Anthropic's way of sending a picture: inside the tool_result. The
    // continuation also replays the response's thinking block verbatim.
    AgentToolResult result{first.call->call_id, "succeeded", R"({"state":"succeeded","result":{"summary":{"objects":1}}})"};
    result.image = std::make_shared<ToolImage>(ToolImage{"image/png", kRed, 16, 16});
    REQUIRE(agent->continue_after_tool(result));
    const Turn second = run_until_settled(*agent);
    report(usage);
    INFO("second reply: " << second.text << (second.error ? " error: " + second.error->code + " " + second.error->message : ""));
    REQUIRE_FALSE(second.error);
    CHECK(second.completed);
    CHECK(lowercase(second.text).find("red") != std::string::npos);
    REQUIRE(sent.size() == 2);
    const json messages = json::parse(sent.back().body)["messages"];
    CHECK(messages[messages.size() - 2]["role"] == "assistant");
    CHECK(messages.back()["content"][0]["type"] == "tool_result");
}

namespace {

// An earlier turn's call as the host replays it, with an id OpenAI minted.
AgentConversationContext openai_minted_call()
{
    AgentConversationContext call;
    call.call_id        = "call_Ab3dE5fG7hI9jK1lM3nO5pQ7";
    call.tool           = "workspace_inspect";
    call.arguments_json = R"({"sections":["summary"]})";
    call.output_json    = R"({"state":"succeeded","result":{"summary":{"objects":1}}})";
    return call;
}

bool has_block(const json& messages, const std::string& type)
{
    for (const json& message : messages)
        if (message["content"].is_array())
            for (const json& block : message["content"])
                if (block.value("type", "") == type)
                    return true;
    return false;
}

} // namespace

TEST_CASE("Anthropic accepts a turn that replays its thinking with a fired skill still in place, an OpenAI call id in its history",
          "[agent][anthropic][live][.]")
{
    if (anthropic_key().empty())
        SKIP("ANTHROPIC_API_KEY is not set");
    std::vector<AgentHttpRequest> sent;
    std::vector<AgentUsage>       usage;
    AgentServicePtr               agent = live_anthropic_agent(sent, usage, /*enforce_history_check=*/true);

    AgentRequest request;
    request.request_id                = "live-anthropic-history";
    request.session.instructions      = "You are a connectivity check. First call the workspace_inspect tool once, with sections "
                                        "[\"summary\"]. When its result arrives, reply with the single word: checked.";
    request.session.include_workspace = false;
    request.conversation              = {{"user", "Is anything on the plate?"}, openai_minted_call(), {"assistant", "One object."}};
    request.user_text                 = "Run the check.";
    request.skills = {AgentSkillUse{"file-loaded", "A file was loaded. This note is part of the check; act on the instructions.",
                                    nullptr, "file_loaded"}};
    REQUIRE(agent->start(request));
    const Turn first = run_until_settled(*agent);
    INFO("first reply: " << first.text << (first.error ? " error: " + first.error->code + " " + first.error->message : ""));
    REQUIRE_FALSE(first.error);
    REQUIRE(first.call);

    REQUIRE(agent->continue_after_tool({first.call->call_id, "succeeded", R"({"state":"succeeded","result":{"summary":{"objects":1}}})"}));
    const Turn second = run_until_settled(*agent);
    report(usage);
    INFO("second reply: " << second.text << (second.error ? " error: " + second.error->code + " " + second.error->message : ""));
    // Accepted under an enforced history check: the history did not change.
    REQUIRE_FALSE(second.error);
    CHECK(second.completed);
    REQUIRE(sent.size() == 2);
    const json messages = json::parse(sent.back().body)["messages"];
    // What the check covered: the skill, still in place, before replayed
    // thinking. Without a thinking block in the first response there was
    // nothing for the check to reject.
    const bool skill_in_place = sent.back().body.find("A file was loaded.") != std::string::npos;
    const bool thinking_replayed = has_block(messages, "thinking") || has_block(messages, "redacted_thinking");
    WARN("history check: skill in place " << skill_in_place << ", thinking replayed " << thinking_replayed);
    CHECK(skill_in_place);
    CHECK(sent.back().body.find("call_Ab3dE5fG7hI9jK1lM3nO5pQ7") != std::string::npos);
}

TEST_CASE("Anthropic writes a title from a conversation with a replayed call, or one that opens on the assistant",
          "[agent][anthropic][live][.]")
{
    if (anthropic_key().empty())
        SKIP("ANTHROPIC_API_KEY is not set");
    const auto title = [](std::vector<AgentConversationContext> conversation) {
        std::vector<AgentHttpRequest> sent;
        std::vector<AgentUsage>       usage;
        AgentServicePtr               agent = live_anthropic_agent(sent, usage);
        AgentRequest                  request;
        request.request_id   = "live-anthropic-title";
        request.purpose      = AgentRequest::Purpose::ConversationTitle;
        request.conversation = std::move(conversation);
        REQUIRE(agent->start(request));
        const Turn turn = run_until_settled(*agent);
        report(usage);
        INFO("title: " << turn.text << (turn.error ? " error: " + turn.error->code + " " + turn.error->message : ""));
        CHECK_FALSE(turn.error);
        CHECK(turn.completed);
        CHECK_FALSE(turn.text.empty());
        REQUIRE(sent.size() == 1);
        return json::parse(sent.front().body);
    };
    // A tool exchange with an OpenAI id and no tools declared, ending on the
    // assistant, so the codec adds "Write the title.".
    const json replayed = title({{"user", "Help me print a backpack frame."}, openai_minted_call(), {"assistant", "Use five walls."}});
    CHECK_FALSE(replayed.contains("tools"));
    CHECK(replayed["messages"].back()["content"].dump().find("Write the title.") != std::string::npos);
    // A conversation whose first message is the assistant's.
    const json opening = title({{"assistant", "Hello! Load a model to begin."}, {"user", "A phone stand."}});
    CHECK(opening["messages"][0]["role"] == "assistant");
}

namespace {

ModelListResult fetch_now(const ProviderProfile& profile, const std::string& key)
{
    std::mutex              mutex;
    std::condition_variable answered;
    std::optional<ModelListResult> result;
    fetch_model_list(profile, key, agent_endpoint_override(), std::make_shared<std::atomic<bool>>(false),
                     [&](ModelListResult got) {
                         std::lock_guard<std::mutex> lock(mutex);
                         result = std::move(got);
                         answered.notify_one();
                     });
    std::unique_lock<std::mutex> lock(mutex);
    answered.wait_for(lock, std::chrono::seconds(60), [&] { return result.has_value(); });
    REQUIRE(result.has_value());
    return std::move(*result);
}

Turn one_word(const ProviderProfile& profile, const std::string& key, const std::string& model, const Pricing& pricing,
              std::vector<AgentHttpRequest>& sent)
{
    AgentServicePtr agent = make_provider_agent(profile, key, model, agent_endpoint_override(),
                                                std::make_unique<RecordingTransport>(sent, pricing));
    AgentRequest request;
    request.request_id                = "live-model-choice";
    request.session.instructions      = "You are a connectivity check. Reply with exactly the word READY and nothing else. "
                                        "Do not call any tool.";
    request.session.include_workspace = false;
    request.user_text                 = "Are you there?";
    REQUIRE(agent->start(request));
    return run_until_settled(*agent);
}

} // namespace

// The composer's pickers, live and small: each provider with a key lists its
// models, answers once on a model other than its default at the lowest
// thinking level it offers, then once on its default, as a chat switched
// between two turns would. Two one-word replies per provider.
TEST_CASE("each provider lists its models and answers on a chosen model and thinking level", "[agent][models][live][.]")
{
    struct Live { const char* provider; std::string key; const Pricing* pricing; };
    const Live providers[] = {{"openai", environment("OPENAI_API_KEY"), &kOpenAI},
                              {"anthropic", anthropic_key(), &kAnthropic},
                              {"openrouter", openrouter_key(), &kOpenRouter}};
    int tried = 0;
    for (const Live& live : providers) {
        if (live.key.empty())
            continue;
        ++tried;
        CAPTURE(live.provider);
        const ProviderProfile& row = *profile_for(live.provider);

        const ModelListResult list = fetch_now(row, live.key);
        INFO("list error: " << list.error);
        REQUIRE(list.ok);
        REQUIRE_FALSE(list.models.empty());
        std::cerr << "LIVE MODELS " << live.provider << ": " << list.models.size() << " models\n";
        for (std::size_t i = 0; i < list.models.size() && i < 12; ++i) {
            std::string efforts;
            for (const std::string& effort : list.models[i].efforts)
                efforts += (efforts.empty() ? "" : ",") + effort;
            std::cerr << "  " << list.models[i].id << " (" << list.models[i].label << ") [" << efforts << "]\n";
        }
        const bool lists_default = std::any_of(list.models.begin(), list.models.end(),
                                               [&](const ModelOption& model) { return model.id == row.model; });
        CHECK(lists_default);

        // The smallest other model that takes a thinking level.
        const ModelOption* chosen = nullptr;
        for (const char* size : {"nano", "haiku", "mini", ""})
            for (const ModelOption& model : list.models)
                if (!chosen && model.id != row.model && !model.efforts.empty() &&
                    model.id.find(size) != std::string::npos)
                    chosen = &model;
        REQUIRE(chosen != nullptr);
        std::cerr << "LIVE CHOSEN " << live.provider << ": " << chosen->id << " at " << chosen->efforts.front() << "\n";

        std::vector<AgentHttpRequest> sent;
        ProviderProfile with_effort = row;
        with_effort.effort          = chosen->efforts.front();
        const Turn first = one_word(with_effort, live.key, chosen->id, *live.pricing, sent);
        INFO("first: " << first.text << (first.error ? " error: " + first.error->code + " " + first.error->message : ""));
        REQUIRE_FALSE(first.error);
        CHECK(first.completed);
        CHECK(first.text.find("READY") != std::string::npos);

        // Switched back to the default between turns, with no level.
        const Turn second = one_word(row, live.key, row.model, *live.pricing, sent);
        INFO("second: " << second.text << (second.error ? " error: " + second.error->code + " " + second.error->message : ""));
        REQUIRE_FALSE(second.error);
        CHECK(second.completed);
        REQUIRE(sent.size() == 2);
        CHECK(json::parse(sent[0].body)["model"] == chosen->id);
        CHECK(json::parse(sent[1].body)["model"] == row.model);
    }
    if (tried == 0)
        SKIP("no provider key is set");
}
