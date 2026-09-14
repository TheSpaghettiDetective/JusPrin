#include "AgentConfiguration.hpp"

#include "OpenAIResponsesAgent.hpp"
#include "libslic3r/AppConfig.hpp"

#include <cstdlib>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

constexpr const char* kSection = "jusprin_agent";

std::string api_key_item_for(const std::string& provider) { return provider + "_api_key"; }

bool configured_true(const AppConfig* config, const char* key)
{
    if (config == nullptr)
        return false;
    const std::string value = config->get(kSection, key);
    return value == "true" || value == "1";
}

} // namespace

std::string load_provider_api_key(const AppConfig* config, const std::string& provider)
{
    return config == nullptr ? std::string() : config->get(kSection, api_key_item_for(provider));
}

AgentRuntime load_agent_runtime(AppConfig* config)
{
    AgentRuntime runtime;
    runtime.setup = make_agent_setup(config);
    runtime.provider = config == nullptr ? std::string() : config->get(kSection, "provider");
    if (runtime.provider.empty())
        runtime.provider = "openai";

    if (!configured_true(config, "enabled")) {
        runtime.unavailable_reason = "The Agent is not enabled.";
        return runtime;
    }
    if (runtime.provider != "openai") {
        runtime.unavailable_reason = "The configured Agent provider is not supported.";
        return runtime;
    }
    if (!configured_true(config, "cloud_consent")) {
        runtime.unavailable_reason = "Cloud Agent access requires explicit consent.";
        return runtime;
    }

    OpenAIResponsesConfig openai;
    openai.api_key = load_provider_api_key(config, runtime.provider);
    if (config != nullptr && !config->get(kSection, "model").empty())
        openai.model = config->get(kSection, "model");
    if (const char* endpoint = std::getenv("JUSPRIN_OPENAI_ENDPOINT"); endpoint != nullptr && *endpoint != '\0')
        openai.endpoint = endpoint;
    if (openai.api_key.empty()) {
        runtime.unavailable_reason = "No OpenAI API key is configured.";
        return runtime;
    }
    runtime.service = std::make_unique<OpenAIResponsesAgent>(std::move(openai), make_openai_http_transport());
    runtime.availability = runtime.service->ready() ? AgentAvailability::Ready : AgentAvailability::Unavailable;
    if (runtime.availability == AgentAvailability::Unavailable)
        runtime.unavailable_reason = "The OpenAI Agent could not be initialized.";
    return runtime;
}

AgentSetupServicePtr make_agent_setup(AppConfig* config)
{
    // Committing is the only part of setup that touches the machine, so it
    // lives here rather than in the GUI-free probe. Reaching this point means
    // the user read what the dock says about the provider billing them and
    // about the key staying on this machine, and chose to continue: that is
    // the cloud consent load_agent_runtime() requires on the next launch.
    auto commit = [config](const SetupCredentials& credentials) {
        if (config == nullptr || credentials.provider.empty() || credentials.api_key.empty())
            return false;
        config->set(kSection, api_key_item_for(credentials.provider), credentials.api_key);
        config->set(kSection, "provider", credentials.provider);
        config->set(kSection, "enabled", "true");
        config->set(kSection, "cloud_consent", "true");
        if (!credentials.model.empty())
            config->set(kSection, "model", credentials.model);
        // Written now, not at the next routine save, so a crash after setup
        // does not lose a key the user was just told is stored.
        config->save();
        return true;
    };
    std::string endpoint_override;
    if (const char* endpoint = std::getenv("JUSPRIN_OPENAI_ENDPOINT"); endpoint != nullptr && *endpoint != '\0')
        endpoint_override = endpoint;
    return std::make_shared<ProviderKeySetup>(&make_openai_http_transport, std::move(commit),
                                              std::move(endpoint_override));
}

} // namespace Slic3r::GUI::JusPrin::Agent
