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
// Persistence is injected, so this file never reaches the keychain or
// AppConfig and stays linkable into the GUI-free contract tests.

#include "AgentService.hpp"
#include "OpenAIResponsesAgent.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

struct SetupCredentials
{
    std::string provider; // "openai"
    std::string api_key;
    std::string model;    // optional; empty keeps the adapter's default
    std::string endpoint; // optional; empty keeps the adapter's default
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
    // rather than pretending: a key the keychain refused is a key the user
    // will have to enter again next launch, and they are told so.
    virtual bool commit(const SetupCredentials& credentials) = 0;
};

using AgentSetupServicePtr = std::shared_ptr<IAgentSetupService>;

// Providers this build can verify. The page offers the same list; the host
// checks anyway, so a page/host mismatch fails visibly instead of hanging.
bool setup_provider_supported(const std::string& provider);

class ProviderKeySetup final : public IAgentSetupService
{
public:
    using TransportFactory = std::function<std::unique_ptr<IAgentHttpTransport>()>;
    using CommitFn         = std::function<bool(const SetupCredentials&)>;
    using Clock            = std::function<std::chrono::steady_clock::time_point()>;

    // endpoint_override redirects the check the way JUSPRIN_OPENAI_ENDPOINT
    // redirects the live Agent, so a developer or a harness can point setup
    // at a stand-in instead of the real provider.
    ProviderKeySetup(TransportFactory transport,
                     CommitFn         commit,
                     std::string      endpoint_override = {},
                     Clock            clock             = {});
    ~ProviderKeySetup() override;

    bool                        busy() const override { return m_candidate != nullptr; }
    bool                        start_check(const SetupCredentials& credentials) override;
    void                        cancel() override;
    std::optional<SetupOutcome> poll() override;
    bool                        commit(const SetupCredentials& credentials) override;

private:
    int          elapsed_ms() const;
    SetupOutcome finish_ok();

    TransportFactory                      m_transport_factory;
    CommitFn                              m_commit;
    std::string                           m_endpoint_override;
    Clock                                 m_clock;
    AgentServicePtr                       m_candidate;
    std::chrono::steady_clock::time_point m_started{};
};

} // namespace Slic3r::GUI::JusPrin::Agent
