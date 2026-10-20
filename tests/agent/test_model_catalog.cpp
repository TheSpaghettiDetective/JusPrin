// Each provider's model list, read into what the composer's model and
// thinking pickers offer. The answers are shaped like each provider's
// documented response.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ModelCatalog.hpp"

#include <nlohmann/json.hpp>

using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

std::vector<std::string> ids(const ModelListPage& page)
{
    std::vector<std::string> result;
    for (const ModelOption& model : page.models)
        result.push_back(model.id);
    return result;
}

json anthropic_effort(std::initializer_list<const char*> supported)
{
    json effort{{"supported", true}};
    for (const char* level : {"low", "medium", "high", "xhigh", "max"})
        effort[level] = json{{"supported", std::find_if(supported.begin(), supported.end(), [&](const char* s) {
                                               return std::string(s) == level;
                                           }) != supported.end()}};
    return effort;
}

} // namespace

TEST_CASE("each provider's list sits beside its request endpoint", "[agent][models]")
{
    CHECK(model_list_url(*profile_for("openai"), "") == "https://api.openai.com/v1/models");
    CHECK(model_list_url(*profile_for("anthropic"), "") == "https://api.anthropic.com/v1/models?limit=1000");
    CHECK(model_list_url(*profile_for("openrouter"), "") == "https://openrouter.ai/api/v1/models?supported_parameters=tools");
    // An endpoint override moves the list with it.
    CHECK(model_list_url(*profile_for("openai"), "http://127.0.0.1:9/v1/responses") == "http://127.0.0.1:9/v1/models");
}

TEST_CASE("Anthropic models offer their effort levels where thinking is adaptive", "[agent][models][anthropic]")
{
    const json response{
        {"data", json::array({
            json{{"id", "claude-opus-5-5"}, {"display_name", "Claude Opus 5.5"},
                 {"capabilities", json{{"thinking", json{{"supported", true},
                                                         {"types", json{{"enabled", json{{"supported", false}}},
                                                                        {"adaptive", json{{"supported", true}}}}}}},
                                       {"effort", anthropic_effort({"low", "medium", "high", "xhigh", "max"})}}}},
            // Effort without adaptive thinking: the request would send
            // adaptive thinking, so no level is offered.
            json{{"id", "claude-opus-4-5"}, {"display_name", "Claude Opus 4.5"},
                 {"capabilities", json{{"thinking", json{{"supported", true},
                                                         {"types", json{{"enabled", json{{"supported", true}}},
                                                                        {"adaptive", json{{"supported", false}}}}}}},
                                       {"effort", anthropic_effort({"low", "medium", "high"})}}}},
            json{{"id", "claude-haiku-4-5"}, {"display_name", "Claude Haiku 4.5"}},
        })},
        {"has_more", true},
        {"last_id", "claude-haiku-4-5"}};
    const ModelListPage page = parse_model_list(*profile_for("anthropic"), response);
    REQUIRE(page.models.size() == 3);
    CHECK(page.models[0].label == "Claude Opus 5.5");
    CHECK(page.models[0].efforts == std::vector<std::string>{"low", "medium", "high", "xhigh", "max"});
    CHECK(page.models[0].default_effort.empty());
    CHECK(page.models[1].efforts.empty());
    CHECK(page.models[2].efforts.empty());
    CHECK(page.next_after == "claude-haiku-4-5");
}

TEST_CASE("OpenRouter models take their levels, default and on/off from the reasoning object", "[agent][models][openrouter]")
{
    const json response{{"data", json::array({
        json{{"id", "anthropic/claude-sonnet-5.5"}, {"name", "Anthropic: Claude Sonnet 5.5"},
             {"supported_parameters", json::array({"tools", "reasoning"})},
             {"reasoning", json{{"supported_efforts", json::array({"high", "medium", "low", "none"})},
                                {"default_effort", "medium"}, {"mandatory", true}}}},
        json{{"id", "openai/gpt-5.4-mini"}, {"name", "OpenAI: GPT-5.4 mini"},
             {"supported_parameters", json::array({"tools", "reasoning"})},
             {"reasoning", json{{"supported_efforts", nullptr}, {"mandatory", false}}}},
        json{{"id", "deepseek/deepseek-v3"}, {"name", "DeepSeek V3"}, {"supported_parameters", json::array({"tools"})}},
        // Asked for already filtered; one that slipped through without
        // tools is still left out.
        json{{"id", "some/chat-only"}, {"name", "Chat only"}, {"supported_parameters", json::array({"temperature"})}},
    })}};
    const ModelListPage page = parse_model_list(*profile_for("openrouter"), response);
    CHECK(ids(page) == std::vector<std::string>{"anthropic/claude-sonnet-5.5", "openai/gpt-5.4-mini", "deepseek/deepseek-v3"});
    // Lowest first, and "none" dropped where reasoning cannot be turned off.
    CHECK(page.models[0].efforts == std::vector<std::string>{"low", "medium", "high"});
    CHECK(page.models[0].default_effort == "medium");
    CHECK(page.models[0].label == "Anthropic: Claude Sonnet 5.5");
    // Null means every level the gateway names.
    CHECK(page.models[1].efforts ==
          std::vector<std::string>{"none", "minimal", "low", "medium", "high", "xhigh", "max"});
    CHECK(page.models[2].efforts.empty());
    CHECK(page.next_after.empty());
}

TEST_CASE("OpenAI's list keeps the conversation models, newest first, with levels for reasoning models", "[agent][models][openai]")
{
    const json response{{"object", "list"}, {"data", json::array({
        json{{"id", "gpt-4.1"}, {"created", 100}, {"object", "model"}, {"owned_by", "openai"}},
        json{{"id", "gpt-5.4-mini"}, {"created", 300}},
        json{{"id", "o4-mini"}, {"created", 200}},
        json{{"id", "text-embedding-3-large"}, {"created", 400}},
        json{{"id", "gpt-4o-realtime-preview"}, {"created", 500}},
        json{{"id", "gpt-image-1"}, {"created", 600}},
        json{{"id", "whisper-1"}, {"created", 50}},
        json{{"id", "omni-moderation-latest"}, {"created", 60}},
        json{{"id", "gpt-6-sol"}, {"created", 700}},
        json{{"id", "gpt-5.5-2026-04-23"}, {"created", 250}},
    })}};
    const ModelListPage page = parse_model_list(*profile_for("openai"), response);
    CHECK(ids(page) == std::vector<std::string>{"gpt-6-sol", "gpt-5.4-mini", "gpt-5.5-2026-04-23", "o4-mini", "gpt-4.1"});
    CHECK(page.models[0].label == "GPT-6 sol");
    CHECK(page.models[1].label == "GPT-5.4 mini");
    CHECK(page.models[2].label == "GPT-5.5 (2026-04-23)");
    CHECK(page.models[3].label == "o4 mini");
    CHECK(page.models[4].label == "GPT-4.1");
    // GPT-5 and later, and the o-series, take the three levels.
    for (int i : {0, 1, 2, 3})
        CHECK(page.models[i].efforts == std::vector<std::string>{"low", "medium", "high"});
    CHECK(page.models[4].efforts.empty());
}
