#include "ProviderFactory.hpp"
#include "AnthropicCodec.hpp"
#include "ResponsesCodec.hpp"

#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

// A picture or file is sent in one of two shapes: a data URL (Responses) or a
// base64 source with its bytes under "data" (Anthropic).
json without_file_bytes(const json& value)
{
    const auto note = [](const std::string& bytes) { return "[" + std::to_string(bytes.size()) + " characters left out]"; };
    if (value.is_string()) {
        const std::string& text = value.get_ref<const std::string&>();
        return text.rfind("data:", 0) == 0 && text.find(";base64,") != std::string::npos ? json(note(text)) : value;
    }
    if (value.is_array()) {
        json copy = json::array();
        for (const json& child : value)
            copy.push_back(without_file_bytes(child));
        return copy;
    }
    if (!value.is_object())
        return value;
    const bool base64 = value.value("type", json()) == "base64";
    json       copy   = json::object();
    for (auto it = value.begin(); it != value.end(); ++it)
        copy[it.key()] = base64 && it.key() == "data" && it.value().is_string() ? json(note(it.value().get<std::string>())) :
                                                                                  without_file_bytes(it.value());
    return copy;
}

void keep_newest(const std::filesystem::path& directory, std::size_t keep)
{
    std::error_code                    ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        files.push_back(it->path());
    if (files.size() <= keep)
        return;
    // The names start with the time, so name order is age order.
    std::sort(files.begin(), files.end());
    for (std::size_t i = 0; i + keep < files.size(); ++i)
        std::filesystem::remove(files[i], ec);
}

std::unique_ptr<IProviderCodec> codec_for(ProviderFamily family)
{
    switch (family) {
    case ProviderFamily::Responses: return std::make_unique<ResponsesCodec>();
    case ProviderFamily::Anthropic: return std::make_unique<AnthropicCodec>();
    }
    return nullptr;
}

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

} // namespace

std::function<void(const SentRequest&)> request_log_writer(std::filesystem::path directory, std::size_t keep)
{
    return [directory = std::move(directory), keep](const SentRequest& sent) {
        // Two agents can post in the same millisecond: a title beside a reply.
        static std::atomic<unsigned> counter{0};
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        char       name[40];
        std::snprintf(name, sizeof name, "%013lld-%06u.json", static_cast<long long>(now.count()), ++counter % 1000000u);
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        std::ofstream file(directory / name, std::ios::binary);
        file << json{{"provider", sent.provider},
                     {"chat", sent.chat_key},
                     {"turn", sent.turn_id},
                     {"sequence", sent.sequence},
                     {"body", without_file_bytes(sent.body)}}
                    .dump();
        file.close();
        if (!file) {
            BOOST_LOG_TRIVIAL(warning) << "Agent request log not written in " << directory.string();
            return;
        }
        keep_newest(directory, keep);
    };
}

AgentServicePtr make_provider_agent(const ProviderProfile&               profile,
                                    const std::string&                   api_key,
                                    const std::string&                   model,
                                    const std::string&                   endpoint,
                                    std::unique_ptr<IAgentHttpTransport> transport,
                                    ProviderListeners                    listeners)
{
    ProviderAgentConfig config;
    config.profile = profile;
    if (!model.empty())
        config.profile.model = model;
    if (!endpoint.empty())
        config.profile.endpoint = endpoint;
    config.api_key          = api_key;
    config.usage_listener   = std::move(listeners.usage);
    config.refusal_listener = std::move(listeners.refusal);
    config.request_listener = std::move(listeners.request);
    return std::make_unique<ProviderAgent>(std::move(config), codec_for(profile.family), std::move(transport));
}

std::string agent_endpoint_override()
{
    if (std::string endpoint = environment("JUSPRIN_AGENT_ENDPOINT"); !endpoint.empty())
        return endpoint;
    return environment("JUSPRIN_OPENAI_ENDPOINT");
}

AgentRuntime resolve_agent_runtime(const AgentSettings&                                         settings,
                                   const std::string&                                           endpoint_override,
                                   const std::function<std::unique_ptr<IAgentHttpTransport>()>& transport,
                                   ProviderListeners                                            listeners)
{
    AgentRuntime runtime;
    runtime.provider = settings.provider.empty() ? "openai" : settings.provider;
    if (!settings.enabled) {
        runtime.unavailable_reason = "The Agent is not enabled.";
        return runtime;
    }
    const ProviderProfile* profile = profile_for(runtime.provider);
    if (profile == nullptr) {
        runtime.unavailable_reason = "The configured Agent provider is not supported.";
        return runtime;
    }
    if (!settings.cloud_consent) {
        runtime.unavailable_reason = "Cloud Agent access requires explicit consent.";
        return runtime;
    }
    if (settings.api_key.empty()) {
        runtime.unavailable_reason = profile->account_sign_in ? "Not signed in to a JusPrin account." :
                                                                "No " + profile->display_name + " API key is configured.";
        return runtime;
    }
    if (profile->account_sign_in) {
        const std::optional<ProviderProfile> served =
            account_profile(*profile, endpoint_override.empty() ? settings.endpoint : endpoint_override, settings.format);
        if (!served) {
            runtime.unavailable_reason = "Sign in to your JusPrin account again.";
            return runtime;
        }
        runtime.service = make_provider_agent(*served, settings.api_key, settings.model, {}, transport(), std::move(listeners));
    } else {
        runtime.service = make_provider_agent(*profile, settings.api_key, settings.model, endpoint_override, transport(),
                                              std::move(listeners));
    }
    runtime.availability = runtime.service->ready() ? AgentAvailability::Ready : AgentAvailability::Unavailable;
    if (runtime.availability == AgentAvailability::Unavailable)
        runtime.unavailable_reason = "The " + profile->display_name + " Agent could not be initialized.";
    return runtime;
}

} // namespace Slic3r::GUI::JusPrin::Agent
