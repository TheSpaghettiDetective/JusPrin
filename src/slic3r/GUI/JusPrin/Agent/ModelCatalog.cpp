#include "ModelCatalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace Slic3r::GUI::JusPrin::Agent {

using nlohmann::json;

namespace {

// Every level the providers name, lowest first, which is the order the
// picker lists them in.
constexpr std::array<const char*, 7> kEffortOrder{"none", "minimal", "low", "medium", "high", "xhigh", "max"};

std::vector<std::string> in_effort_order(const std::vector<std::string>& efforts)
{
    std::vector<std::string> ordered;
    for (const char* level : kEffortOrder)
        if (std::find(efforts.begin(), efforts.end(), level) != efforts.end())
            ordered.emplace_back(level);
    return ordered;
}

bool starts_with(const std::string& text, const std::string& prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

// Anthropic: levels only where thinking is adaptive, since the request sends
// adaptive thinking with the level as effort.
ModelOption anthropic_model(const json& model)
{
    ModelOption option;
    option.id    = model.at("id").get<std::string>();
    option.label = model.value("display_name", option.id);
    const json capabilities = model.value("capabilities", json::object());
    const bool adaptive =
        capabilities.value("/thinking/types/adaptive/supported"_json_pointer, false);
    if (adaptive && capabilities.contains("effort")) {
        std::vector<std::string> efforts;
        for (const char* level : kEffortOrder)
            if (capabilities["effort"].value(json::json_pointer("/" + std::string(level) + "/supported"), false))
                efforts.emplace_back(level);
        option.efforts = in_effort_order(efforts);
    }
    return option;
}

ModelOption openrouter_model(const json& model)
{
    ModelOption option;
    option.id    = model.at("id").get<std::string>();
    option.label = model.value("name", option.id);
    if (!model.contains("reasoning") || !model["reasoning"].is_object())
        return option; // a model that does not reason
    const json& reasoning = model["reasoning"];
    std::vector<std::string> efforts;
    if (!reasoning.contains("supported_efforts"))
        return option; // reasons, with no level to choose
    if (reasoning["supported_efforts"].is_null()) {
        // Null means every level the gateway names.
        efforts.assign(kEffortOrder.begin(), kEffortOrder.end());
    } else {
        for (const json& level : reasoning["supported_efforts"])
            efforts.push_back(level.get<std::string>());
    }
    // A model whose reasoning is mandatory cannot be turned off.
    if (reasoning.value("mandatory", false))
        efforts.erase(std::remove(efforts.begin(), efforts.end(), "none"), efforts.end());
    option.efforts = in_effort_order(efforts);
    if (reasoning.contains("default_effort") && reasoning["default_effort"].is_string())
        option.default_effort = reasoning["default_effort"].get<std::string>();
    return option;
}

// OpenAI's list names every model the key can reach, speech, images and
// embeddings included, and says nothing about what each can do. These rules
// keep the text models a conversation can use and offer the three levels
// every reasoning model accepts.
bool openai_conversation_model(const std::string& id)
{
    const bool family = starts_with(id, "gpt-") || (id.size() > 1 && id[0] == 'o' && std::isdigit(static_cast<unsigned char>(id[1])));
    if (!family)
        return false;
    for (const char* other : {"audio", "realtime", "tts", "transcribe", "image", "search", "embedding", "moderation", "instruct"})
        if (id.find(other) != std::string::npos)
            return false;
    return true;
}

// GPT-5 and later, and the o-series, take reasoning effort. Some reject
// "none" or "minimal" (OpenAI's reasoning guide); every one takes these three.
bool openai_reasoning_model(const std::string& id)
{
    if (id[0] == 'o')
        return true;
    const std::string version = id.substr(4); // after "gpt-"
    return !version.empty() && std::isdigit(static_cast<unsigned char>(version[0])) && std::atoi(version.c_str()) >= 5;
}

// "gpt-5.4-mini" reads "GPT-5.4 mini"; "o4-mini" reads "o4 mini"; a dated
// snapshot keeps its date whole: "gpt-5.5-2026-04-23" reads "GPT-5.5 (2026-04-23)".
std::string openai_label(const std::string& id)
{
    std::string base = id, date;
    if (id.size() > 11) {
        const std::string tail = id.substr(id.size() - 11); // "-YYYY-MM-DD"
        if (tail[0] == '-' && tail[5] == '-' && tail[8] == '-' &&
            std::all_of(tail.begin() + 1, tail.end(), [](char c) { return c == '-' || std::isdigit(static_cast<unsigned char>(c)); })) {
            base = id.substr(0, id.size() - 11);
            date = tail.substr(1);
        }
    }
    std::string head = base, rest;
    const std::size_t family_end = starts_with(base, "gpt-") ? base.find('-', 4) : base.find('-');
    if (family_end != std::string::npos) {
        head = base.substr(0, family_end);
        rest = base.substr(family_end + 1);
        std::replace(rest.begin(), rest.end(), '-', ' ');
    }
    if (starts_with(head, "gpt-"))
        head = "GPT-" + head.substr(4);
    std::string label = rest.empty() ? head : head + " " + rest;
    return date.empty() ? label : label + " (" + date + ")";
}

} // namespace

std::string model_list_url(const ProviderProfile& profile, const std::string& endpoint_override)
{
    const std::string endpoint = endpoint_override.empty() ? profile.endpoint : endpoint_override;
    const std::size_t slash    = endpoint.rfind('/');
    const std::string base     = slash == std::string::npos ? endpoint : endpoint.substr(0, slash);
    if (profile.account_sign_in)
        return base + "/models";
    if (profile.family == ProviderFamily::Anthropic)
        return base + "/models?limit=1000";
    if (profile.id == "openrouter")
        return base + "/models?supported_parameters=tools";
    return base + "/models";
}

ModelListPage parse_model_list(const ProviderProfile& profile, const json& response)
{
    ModelListPage page;
    const json& data = response.at("data");
    if (profile.account_sign_in) {
        for (const json& model : data) {
            ModelOption option;
            option.id             = model.at("id").get<std::string>();
            option.label          = model.value("label", option.id);
            option.format         = model.at("format").get<std::string>();
            option.efforts        = model.value("efforts", std::vector<std::string>{});
            option.default_effort = model.value("default_effort", "");
            page.models.push_back(std::move(option));
        }
    } else if (profile.family == ProviderFamily::Anthropic) {
        for (const json& model : data)
            page.models.push_back(anthropic_model(model));
        if (response.value("has_more", false))
            page.next_after = response.value("last_id", "");
    } else if (profile.id == "openrouter") {
        for (const json& model : data) {
            const json parameters = model.value("supported_parameters", json::array());
            if (std::find(parameters.begin(), parameters.end(), "tools") != parameters.end())
                page.models.push_back(openrouter_model(model));
        }
    } else {
        std::vector<std::pair<long long, ModelOption>> dated;
        for (const json& model : data) {
            const std::string id = model.at("id").get<std::string>();
            if (!openai_conversation_model(id))
                continue;
            ModelOption option;
            option.id    = id;
            option.label = openai_label(id);
            if (openai_reasoning_model(id))
                option.efforts = {"low", "medium", "high"};
            dated.emplace_back(model.value("created", 0LL), std::move(option));
        }
        std::stable_sort(dated.begin(), dated.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto& [created, option] : dated)
            page.models.push_back(std::move(option));
    }
    return page;
}

} // namespace Slic3r::GUI::JusPrin::Agent
