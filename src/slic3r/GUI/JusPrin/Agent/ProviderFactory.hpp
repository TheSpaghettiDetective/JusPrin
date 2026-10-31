#pragma once

// Building an Agent service from a provider row. GUI-free: setup and the
// runtime configuration both come through here, and so do the contract
// tests, which cannot link AppConfig.

#include "AgentConfiguration.hpp"
#include "ProviderAgent.hpp"
#include "ProviderProfile.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

struct ProviderListeners
{
    std::function<void(const AgentUsage&)> usage;
    std::function<void(const std::string&, const std::string&, const std::string&)> refusal;
    std::function<void(const SentRequest&)> request;
};

// Keeps each request sent as one JSON file in `directory`, the newest `keep`
// of them, so that what a model was told can be read after the reply looks
// wrong. A picture or file in a request becomes a note of its size. A file
// that can't be written costs the log only: the request is still sent.
std::function<void(const SentRequest&)> request_log_writer(std::filesystem::path directory, std::size_t keep = 200);

// The agent for a provider, with the codec its family speaks. An empty model
// or endpoint keeps the row's default.
AgentServicePtr make_provider_agent(const ProviderProfile&                profile,
                                    const std::string&                    api_key,
                                    const std::string&                    model,
                                    const std::string&                    endpoint,
                                    std::unique_ptr<IAgentHttpTransport>  transport,
                                    ProviderListeners                     listeners = {});

// Where a developer or a harness points the Agent instead of the provider:
// JUSPRIN_AGENT_ENDPOINT, or its older name JUSPRIN_OPENAI_ENDPOINT. It
// applies to whichever provider is in use. Empty when neither is set.
std::string agent_endpoint_override();

// The Agent choices AppConfig holds, read by AgentConfiguration.cpp.
struct AgentSettings
{
    bool        enabled{false};
    bool        cloud_consent{false};
    std::string provider; // empty means openai
    std::string api_key;  // the stored "<provider>_api_key"
    std::string model;    // empty keeps the provider's default
    // For an account provider: the wire format its model is served in (as
    // the account's model list last said) and the gateway endpoint. Empty
    // format means the account has not been checked yet.
    std::string format;
    std::string endpoint;
};

// The runtime those choices give: a ready service, or the reason there is
// none. `setup` is left for the caller to fill.
AgentRuntime resolve_agent_runtime(const AgentSettings&                                  settings,
                                   const std::string&                                    endpoint_override,
                                   const std::function<std::unique_ptr<IAgentHttpTransport>()>& transport,
                                   ProviderListeners                                     listeners = {});

} // namespace Slic3r::GUI::JusPrin::Agent
