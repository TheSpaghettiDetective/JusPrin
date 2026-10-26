#pragma once

// Signing in to a JusPrin account, which is how the "jusprin" provider gets
// its credential: an OAuth2 access token from the JusPrin server, used as the
// gateway's bearer token where another provider takes an API key.
//
// Sign-in is the authorization code flow with PKCE in the system browser,
// answered on a loopback port (RFC 8252); the server accepts any port on its
// http://127.0.0.1/callback redirect. Finishing a sign-in, and checking a
// token the app already holds (one imported from JusPrin v1, or the stored
// one), are the same step: ask the server who the account is and which
// models it serves this account, in which wire format. A token that passes
// that step is stored with the answers, and the agent built from it is handed
// over the way key setup hands over its verified agent.
//
// GUI-free and poll-driven like ProviderKeySetup: the browser, the loopback
// server and HTTP are injected, their callbacks may arrive on any thread and
// only queue, and outcomes are released from poll() on the owner's thread.

#include "AgentService.hpp"
#include "ModelCatalog.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

// The application JusPrin 2 is registered as on the JusPrin server: a public
// client using the authorization code grant, added by hand in the server's
// admin like v1's "JusPrin" application.
inline constexpr const char* kCloudClientId = "JusPrin2";
// The loopback port the browser is sent back to. Fixed, because OrcaSlicer's
// loopback server takes a port, not one the system picks; any port is
// accepted by the server.
inline constexpr unsigned short kCloudCallbackPort = 41172;

struct CloudAuthorization
{
    std::string    authorize_url; // the page the browser opens
    std::string    token_url;
    std::string    client_id;
    std::string    redirect_uri;
    std::string    scope;
    std::string    state;
    std::string    code_verifier;
    unsigned short port{0};
    // Where the browser is sent once the app has the token, or failed to get it.
    std::string    success_url;
    std::string    failure_url;
};

struct CloudAuthorizationResult
{
    bool        ok{false};
    std::string access_token;
    std::string error; // for the log; the page shows its own words
};

struct CloudHttpResult
{
    unsigned    status{0}; // 0 when the request never got an answer
    std::string body;
};

// What signing in needs from the machine.
struct CloudAccountDeps
{
    // Opens the browser on the authorize page and waits for its redirect on
    // the loopback port; `done` is called once, with the token exchange's
    // result. False when the loopback port could not be opened.
    std::function<bool(const CloudAuthorization&, std::function<void(CloudAuthorizationResult)> done)> authorize;
    // Stops waiting; `done` is then not called.
    std::function<void()> cancel_authorize;
    // Called from every poll(), on the owner's thread: lets the machine side
    // finish there what it started (OrcaSlicer's job worker finalizes its
    // jobs on the thread that asks it to).
    std::function<void()> pump;
    // A GET with the token as bearer; `done` is called once.
    std::function<void(const std::string& url, const std::string& token, std::function<void(CloudHttpResult)> done)> get;
    // Revokes a token on the server, best effort: nothing waits for it.
    std::function<void(const std::string& url, const std::string& client_id, const std::string& token)> revoke;
    // A PKCE verifier, its S256 challenge and a state value, fresh each call.
    std::function<void(std::string& verifier, std::string& challenge, std::string& state)> pkce;
};

// The account as JusPrin keeps it on this computer.
struct CloudAccountRecord
{
    std::string token;
    std::string email;
    // Each model the account was last told about, with its wire format.
    std::vector<std::pair<std::string, std::string>> formats;
};

// Where the account is kept, injected so this file never reaches AppConfig.
struct CloudAccountStore
{
    std::function<std::optional<CloudAccountRecord>()> load;
    // Stores a verified account and records the choices that make it the
    // Agent's provider. False when it could not be written.
    std::function<bool(const CloudAccountRecord&)> save;
    std::function<bool()> erase;
};

enum class CloudSignInPhase { Idle, Browser, Checking };

struct CloudSignInOutcome
{
    enum class Kind { SignedIn, Expired, Failed };
    Kind            kind{Kind::Failed};
    std::string     email;
    // SignedIn: whether the account could be stored on this computer.
    bool            saved{false};
    // SignedIn: the agent for the account's default model, idle.
    AgentServicePtr service;
    // Failed: what went wrong.
    AgentError      error;
};

class ICloudSignIn
{
public:
    virtual ~ICloudSignIn() = default;

    virtual CloudSignInPhase phase() const = 0;
    // The page the browser was sent to, while the phase is Browser, for a
    // person whose browser did not open.
    virtual std::string browser_url() const = 0;

    // Opens the browser. False while a sign-in or check is running, or when
    // the loopback port could not be opened.
    virtual bool start() = 0;
    // Checks the token already stored: the account's email and model formats
    // are refreshed, or the token turns out to have expired. False when no
    // token is stored or something is already running.
    virtual bool check_stored() = 0;
    virtual void cancel() = 0;
    virtual std::optional<CloudSignInOutcome> poll() = 0;

    // The stored account, or nothing when signed out.
    virtual std::optional<CloudAccountRecord> account() const = 0;
    // Forgets the account on this computer and revokes its token. False when
    // there was none or it could not be removed.
    virtual bool sign_out() = 0;
};

using CloudSignInPtr = std::shared_ptr<ICloudSignIn>;

class CloudSignIn final : public ICloudSignIn
{
public:
    // Builds the agent for a verified account: its token, the model and that
    // model's wire format.
    using AgentFactory = std::function<AgentServicePtr(const std::string& token, const std::string& model,
                                                       const std::string& format)>;

    // `server` is the JusPrin server's base URL, as "https://app.obico.io";
    // `gateway` is the provider's request endpoint, whose sibling "models"
    // lists the account's models.
    CloudSignIn(std::string server, std::string gateway, std::string default_model, CloudAccountDeps deps,
                CloudAccountStore store, AgentFactory agents);
    ~CloudSignIn() override;

    CloudSignInPhase                  phase() const override { return m_phase; }
    std::string                       browser_url() const override;
    bool                              start() override;
    bool                              check_stored() override;
    void                              cancel() override;
    std::optional<CloudSignInOutcome> poll() override;
    std::optional<CloudAccountRecord> account() const override;
    bool                              sign_out() override;

private:
    struct Inbox;

    void begin_check(std::string token);
    void ask_who(std::uint64_t generation);
    void ask_models(std::uint64_t generation);
    std::optional<CloudSignInOutcome> finish_check();
    CloudSignInOutcome failed(const std::string& code, const std::string& message, bool retryable);

    std::string                    m_server;
    std::string                    m_gateway;
    std::string                    m_default_model;
    CloudAccountDeps               m_deps;
    CloudAccountStore              m_store;
    AgentFactory                   m_agents;
    std::shared_ptr<Inbox>         m_inbox;
    CloudSignInPhase               m_phase{CloudSignInPhase::Idle};
    std::uint64_t                  m_generation{0};
    CloudAuthorization             m_authorization;
    // The check in progress: the token being checked, and what the server
    // has said so far.
    std::string                    m_token;
    std::optional<CloudHttpResult> m_who;
    std::optional<CloudHttpResult> m_models;
};

// The JusPrin server a token comes from and goes back to, from its gateway
// endpoint: everything before "/ent/".
std::string cloud_server_from_gateway(const std::string& gateway);

} // namespace Slic3r::GUI::JusPrin::Agent
