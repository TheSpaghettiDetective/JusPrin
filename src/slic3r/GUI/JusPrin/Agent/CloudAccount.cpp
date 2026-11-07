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
    enum class Kind { Authorized, Who, Models, Auth };
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

bool CloudSignIn::start(CloudBrowserStart where)
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
    const std::string authorize = "/o/authorize/?" + query({{"response_type", "code"},
                                                            {"client_id", authorization.client_id},
                                                            {"redirect_uri", authorization.redirect_uri},
                                                            {"scope", authorization.scope},
                                                            {"state", state},
                                                            {"code_challenge", challenge},
                                                            {"code_challenge_method", "S256"}});
    // Google's sign-in comes back to the server, which then sends the browser
    // on to /o/authorize/, now signed in. Opened directly, /o/authorize/ first
    // shows the server's sign-in page.
    authorization.authorize_url = where == CloudBrowserStart::Google ?
                                      m_server + "/accounts/google/login/?" +
                                          query({{"process", "login"}, {"next", authorize}}) :
                                      m_server + authorize;

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

std::optional<CloudEmailStep> CloudSignIn::email_step() const
{
    if (m_phase != CloudSignInPhase::Email)
        return std::nullopt;
    return m_email;
}

void CloudSignIn::ask_server(const std::string& endpoint, const json& body, const std::string& session)
{
    m_asked             = endpoint;
    m_email.busy        = true;
    m_email.refusal.clear();
    std::weak_ptr<Inbox> inbox = m_inbox;
    const std::uint64_t generation = m_generation;
    m_deps.post(m_server + "/ent/jusprin/v2/auth/" + endpoint, body.dump(), session,
                [inbox, generation](CloudHttpResult result) {
                    if (const auto target = inbox.lock())
                        target->push({generation, Inbox::Kind::Auth, {}, std::move(result)});
                });
}

bool CloudSignIn::start_email(const std::string& email)
{
    if (m_phase != CloudSignInPhase::Idle || email.empty())
        return false;
    ++m_generation;
    m_phase       = CloudSignInPhase::Email;
    m_email       = CloudEmailStep{};
    m_email.email = email;
    m_session.clear();
    return true;
}

bool CloudSignIn::send_code()
{
    if (m_phase != CloudSignInPhase::Email || m_email.busy)
        return false;
    // A new request rather than allauth's resend: a resent code keeps the
    // first one's deadline and is refused once that code is spent. The
    // pending code's session stays until the new one's arrives, in case the
    // server refuses to send another.
    ask_server("code/request", {{"email", m_email.email}}, {});
    return true;
}

bool CloudSignIn::submit_code(const std::string& code)
{
    if (m_phase != CloudSignInPhase::Email || m_email.busy || m_email.step != CloudEmailStep::Step::Code)
        return false;
    ask_server("code/confirm", {{"code", code}}, m_session);
    return true;
}

bool CloudSignIn::submit_password(const std::string& password)
{
    if (m_phase != CloudSignInPhase::Email || m_email.busy || m_email.step != CloudEmailStep::Step::Choose)
        return false;
    // The server signs in by username; allauth matches it against addresses too.
    ask_server("login", {{"username", m_email.email}, {"password", password}}, {});
    return true;
}

// What an auth endpoint answered, in allauth's shapes: 200 once signed in,
// with the token in "meta"; 401 while a code is pending, with the session
// token to carry; 400 with "errors"; 409 once the pending code is gone. A
// token goes on to the same check as the browser's; a refusal the person can
// act on stays on the step; anything else ends the sign-in with one generic
// error.
std::optional<CloudSignInOutcome> CloudSignIn::answered(CloudHttpResult http)
{
    m_email.busy = false;
    const json body   = json::parse(http.body, nullptr, false);
    const auto errors = [&](const char* code) {
        if (!body.is_object() || !body.contains("errors") || !body["errors"].is_array())
            return false;
        return std::any_of(body["errors"].begin(), body["errors"].end(),
                           [code](const json& error) { return error.is_object() && error.value("code", "") == code; });
    };
    const std::string session = body.is_object() ? body.value("/meta/session_token"_json_pointer, std::string()) : std::string();
    const std::string token   = body.is_object() ? body.value("/meta/access_token"_json_pointer, std::string()) : std::string();

    if (m_asked == "code/request" && http.status == 401 && !session.empty()) {
        m_email.code_resent = m_email.step == CloudEmailStep::Step::Code;
        m_email.step        = CloudEmailStep::Step::Code;
        m_session           = session;
        return std::nullopt;
    }
    if (http.status == 200 && !token.empty()) {
        m_session.clear();
        begin_check(token);
        return std::nullopt;
    }
    if (http.status == 400 && m_asked == "code/request" && errors("too_many_login_attempts"))
        m_email.refusal = "too_many_codes";
    else if (http.status == 400 && m_asked == "code/confirm" && errors("incorrect_code"))
        m_email.refusal = "wrong_code";
    else if (http.status == 409 && m_asked == "code/confirm")
        m_email.refusal = "expired_code";
    else if (http.status == 400 && m_asked == "login")
        m_email.refusal = errors("too_many_login_attempts") ? "too_many_tries" : "wrong_password";
    else
        return failed("sign_in_failed", "JusPrin couldn't sign you in. Try again.", true);
    return std::nullopt;
}

void CloudSignIn::cancel()
{
    if (m_phase == CloudSignInPhase::Browser && m_deps.cancel_authorize)
        m_deps.cancel_authorize();
    ++m_generation;
    m_phase = CloudSignInPhase::Idle;
    m_token.clear();
    m_session.clear();
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
        case Inbox::Kind::Auth:
            if (m_phase != CloudSignInPhase::Email)
                continue;
            if (std::optional<CloudSignInOutcome> outcome = answered(std::move(item->http)))
                return outcome;
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
        return failed("account_no_models", "JusPrin has no AI model for this account right now. Try again later.", true);

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
