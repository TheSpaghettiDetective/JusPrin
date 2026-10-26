#pragma once

#include "AgentService.hpp"
#include "AgentSetup.hpp"

#include <functional>
#include <memory>
#include <string>

namespace Slic3r { class AppConfig; }

namespace Slic3r::GUI::JusPrin::Agent {

class IChatModelSource;

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

// Reads the Agent's choices and provider key from AppConfig. Project/recovery
// state never receives credentials.
AgentRuntime load_agent_runtime(AppConfig* config);

// Shared provider credential lookup for bounded, non-conversational features
// such as printer recognition. The key is an AppConfig item, "<provider>_api_key"
// in the Agent section, rather than an OS credential-store entry: macOS asks
// for the login password whenever a differently signed build reads a Keychain
// entry, which every unsigned rebuild is.
std::string load_provider_api_key(const AppConfig* config, const std::string& provider);

// The setup service the Agent dock drives: it verifies a candidate key
// against the live provider and, on success, stores it and records the
// non-secret choices load_agent_runtime() reads on the next launch.
AgentSetupServicePtr make_agent_setup(AppConfig* config);

// Signing in to a JusPrin account: the account is stored where the Agent's
// keys are, and signing in makes it the Agent's provider.
CloudSignInPtr make_cloud_sign_in(AppConfig* config);

// The docked Agent's per-chat models: every provider with a stored key, the
// default provider setup recorded, agents built from those keys, and each
// provider's model list fetched from the provider. `post` runs a callback on
// the GUI thread; the lists arrive through it.
std::shared_ptr<IChatModelSource> make_chat_model_source(AppConfig* config,
                                                         std::function<void(std::function<void()>)> post);

} // namespace Slic3r::GUI::JusPrin::Agent
