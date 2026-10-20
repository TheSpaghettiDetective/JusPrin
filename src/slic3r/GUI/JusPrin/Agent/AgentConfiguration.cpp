#include "AgentConfiguration.hpp"

#include "AgentHost.hpp"
#include "ModelCatalogFetch.hpp"
#include "OpenAIResponsesAgent.hpp"
#include "ProviderFactory.hpp"
#include "libslic3r/AppConfig.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

constexpr const char* kSection = "jusprin_agent";

std::string api_key_item_for(const std::string& provider) { return provider + "_api_key"; }

ProviderListeners logging_listeners()
{
    ProviderListeners listeners;
    // Refused tool calls never reach a card or the activity list; the log is
    // where a wrong call can be read afterwards.
    listeners.refusal = [](const std::string& tool, const std::string& arguments, const std::string& code) {
        BOOST_LOG_TRIVIAL(warning) << "Agent tool call refused (" << code << "): " << tool << " " << arguments.substr(0, 1024);
    };
    return listeners;
}

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
    AgentRuntime runtime = resolve_agent_runtime(settings, agent_endpoint_override(), &make_openai_http_transport, logging_listeners());
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

namespace {

class AppConfigChatModelSource final : public IChatModelSource
{
public:
    AppConfigChatModelSource(AppConfig* config, std::function<void(std::function<void()>)> post)
        : m_config(config), m_post(std::move(post)), m_cancelled(std::make_shared<std::atomic<bool>>(false))
    {}
    // A list still on its way is dropped rather than delivered to a host
    // that is gone.
    ~AppConfigChatModelSource() override { *m_cancelled = true; }

    // Read each time: setup may have stored a key since the last turn.
    std::vector<std::string> configured_providers() const override
    {
        std::vector<std::string> providers;
        if (!configured_true(m_config, "enabled") || !configured_true(m_config, "cloud_consent"))
            return providers;
        for (const ProviderProfile& profile : provider_profiles())
            if (!load_provider_api_key(m_config, profile.id).empty())
                providers.push_back(profile.id);
        return providers;
    }

    std::string default_provider() const override
    {
        const std::string provider = m_config == nullptr ? std::string() : m_config->get(kSection, "provider");
        return provider.empty() ? "openai" : provider;
    }

    AgentServicePtr make_agent(const ChatModelChoice& choice) override
    {
        const ProviderProfile* row = profile_for(choice.provider);
        const std::string      key = load_provider_api_key(m_config, choice.provider);
        if (row == nullptr || key.empty())
            return nullptr;
        ProviderProfile profile = *row;
        profile.effort          = choice.effort;
        return make_provider_agent(profile, key, choice.model, agent_endpoint_override(), make_openai_http_transport(),
                                   logging_listeners());
    }

    void list_models(const std::string& provider, ModelsDone done) override
    {
        const ProviderProfile* profile = profile_for(provider);
        const std::string      key     = load_provider_api_key(m_config, provider);
        if (profile == nullptr || key.empty()) {
            done(false, {});
            return;
        }
        fetch_model_list(*profile, key, agent_endpoint_override(), m_cancelled,
                         [post = m_post, done = std::move(done), provider](ModelListResult result) {
                             if (!result.ok)
                                 BOOST_LOG_TRIVIAL(warning) << "Agent model list for " << provider << ": " << result.error;
                             post([done, result = std::move(result)]() mutable { done(result.ok, std::move(result.models)); });
                         });
    }

private:
    AppConfig*                                  m_config;
    std::function<void(std::function<void()>)>  m_post;
    std::shared_ptr<std::atomic<bool>>          m_cancelled;
};

} // namespace

std::shared_ptr<IChatModelSource> make_chat_model_source(AppConfig* config, std::function<void(std::function<void()>)> post)
{
    return std::make_shared<AppConfigChatModelSource>(config, std::move(post));
}

} // namespace Slic3r::GUI::JusPrin::Agent
