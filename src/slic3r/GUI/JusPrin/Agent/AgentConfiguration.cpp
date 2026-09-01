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
constexpr const char* kSecretService = "JusPrin Agent OpenAI";
constexpr const char* kSecretUser = "openai_api_key";

std::string configured_key()
{
    if (const char* key = std::getenv("OPENAI_API_KEY"); key != nullptr && *key != '\0')
        return key;
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk())
        return {};
    wxString username;
    wxSecretValue value;
    if (!store.Load(kSecretService, username, value) || !value.IsOk())
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

AgentRuntime load_agent_runtime(const AppConfig* config)
{
    AgentRuntime runtime;
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
    openai.api_key = configured_key();
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

bool save_openai_api_key(const std::string& key)
{
    if (key.empty())
        return false;
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk())
        return false;
    return store.Save(kSecretService, kSecretUser, wxSecretValue(wxString::FromUTF8(key)));
}

bool delete_openai_api_key()
{
    wxSecretStore store = wxSecretStore::GetDefault();
    return store.IsOk() && store.Delete(kSecretService);
}

} // namespace Slic3r::GUI::JusPrin::Agent
