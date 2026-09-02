#include "AgentSetup.hpp"

#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

// The probe deliberately carries no project. A provider the user has not
// chosen yet must not receive their workspace merely to prove that a key
// works, so the request is a fixed sentence with an empty snapshot.
AgentRequest probe_request()
{
    AgentRequest request;
    request.request_id = "setup-key-check";
    request.user_text  = "Reply with the single word: ready.";
    return request;
}

} // namespace

bool setup_provider_supported(const std::string& provider) { return provider == "openai"; }

ProviderKeySetup::ProviderKeySetup(TransportFactory transport,
                                   CommitFn         commit,
                                   std::string      endpoint_override,
                                   Clock            clock)
    : m_transport_factory(std::move(transport)), m_commit(std::move(commit)),
      m_endpoint_override(std::move(endpoint_override)), m_clock(std::move(clock))
{
    if (!m_clock)
        m_clock = []() { return std::chrono::steady_clock::now(); };
}

ProviderKeySetup::~ProviderKeySetup() = default;

bool ProviderKeySetup::start_check(const SetupCredentials& credentials)
{
    if (busy() || credentials.api_key.empty() || !setup_provider_supported(credentials.provider))
        return false;

    OpenAIResponsesConfig config;
    config.api_key = credentials.api_key;
    if (!credentials.model.empty())
        config.model = credentials.model;
    if (!m_endpoint_override.empty())
        config.endpoint = m_endpoint_override;
    if (!credentials.endpoint.empty())
        config.endpoint = credentials.endpoint;

    auto candidate = std::make_unique<OpenAIResponsesAgent>(std::move(config), m_transport_factory());
    if (!candidate->ready())
        return false;

    m_started = m_clock();
    if (!candidate->start(probe_request()))
        return false;
    m_candidate = std::move(candidate);
    return true;
}

void ProviderKeySetup::cancel()
{
    if (m_candidate)
        m_candidate->cancel();
    m_candidate.reset();
}

std::optional<SetupOutcome> ProviderKeySetup::poll()
{
    if (!m_candidate)
        return std::nullopt;

    while (const std::optional<AgentEvent> event = m_candidate->poll()) {
        switch (event->kind) {
        case AgentEventKind::TextDelta:
            break; // What it says is irrelevant; that it answers is the test.
        case AgentEventKind::ToolCall:
        case AgentEventKind::Completed:
            // Either way the provider accepted the credentials and replied.
            return finish_ok();
        case AgentEventKind::Failed: {
            SetupOutcome outcome;
            outcome.error =
                event->error.value_or(AgentError{"setup_failed", "The provider did not answer.", true});
            outcome.elapsed_ms = elapsed_ms();
            cancel();
            return outcome;
        }
        }
    }
    return std::nullopt;
}

bool ProviderKeySetup::commit(const SetupCredentials& credentials)
{
    return m_commit && m_commit(credentials);
}

int ProviderKeySetup::elapsed_ms() const
{
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(m_clock() - m_started).count());
}

SetupOutcome ProviderKeySetup::finish_ok()
{
    SetupOutcome outcome;
    outcome.ok         = true;
    outcome.elapsed_ms = elapsed_ms();
    // Drop the probe turn so the verified service is handed over idle, with
    // no partial response left for the user's first real message to inherit.
    m_candidate->cancel();
    outcome.service = std::move(m_candidate);
    m_candidate.reset();
    return outcome;
}

} // namespace Slic3r::GUI::JusPrin::Agent
