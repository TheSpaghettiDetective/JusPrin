#include "AgentConfiguration.hpp"

#include "OpenAIResponsesAgent.hpp"
#include "ProviderFactory.hpp"
#include "libslic3r/AppConfig.hpp"

#include <boost/log/trivial.hpp>

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
    AgentSettings settings;
    settings.enabled       = configured_true(config, "enabled");
    settings.cloud_consent = configured_true(config, "cloud_consent");
    if (config != nullptr) {
        settings.provider = config->get(kSection, "provider");
        settings.model    = config->get(kSection, "model");
    }
    settings.api_key = load_provider_api_key(config, settings.provider.empty() ? "openai" : settings.provider);
    ProviderListeners listeners;
    // Refused tool calls never reach a card or the activity list; the log is
    // where a wrong call can be read afterwards.
    listeners.refusal = [](const std::string& tool, const std::string& arguments, const std::string& code) {
        BOOST_LOG_TRIVIAL(warning) << "Agent tool call refused (" << code << "): " << tool << " " << arguments.substr(0, 1024);
    };
    AgentRuntime runtime = resolve_agent_runtime(settings, agent_endpoint_override(), &make_openai_http_transport, std::move(listeners));
    runtime.setup        = make_agent_setup(config);
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
    return std::make_shared<ProviderKeySetup>(&make_openai_http_transport, std::move(commit), agent_endpoint_override());
}

} // namespace Slic3r::GUI::JusPrin::Agent
