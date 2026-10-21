#include "AgentSetup.hpp"
#include "ProviderFactory.hpp"

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

// The fixed starts each provider gives its keys, longer ones first. They say
// what kind of key it is, not which one.
const std::vector<std::string>& key_prefixes(const std::string& provider)
{
    static const std::vector<std::string> anthropic{"sk-ant-"};
    static const std::vector<std::string> openai{"sk-proj-", "sk-svcacct-", "sk-admin-", "sk-"};
    static const std::vector<std::string> openrouter{"sk-or-v1-", "sk-or-"};
    static const std::vector<std::string> none;
    if (provider == "anthropic")
        return anthropic;
    if (provider == "openai")
        return openai;
    if (provider == "openrouter")
        return openrouter;
    return none;
}

// Keys shorter than this past their start keep their end to themselves too:
// 4 characters of a short key would be a large part of it.
constexpr std::size_t kMinHiddenForTail = 8;
constexpr std::size_t kTailLength       = 4;
constexpr const char* kEllipsis         = "\xE2\x80\xA6"; // U+2026

} // namespace

std::string api_key_hint(const std::string& provider, const std::string& key)
{
    std::string prefix;
    for (const std::string& candidate : key_prefixes(provider))
        if (key.size() > candidate.size() && key.compare(0, candidate.size(), candidate) == 0) {
            prefix = candidate;
            break;
        }
    std::string hint = prefix + kEllipsis;
    if (key.size() >= prefix.size() + kMinHiddenForTail + kTailLength)
        hint += key.substr(key.size() - kTailLength);
    return hint;
}

bool setup_provider_supported(const std::string& provider) { return profile_for(provider) != nullptr; }

ProviderKeySetup::ProviderKeySetup(TransportFactory transport,
                                   CommitFn         commit,
                                   KeyStore         keys,
                                   std::string      endpoint_override,
                                   Clock            clock)
    : m_transport_factory(std::move(transport)), m_commit(std::move(commit)), m_keys(std::move(keys)),
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

    const std::string endpoint = credentials.endpoint.empty() ? m_endpoint_override : credentials.endpoint;
    AgentServicePtr   candidate = make_provider_agent(*profile_for(credentials.provider), credentials.api_key, credentials.model,
                                                      endpoint, m_transport_factory());
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

std::vector<SavedKey> ProviderKeySetup::saved_keys() const
{
    std::vector<SavedKey> saved;
    if (!m_keys.load)
        return saved;
    for (const ProviderProfile& profile : provider_profiles()) {
        const std::string key = m_keys.load(profile.id);
        if (!key.empty())
            saved.push_back(SavedKey{profile.id, api_key_hint(profile.id, key)});
    }
    return saved;
}

bool ProviderKeySetup::remove_key(const std::string& provider)
{
    return setup_provider_supported(provider) && m_keys.remove && m_keys.remove(provider);
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
