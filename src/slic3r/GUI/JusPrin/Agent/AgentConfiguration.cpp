#include "AgentConfiguration.hpp"

#include "AgentHost.hpp"
#include "CloudSignInMachine.hpp"
#include "ModelCatalogFetch.hpp"
#include "OpenAIResponsesAgent.hpp"
#include "ProviderFactory.hpp"
#include "libslic3r/AppConfig.hpp"

#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

constexpr const char* kSection = "jusprin_agent";

// JusPrin v1 kept the server it signs in to here; a self-hosted or staging
// server named there is the one JusPrin 2 signs in to as well.
constexpr const char* kServerSection = "jusprin_server";

std::string api_key_item_for(const std::string& provider) { return provider + "_api_key"; }
// An account provider keeps the account's email and the wire format of each
// of its models beside the token.
std::string account_item_for(const std::string& provider) { return provider + "_account"; }
std::string formats_item_for(const std::string& provider) { return provider + "_model_formats"; }

// Where an account provider's gateway is: on the server v1's setting names,
// or the row's own.
std::string cloud_gateway(const AppConfig* config, const ProviderProfile& row)
{
    std::string base = config == nullptr ? std::string() : config->get(kServerSection, "base_url");
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    return base.empty() ? row.endpoint : base + "/ent/jusprin/v2/responses";
}

// The endpoint a provider is reached at, where it is not the row's: the
// developer's override for any provider, or an account provider's gateway.
std::string endpoint_for(const AppConfig* config, const ProviderProfile& profile)
{
    if (std::string override_endpoint = agent_endpoint_override(); !override_endpoint.empty())
        return override_endpoint;
    return profile.account_sign_in ? cloud_gateway(config, profile) : std::string();
}

std::vector<std::pair<std::string, std::string>> stored_formats(const AppConfig* config, const std::string& provider)
{
    std::vector<std::pair<std::string, std::string>> formats;
    if (config == nullptr)
        return formats;
    const nlohmann::json stored = nlohmann::json::parse(config->get(kSection, formats_item_for(provider)), nullptr, false);
    if (stored.is_object())
        for (const auto& [model, format] : stored.items())
            if (format.is_string())
                formats.emplace_back(model, format.get<std::string>());
    return formats;
}

void store_formats(AppConfig* config, const std::string& provider, const std::vector<std::pair<std::string, std::string>>& formats)
{
    nlohmann::json stored = nlohmann::json::object();
    for (const auto& [model, format] : formats)
        stored[model] = format;
    config->set(kSection, formats_item_for(provider), stored.dump());
}

std::string stored_format(const AppConfig* config, const std::string& provider, const std::string& model)
{
    for (const auto& [listed, format] : stored_formats(config, provider))
        if (listed == model)
            return format;
    return {};
}

// Deletes a provider's stored key, and an account's email and formats with
// it. A chat without a choice of its own talks to the recorded provider; once
// its key is gone, that is the first provider that still has one.
bool remove_provider_key(AppConfig* config, const std::string& provider)
{
    if (config == nullptr || config->get(kSection, api_key_item_for(provider)).empty())
        return false;
    config->erase(kSection, api_key_item_for(provider));
    config->erase(kSection, account_item_for(provider));
    config->erase(kSection, formats_item_for(provider));
    if (config->get(kSection, "provider") == provider)
        for (const ProviderProfile& profile : provider_profiles())
            if (!config->get(kSection, api_key_item_for(profile.id)).empty()) {
                config->set(kSection, "provider", profile.id);
                break;
            }
    // Saved now, as a stored key is: the person was just told it is gone.
    config->save();
    return true;
}

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
    if (const ProviderProfile* profile = profile_for(settings.provider); profile != nullptr && profile->account_sign_in) {
        settings.format   = stored_format(config, profile->id, settings.model.empty() ? profile->model : settings.model);
        settings.endpoint = cloud_gateway(config, *profile);
    }
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
    ProviderKeySetup::KeyStore keys;
    keys.load   = [config](const std::string& provider) { return load_provider_api_key(config, provider); };
    keys.remove = [config](const std::string& provider) { return remove_provider_key(config, provider); };
    return std::make_shared<ProviderKeySetup>(&make_openai_http_transport, std::move(commit), std::move(keys),
                                              agent_endpoint_override(), ProviderKeySetup::Clock{},
                                              make_cloud_sign_in(config));
}

