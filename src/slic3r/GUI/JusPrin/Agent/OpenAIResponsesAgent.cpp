#include "OpenAIResponsesAgent.hpp"
#include "ResponsesCodec.hpp"

#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

ProviderAgentConfig provider_config(OpenAIResponsesConfig config)
{
    ProviderAgentConfig result;
    result.profile          = openai_profile();
    result.profile.model    = std::move(config.model);
    result.profile.endpoint = std::move(config.endpoint);
    result.api_key          = std::move(config.api_key);
    result.usage_listener   = std::move(config.usage_listener);
    result.refusal_listener = std::move(config.refusal_listener);
    return result;
}

} // namespace

OpenAIResponsesAgent::OpenAIResponsesAgent(OpenAIResponsesConfig config, std::unique_ptr<IAgentHttpTransport> transport)
    : ProviderAgent(provider_config(std::move(config)), std::make_unique<ResponsesCodec>(), std::move(transport))
{}

} // namespace Slic3r::GUI::JusPrin::Agent
