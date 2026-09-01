#pragma once

#include "AgentService.hpp"

#include <string>

namespace Slic3r { class AppConfig; }

namespace Slic3r::GUI::JusPrin::Agent {

struct AgentRuntime
{
    AgentServicePtr   service;
    AgentAvailability availability{AgentAvailability::Unavailable};
    std::string       provider;
    std::string       unavailable_reason;
};

// Uses the existing AppConfig only for non-secret choices. OPENAI_API_KEY is
// a developer/test override; otherwise the key is read from the OS credential
// store. Project/recovery state never receives credentials.
AgentRuntime load_agent_runtime(const AppConfig* config);
bool save_openai_api_key(const std::string& key);
bool delete_openai_api_key();

} // namespace Slic3r::GUI::JusPrin::Agent