CloudSignInPtr make_cloud_sign_in(AppConfig* config)
{
    const ProviderProfile& row     = account_provider();
    const std::string      gateway = endpoint_for(config, row);
    CloudAccountStore      store;
    store.load = [config, id = row.id]() -> std::optional<CloudAccountRecord> {
        if (config == nullptr)
            return std::nullopt;
        CloudAccountRecord record;
        record.token   = config->get(kSection, api_key_item_for(id));
        record.email   = config->get(kSection, account_item_for(id));
        record.formats = stored_formats(config, id);
        return record;
    };
    // Signing in is choosing the account as the Agent's provider, after the
    // account row told the person what goes to JusPrin and on to the model's
    // provider: the same choices and consent a verified key records.
    store.save = [config, id = row.id](const CloudAccountRecord& record) {
        if (config == nullptr)
            return false;
        config->set(kSection, api_key_item_for(id), record.token);
        config->set(kSection, account_item_for(id), record.email);
        store_formats(config, id, record.formats);
        config->set(kSection, "provider", id);
        config->set(kSection, "enabled", "true");
        config->set(kSection, "cloud_consent", "true");
        config->save();
        return true;
    };
    store.erase = [config, id = row.id]() { return remove_provider_key(config, id); };
    auto agents = [row, gateway](const std::string& token, const std::string& model, const std::string& format) -> AgentServicePtr {
        const std::optional<ProviderProfile> served = account_profile(row, gateway, format);
        if (!served)
            return nullptr;
        return make_provider_agent(*served, token, model, {}, make_openai_http_transport(), logging_listeners());
    };
    return std::make_shared<CloudSignIn>(cloud_server_from_gateway(gateway), gateway, row.model, make_cloud_account_deps(),
                                         std::move(store), std::move(agents));
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
        if (!row->account_sign_in)
            return make_provider_agent(profile, key, choice.model, agent_endpoint_override(), make_openai_http_transport(),
                                       logging_listeners());
        const std::optional<ProviderProfile> served = account_profile(profile, endpoint_for(m_config, *row), choice.format);
        if (!served)
            return nullptr;
        return make_provider_agent(*served, key, choice.model, {}, make_openai_http_transport(), logging_listeners());
    }

    std::string model_format(const std::string& provider, const std::string& model) const override
    {
        return stored_format(m_config, provider, model);
    }

    void list_models(const std::string& provider, ModelsDone done) override
    {
        const ProviderProfile* profile = profile_for(provider);
        const std::string      key     = load_provider_api_key(m_config, provider);
        if (profile == nullptr || key.empty()) {
            done(false, {});
            return;
        }
        fetch_model_list(*profile, key, endpoint_for(m_config, *profile), m_cancelled,
                         [post = m_post, done = std::move(done), provider, config = m_config,
                          account = profile->account_sign_in](ModelListResult result) {
                             if (!result.ok)
                                 BOOST_LOG_TRIVIAL(warning) << "Agent model list for " << provider << ": " << result.error;
                             post([done, result = std::move(result), provider, config, account]() mutable {
                                 // An account's list says which format each model
                                 // is served in now; chats not yet started go by it.
                                 if (account && result.ok && config != nullptr) {
                                     std::vector<std::pair<std::string, std::string>> formats;
                                     for (const ModelOption& option : result.models)
                                         formats.emplace_back(option.id, option.format);
                                     store_formats(config, provider, formats);
                                     config->save();
                                 }
                                 done(result.ok, std::move(result.models));
                             });
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
