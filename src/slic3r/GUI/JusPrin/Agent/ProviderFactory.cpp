#include "ProviderFactory.hpp"
#include "AnthropicCodec.hpp"
#include "ResponsesCodec.hpp"

#include <cstdlib>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

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
        runtime.unavailable_reason = "No " + profile->display_name + " API key is configured.";
        return runtime;
    }
    runtime.service = make_provider_agent(*profile, settings.api_key, settings.model, endpoint_override, transport(),
                                          std::move(listeners));
    runtime.availability = runtime.service->ready() ? AgentAvailability::Ready : AgentAvailability::Unavailable;
    if (runtime.availability == AgentAvailability::Unavailable)
        runtime.unavailable_reason = "The " + profile->display_name + " Agent could not be initialized.";
    return runtime;
}

} // namespace Slic3r::GUI::JusPrin::Agent
