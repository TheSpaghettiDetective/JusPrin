#include "CloudAccount.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>

namespace Slic3r::GUI::JusPrin::Agent {

using nlohmann::json;

namespace {

std::string percent_encoded(const std::string& text)
{
    std::string encoded;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            encoded += escaped;
        }
    }
    return encoded;
}

std::string query(const std::vector<std::pair<std::string, std::string>>& parameters)
{
    std::string text;
    for (const auto& [name, value] : parameters)
        text += (text.empty() ? "" : "&") + name + "=" + percent_encoded(value);
    return text;
}

bool refused(const CloudHttpResult& result) { return result.status == 401 || result.status == 403; }

} // namespace

// What the injected browser and HTTP calls report, queued until poll(). Shared
// with the callbacks so one that arrives after this object is gone has
// somewhere harmless to go.
struct CloudSignIn::Inbox
{
    enum class Kind { Authorized, Who, Models };
    struct Item
    {
        std::uint64_t            generation{0};
        Kind                     kind{Kind::Authorized};
        CloudAuthorizationResult authorization;
        CloudHttpResult          http;
    };
    std::mutex       mutex;
    std::deque<Item> items;

    void push(Item item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        items.push_back(std::move(item));
    }
    std::optional<Item> pop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (items.empty())
            return std::nullopt;
        Item item = std::move(items.front());
        items.pop_front();
        return item;
    }
};

std::string cloud_server_from_gateway(const std::string& gateway)
{
    const std::size_t ent = gateway.find("/ent/");
    if (ent != std::string::npos)
        return gateway.substr(0, ent);
    const std::size_t scheme = gateway.find("://");
    const std::size_t path   = gateway.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    return path == std::string::npos ? gateway : gateway.substr(0, path);
}

CloudSignIn::CloudSignIn(std::string server, std::string gateway, std::string default_model, CloudAccountDeps deps,
                         CloudAccountStore store, AgentFactory agents)
    : m_server(std::move(server)), m_gateway(std::move(gateway)), m_default_model(std::move(default_model)),
      m_deps(std::move(deps)), m_store(std::move(store)), m_agents(std::move(agents)), m_inbox(std::make_shared<Inbox>())
{}

CloudSignIn::~CloudSignIn() { cancel(); }

std::string CloudSignIn::browser_url() const
{
    return m_phase == CloudSignInPhase::Browser ? m_authorization.authorize_url : std::string();
}

bool CloudSignIn::start()
{
    if (m_phase != CloudSignInPhase::Idle)
        return false;
    std::string verifier, challenge, state;
    m_deps.pkce(verifier, challenge, state);
    CloudAuthorization authorization;
    authorization.client_id     = kCloudClientId;
    authorization.port          = kCloudCallbackPort;
    authorization.redirect_uri  = "http://127.0.0.1:" + std::to_string(kCloudCallbackPort) + "/callback";
    authorization.scope         = "read write";
    authorization.state         = state;
    authorization.code_verifier = verifier;
    authorization.token_url     = m_server + "/o/token/";
    authorization.success_url   = m_server + "/ent/jusprin/v2/signed_in/";
    authorization.failure_url   = m_server + "/ent/jusprin/v2/sign_in_failed/";
    authorization.authorize_url = m_server + "/o/authorize/?" +
                                  query({{"response_type", "code"},
                                         {"client_id", authorization.client_id},
                                         {"redirect_uri", authorization.redirect_uri},
                                         {"scope", authorization.scope},
                                         {"state", state},
                                         {"code_challenge", challenge},
                                         {"code_challenge_method", "S256"}});

    const std::uint64_t generation = ++m_generation;
    std::weak_ptr<Inbox> inbox     = m_inbox;
    if (!m_deps.authorize(authorization, [inbox, generation](CloudAuthorizationResult result) {
            if (const auto target = inbox.lock())
                target->push({generation, Inbox::Kind::Authorized, std::move(result), {}});
        }))
        return false;
    m_authorization = std::move(authorization);
    m_phase         = CloudSignInPhase::Browser;
    return true;
}

bool CloudSignIn::check_stored()
{
    if (m_phase != CloudSignInPhase::Idle || !m_store.load)
        return false;
    const std::optional<CloudAccountRecord> record = m_store.load();
    if (!record || record->token.empty())
        return false;
    ++m_generation;
    begin_check(record->token);
    return true;
}

void CloudSignIn::begin_check(std::string token)
{
    m_phase = CloudSignInPhase::Checking;
    m_token = std::move(token);
    m_who.reset();
    m_models.reset();
    ask_who(m_generation);
    ask_models(m_generation);
}

void CloudSignIn::ask_who(std::uint64_t generation)
{
    std::weak_ptr<Inbox> inbox = m_inbox;
    m_deps.get(m_server + "/jusprin/api/me/", m_token, [inbox, generation](CloudHttpResult result) {
        if (const auto target = inbox.lock())
            target->push({generation, Inbox::Kind::Who, {}, std::move(result)});
    });
}

