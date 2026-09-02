#pragma once

#include "AgentService.hpp"
#include "AgentSetup.hpp"

#include <string>

namespace Slic3r { class AppConfig; }

namespace Slic3r::GUI::JusPrin::Agent {

struct AgentRuntime
{
    AgentServicePtr      service;
    AgentAvailability    availability{AgentAvailability::Unavailable};
    std::string          provider;
    std::string          unavailable_reason;
    // Verifies and stores credentials entered in the Agent dock. Always
    // present: setup is what an unconfigured build offers.
    AgentSetupServicePtr setup;
};

// Uses the existing AppConfig only for non-secret choices. OPENAI_API_KEY is
// a developer/test override; otherwise the key is read from the OS credential
// store. Project/recovery state never receives credentials.
AgentRuntime load_agent_runtime(AppConfig* config);

// Credential storage, keyed by provider. The OpenAI entry keeps the name
// earlier builds wrote, so a key saved before this became multi-provider is
// still found.
bool save_provider_api_key(const std::string& provider, const std::string& key);
bool delete_provider_api_key(const std::string& provider);

// The setup service the Agent dock drives: it verifies a candidate key
// against the live provider and, on success, stores it and records the
// non-secret choices load_agent_runtime() reads on the next launch.
AgentSetupServicePtr make_agent_setup(AppConfig* config);

} // namespace Slic3r::GUI::JusPrin::Agent
