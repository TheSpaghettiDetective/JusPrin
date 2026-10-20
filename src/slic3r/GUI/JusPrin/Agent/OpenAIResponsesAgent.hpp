#pragma once

// The OpenAI adapter: the provider-neutral turn loop (ProviderAgent) with
// the Responses codec and the openai profile. Kept as one class so setup,
// configuration and the harnesses name a provider the way they always have;
// what it does lives in ProviderAgent.cpp and ResponsesCodec.cpp.

#include "ProviderAgent.hpp"

#include <functional>
#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

struct OpenAIResponsesConfig
{
    std::string api_key;
    std::string model{openai_profile().model};
    std::string endpoint{openai_profile().endpoint};
    std::function<void(const AgentUsage&)> usage_listener;
    // A tool call the adapter refused before it reached a card: the tool
    // name, the arguments as the model sent them, and why.
    std::function<void(const std::string&, const std::string&, const std::string&)> refusal_listener;
};

class OpenAIResponsesAgent final : public ProviderAgent
{
public:
    OpenAIResponsesAgent(OpenAIResponsesConfig config, std::unique_ptr<IAgentHttpTransport> transport);
};

std::unique_ptr<IAgentHttpTransport> make_openai_http_transport();

} // namespace Slic3r::GUI::JusPrin::Agent