void CloudSignIn::ask_models(std::uint64_t generation)
{
    const std::size_t    slash = m_gateway.rfind('/');
    const std::string    url   = (slash == std::string::npos ? m_gateway : m_gateway.substr(0, slash)) + "/models";
    std::weak_ptr<Inbox> inbox = m_inbox;
    m_deps.get(url, m_token, [inbox, generation](CloudHttpResult result) {
        if (const auto target = inbox.lock())
            target->push({generation, Inbox::Kind::Models, {}, std::move(result)});
    });
}

void CloudSignIn::cancel()
{
    if (m_phase == CloudSignInPhase::Browser && m_deps.cancel_authorize)
        m_deps.cancel_authorize();
    ++m_generation;
    m_phase = CloudSignInPhase::Idle;
    m_token.clear();
    m_who.reset();
    m_models.reset();
}

std::optional<CloudSignInOutcome> CloudSignIn::poll()
{
    if (m_deps.pump)
        m_deps.pump();
    while (std::optional<Inbox::Item> item = m_inbox->pop()) {
        if (item->generation != m_generation)
            continue; // from a sign-in that was cancelled
        switch (item->kind) {
        case Inbox::Kind::Authorized:
            if (m_phase != CloudSignInPhase::Browser)
                continue;
            if (!item->authorization.ok || item->authorization.access_token.empty())
                return failed("sign_in_failed", "Signing in didn't finish. Nothing was saved. Try again.", true);
            begin_check(std::move(item->authorization.access_token));
            break;
        case Inbox::Kind::Who: m_who = std::move(item->http); break;
        case Inbox::Kind::Models: m_models = std::move(item->http); break;
        }
        if (m_phase == CloudSignInPhase::Checking && m_who && m_models)
            return finish_check();
    }
    return std::nullopt;
}

std::optional<CloudSignInOutcome> CloudSignIn::finish_check()
{
    const CloudHttpResult who    = std::move(*m_who);
    const CloudHttpResult models = std::move(*m_models);
    m_who.reset();
    m_models.reset();
    if (refused(who) || refused(models)) {
        m_phase = CloudSignInPhase::Idle;
        m_token.clear();
        CloudSignInOutcome outcome;
        outcome.kind = CloudSignInOutcome::Kind::Expired;
        return outcome;
    }
    const json who_json    = json::parse(who.body, nullptr, false);
    const json models_json = json::parse(models.body, nullptr, false);
    if (who.status != 200 || models.status != 200 || !who_json.is_object() || !models_json.is_object() ||
        !models_json.contains("data"))
        return failed("account_unreachable", "JusPrin couldn't be reached to finish signing in. Try again.", true);

    CloudAccountRecord record;
    record.token = m_token;
    // /jusprin/api/me/ answers {"user": {"email", ...}, "ai_credits": {...}}.
    record.email = who_json.value("/user/email"_json_pointer, std::string());
    for (const json& entry : models_json["data"]) {
        const std::string id = entry.value("id", ""), format = entry.value("format", "");
        if (!id.empty() && !format.empty())
            record.formats.emplace_back(id, format);
    }
    // The agent is for the provider's default model, or else the first the
    // server lists.
    auto chosen = std::find_if(record.formats.begin(), record.formats.end(),
                               [this](const auto& entry) { return entry.first == m_default_model; });
    if (chosen == record.formats.end())
        chosen = record.formats.begin();
    AgentServicePtr service = chosen == record.formats.end() ? nullptr : m_agents(m_token, chosen->first, chosen->second);
    if (!service)
        return failed("account_no_models", "JusPrin has no Agent model for this account right now. Try again later.", true);

    CloudSignInOutcome outcome;
    outcome.kind    = CloudSignInOutcome::Kind::SignedIn;
    outcome.email   = record.email;
    outcome.saved   = m_store.save && m_store.save(record);
    outcome.service = std::move(service);
    m_phase         = CloudSignInPhase::Idle;
    m_token.clear();
    return outcome;
}

CloudSignInOutcome CloudSignIn::failed(const std::string& code, const std::string& message, bool retryable)
{
    m_phase = CloudSignInPhase::Idle;
    m_token.clear();
    CloudSignInOutcome outcome;
    outcome.kind  = CloudSignInOutcome::Kind::Failed;
    outcome.error = AgentError{code, message, retryable};
    return outcome;
}

std::optional<CloudAccountRecord> CloudSignIn::account() const
{
    if (!m_store.load)
        return std::nullopt;
    std::optional<CloudAccountRecord> record = m_store.load();
    if (!record || record->token.empty())
        return std::nullopt;
    return record;
}

bool CloudSignIn::sign_out()
{
    const std::optional<CloudAccountRecord> record = account();
    if (!record || !m_store.erase || !m_store.erase())
        return false;
    cancel();
    if (m_deps.revoke)
        m_deps.revoke(m_server + "/o/revoke_token/", kCloudClientId, record->token);
    return true;
}

} // namespace Slic3r::GUI::JusPrin::Agent
