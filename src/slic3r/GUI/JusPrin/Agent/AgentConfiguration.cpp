#include "AgentConfiguration.hpp"

#include "DeterministicMockAgent.hpp"
#include "OpenAIResponsesAgent.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/secretstore.h>

#include <cstdlib>
#include <iostream>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

constexpr const char* kSection = "jusprin_agent";

// Earlier builds stored the single OpenAI key under these exact names; the
// OpenAI provider keeps them so an already-configured machine keeps working.
std::string secret_service_for(const std::string& provider)
{
    return provider == "openai" ? "JusPrin Agent OpenAI" : "JusPrin Agent " + provider;
}

std::string secret_user_for(const std::string& provider) { return provider + "_api_key"; }

std::string configured_key(const std::string& provider)
{
    if (provider == "openai")
        if (const char* key = std::getenv("OPENAI_API_KEY"); key != nullptr && *key != '\0')
            return key;
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk())
        return {};
    wxString username;
    wxSecretValue value;
    if (!store.Load(secret_service_for(provider), username, value) || !value.IsOk())
        return {};
    return std::string(static_cast<const char*>(value.GetData()), value.GetSize());
}

bool configured_true(const AppConfig* config, const char* key)
{
    if (config == nullptr)
        return false;
    const std::string value = config->get(kSection, key);
    return value == "true" || value == "1";
}

} // namespace

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
    if (runtime.provider == "mock") {
        runtime.service = std::make_unique<DeterministicMockAgent>();
        runtime.availability = AgentAvailability::Ready;
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
    openai.api_key = configured_key(runtime.provider);
    if (config != nullptr && !config->get(kSection, "model").empty())
        openai.model = config->get(kSection, "model");
    if (const char* endpoint = std::getenv("JUSPRIN_OPENAI_ENDPOINT"); endpoint != nullptr && *endpoint != '\0')
        openai.endpoint = endpoint;
    if (std::getenv("JUSPRIN_AGENT_RECORD_USAGE") != nullptr) {
        openai.usage_listener = [](std::uint64_t input, std::uint64_t output, std::uint64_t total) {
            std::cerr << "JUSPRIN LIVE USAGE provider=openai input_tokens=" << input
                      << " output_tokens=" << output << " total_tokens=" << total << '\n';
        };
    }
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

bool save_provider_api_key(const std::string& provider, const std::string& key)
{
    if (provider.empty() || key.empty())
        return false;
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk())
        return false;
    return store.Save(secret_service_for(provider), secret_user_for(provider), wxSecretValue(wxString::FromUTF8(key)));
}

bool delete_provider_api_key(const std::string& provider)
{
    wxSecretStore store = wxSecretStore::GetDefault();
    return store.IsOk() && store.Delete(secret_service_for(provider));
}

AgentSetupServicePtr make_agent_setup(AppConfig* config)
{
    // Committing is the only part of setup that touches the machine, so it
    // lives here rather than in the GUI-free probe. Reaching this point means
    // the user read what the dock says about the provider billing them and
    // about the key staying on this machine, and chose to continue: that is
    // the cloud consent load_agent_runtime() requires on the next launch.
    auto commit = [config](const SetupCredentials& credentials) {
        if (!save_provider_api_key(credentials.provider, credentials.api_key))
            return false;
        if (config == nullptr)
            return false;
        config->set(kSection, "provider", credentials.provider);
        config->set(kSection, "enabled", "true");
        config->set(kSection, "cloud_consent", "true");
        if (!credentials.model.empty())
            config->set(kSection, "model", credentials.model);
        return true;
    };
    std::string endpoint_override;
    if (const char* endpoint = std::getenv("JUSPRIN_OPENAI_ENDPOINT"); endpoint != nullptr && *endpoint != '\0')
        endpoint_override = endpoint;
    return std::make_shared<ProviderKeySetup>(&make_openai_http_transport, std::move(commit),
                                              std::move(endpoint_override));
}

} // namespace Slic3r::GUI::JusPrin::Agent
