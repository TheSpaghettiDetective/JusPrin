#pragma once

// A model provider as data: which API family it speaks, where, how it is
// authenticated, which model it is given by default, what it is called in
// the errors shown to the person, and any fixed headers it wants. The turn
// loop (ProviderAgent) and the codecs read it; the table below is the only
// place a provider's name decides anything.

#include "AgentTransport.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

// The wire format a provider speaks; one codec per family.
enum class ProviderFamily { Responses, Anthropic };

// How the API key travels: `Authorization: Bearer <key>`, or `x-api-key: <key>`.
enum class AuthStyle { Bearer, ApiKeyHeader };

struct ProviderProfile
{
    std::string    id;
    ProviderFamily family{ProviderFamily::Responses};
    std::string    endpoint;
    AuthStyle      auth{AuthStyle::Bearer};
    // The provider's default; a configured model replaces it in the copy the
    // agent is built with.
    std::string    model;
    // In the error text, as in "The OpenAI API key was rejected".
    std::string    display_name;
    // Sent on every request, after the credential.
    std::vector<AgentHttpHeader> headers;
    // The most output tokens one response may use, for a family that
    // requires the number (Anthropic); 0 where none is sent.
    unsigned       max_tokens{0};
    // How hard the model thinks before a reply, chosen per chat: one of the
    // levels the provider's model list offers for this model ("low",
    // "medium", "high", ...). Empty sends nothing, so the model's own default
    // applies. Title requests never carry it.
    std::string    effort;
    // The credential is a JusPrin account's sign-in rather than an API key,
    // and the endpoint is JusPrin's gateway. Every request names its turn
    // (X-JusPrin-Turn) and says whether it is a title (X-JusPrin-Request),
    // because the gateway charges the account once per turn and never for a
    // title; a refusal means the sign-in has ended, and 402 means the
    // account's allowance is used up.
    bool           account_sign_in{false};
};

// Every provider this build can talk to, in the order setup lists them.
inline const std::vector<ProviderProfile>& provider_profiles()
{
    static const std::vector<ProviderProfile> rows{
        // JusPrin's own gateway. Its models are aliases ("slicer-1") that the
        // gateway maps to a provider's model, and each alias says which wire
        // format it is served in; account_profile() turns this row into the
        // one a chat talks to. max_tokens serves the Anthropic format.
        // BEFORE PRODUCTION: JusPrin may get a domain of its own instead of
        // app.obico.io. This endpoint, the server the account signs in to
        // (derived from it), and the v1 import's carried jusprin_server/base_url
        // all move with that decision; see the rollout notes in the ops repo.
        {"jusprin", ProviderFamily::Responses, "https://app.obico.io/ent/jusprin/v2/responses", AuthStyle::Bearer,
         "slicer-1", "JusPrin", {}, 64000, {}, true},
        {"openai", ProviderFamily::Responses, "https://api.openai.com/v1/responses", AuthStyle::Bearer, "gpt-5.4-mini", "OpenAI", {}, 0},
        // OpenRouter serves the Responses API. The default is the model the
        // OpenAI row uses, so the behaviour is the one already evaluated.
        // X-Title names the app in OpenRouter's usage pages. OpenRouter
        // also reads HTTP-Referer, the app's website, to give it a page of
        // its own; this build has no website constant to send, so it sends
        // none.
        {"openrouter", ProviderFamily::Responses, "https://openrouter.ai/api/v1/responses", AuthStyle::Bearer,
         "openai/gpt-5.4-mini", "OpenRouter", {{"X-Title", "JusPrin"}}, 0},
        // The Messages API. max_tokens is required there and also bounds the
        // model's thinking, which this model cannot turn off; 64000 is the
        // value Anthropic suggests for streaming requests.
        {"anthropic", ProviderFamily::Anthropic, "https://api.anthropic.com/v1/messages", AuthStyle::ApiKeyHeader,
         "claude-opus-5-5", "Anthropic", {{"anthropic-version", "2023-06-01"}}, 64000},
    };
    return rows;
}

// The row for a provider id, or null when this build has none.
inline const ProviderProfile* profile_for(const std::string& id)
{
    for (const ProviderProfile& profile : provider_profiles())
        if (profile.id == id)
            return &profile;
    return nullptr;
}

inline const ProviderProfile& openai_profile() { return *profile_for("openai"); }

// The provider a JusPrin account signs in to.
inline const ProviderProfile& account_provider()
{
    for (const ProviderProfile& profile : provider_profiles())
        if (profile.account_sign_in)
            return profile;
    throw std::logic_error("provider_profiles() has no account provider");
}

// The wire formats JusPrin's gateway serves a model in, as its model list
// names them, and the endpoint each one is posted to beside the other.
inline constexpr const char* kFormatAnthropic = "anthropic";
inline constexpr const char* kFormatResponses = "responses";

// An account provider's row as one model is served: the family the format
// names, posted to that format's endpoint next to `endpoint` (the last path
// segment is replaced, so an override or a self-hosted server moves both).
// Null for a format this build does not speak.
inline std::optional<ProviderProfile> account_profile(const ProviderProfile& row, const std::string& endpoint,
                                                      const std::string& format)
{
    ProviderProfile profile = row;
    if (format == kFormatAnthropic)
        profile.family = ProviderFamily::Anthropic;
    else if (format == kFormatResponses)
        profile.family = ProviderFamily::Responses;
    else
        return std::nullopt;
    const std::string base  = endpoint.empty() ? row.endpoint : endpoint;
    const std::size_t slash = base.rfind('/');
    profile.endpoint = (slash == std::string::npos ? base : base.substr(0, slash + 1)) +
                       (format == kFormatAnthropic ? "messages" : "responses");
    return profile;
}

} // namespace Slic3r::GUI::JusPrin::Agent
