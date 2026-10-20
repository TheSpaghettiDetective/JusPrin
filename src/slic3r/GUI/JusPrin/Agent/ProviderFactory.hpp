#pragma once

// Building an Agent service from a provider row. GUI-free: setup and the
// runtime configuration both come through here, and so do the contract
// tests, which cannot link AppConfig.

#include "AgentConfiguration.hpp"
#include "ProviderAgent.hpp"
#include "ProviderProfile.hpp"

#include <functional>
#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

struct ProviderListeners
{
    std::function<void(const AgentUsage&)> usage;
    std::function<void(const std::string&, const std::string&, const std::string&)> refusal;
};

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
};

// The runtime those choices give: a ready service, or the reason there is
// none. `setup` is left for the caller to fill.
AgentRuntime resolve_agent_runtime(const AgentSettings&                                  settings,
                                   const std::string&                                    endpoint_override,
                                   const std::function<std::unique_ptr<IAgentHttpTransport>()>& transport,
                                   ProviderListeners                                     listeners = {});

} // namespace Slic3r::GUI::JusPrin::Agent
