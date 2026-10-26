#pragma once

// Provider credential setup for the Agent dock.
//
// Verifying a key and building the service that will use it are the same act
// here: the check runs one real request through the provider adapter the
// Agent goes on to use, so a key that passes is a key the Agent is already
// talking through, and the round-trip the user is shown is the one that
// actually happened. Nothing separate has to be kept in sync with it.
//
// GUI-free and poll-driven, mirroring IAgentService: transport callbacks only
// queue work, and the owner releases outcomes from poll() on the GUI thread.
// Persistence is injected, so this file never reaches AppConfig and stays
// linkable into the GUI-free contract tests.

#include "AgentService.hpp"
#include "AgentTransport.hpp"
#include "CloudAccount.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct SetupCredentials
{
    std::string provider; // a provider_profiles() id
    std::string api_key;
    std::string model;    // optional; empty keeps the adapter's default
    std::string endpoint; // optional; empty keeps the adapter's default
};

// What the setup screen may know about a stored key: that there is one, and
// a hint to tell it apart from another. The key itself never leaves the host.
struct SavedKey
{
    std::string provider; // a provider_profiles() id
    std::string hint;     // api_key_hint()
    // For an account provider, the signed-in account's email, which the
    // setup screen shows instead of a key.
    std::string account;
};

struct SetupOutcome
{
    bool            ok{false};
    AgentError      error;      // set when !ok
    int             elapsed_ms{0};
    AgentServicePtr service;    // set when ok: verified, idle, ready to install
};

class IAgentSetupService
{
public:
    virtual ~IAgentSetupService() = default;

    virtual bool busy() const = 0;

    // Begins one verification. False when a check is already running, the
    // credentials are incomplete, or this build has no adapter for the
    // provider.
    virtual bool start_check(const SetupCredentials& credentials) = 0;
    virtual void cancel() = 0;

    // Releases at most one finished outcome; called from the owner's tick.
    virtual std::optional<SetupOutcome> poll() = 0;

    // Persists credentials that have just been verified. Reports failure
    // rather than pretending: a key that was not stored is a key the user
    // will have to enter again next launch, and they are told so.
    virtual bool commit(const SetupCredentials& credentials) = 0;

    // Every provider with a stored key, in provider_profiles() order.
    virtual std::vector<SavedKey> saved_keys() const = 0;

    // Deletes one provider's stored key. False when there was none or it
    // could not be removed. For an account provider this is signing out.
    virtual bool remove_key(const std::string& provider) = 0;

    // Signing in to a JusPrin account, where this build can; null otherwise.
    virtual ICloudSignIn* cloud_sign_in() { return nullptr; }
};

using AgentSetupServicePtr = std::shared_ptr<IAgentSetupService>;

// Providers this build can verify. The page offers the same list; the host
// checks anyway, so a page/host mismatch fails visibly instead of hanging.
bool setup_provider_supported(const std::string& provider);

// A stored key as the setup screen shows it, "sk-ant-…a3F9": the fixed start
// the provider gives every key of its kind, then the last 4 characters. A key
// with no known start shows only its end, and one too short to spare 4
// characters shows none of them.
std::string api_key_hint(const std::string& provider, const std::string& key);

class ProviderKeySetup final : public IAgentSetupService
{
public:
    using TransportFactory = std::function<std::unique_ptr<IAgentHttpTransport>()>;
    using CommitFn         = std::function<bool(const SetupCredentials&)>;
    // Reads one provider's stored key ("" when none) and deletes it.
    struct KeyStore
    {
        std::function<std::string(const std::string& provider)> load;
        std::function<bool(const std::string& provider)>        remove;
    };
    using Clock            = std::function<std::chrono::steady_clock::time_point()>;

    // endpoint_override redirects the check the way JUSPRIN_AGENT_ENDPOINT
    // redirects the live Agent (agent_endpoint_override()), so a developer or
    // a harness can point setup at a stand-in instead of the real provider.
    ProviderKeySetup(TransportFactory transport,
                     CommitFn         commit,
                     KeyStore         keys              = {},
                     std::string      endpoint_override = {},
                     Clock            clock             = {},
                     CloudSignInPtr   cloud             = {});
    ~ProviderKeySetup() override;

    bool                        busy() const override { return m_candidate != nullptr; }
    bool                        start_check(const SetupCredentials& credentials) override;
    void                        cancel() override;
    std::optional<SetupOutcome> poll() override;
    bool                        commit(const SetupCredentials& credentials) override;
    std::vector<SavedKey>       saved_keys() const override;
    bool                        remove_key(const std::string& provider) override;
    ICloudSignIn*               cloud_sign_in() override { return m_cloud.get(); }

private:
    int          elapsed_ms() const;
    SetupOutcome finish_ok();

    TransportFactory                      m_transport_factory;
    CommitFn                              m_commit;
    KeyStore                              m_keys;
    std::string                           m_endpoint_override;
    Clock                                 m_clock;
    CloudSignInPtr                        m_cloud;
    AgentServicePtr                       m_candidate;
    std::chrono::steady_clock::time_point m_started{};
};

} // namespace Slic3r::GUI::JusPrin::Agent
