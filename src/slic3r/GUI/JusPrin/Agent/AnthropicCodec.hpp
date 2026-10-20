#pragma once

// Anthropic's Messages API wire format. Pure JSON in and out; see
// ProviderCodec.hpp. Unlike the Responses API, a call's arguments stream in
// fragments, so the codec keeps the blocks of the response being read and
// is reset before each request.

#include "ProviderCodec.hpp"

#include <map>

namespace Slic3r::GUI::JusPrin::Agent {

// A registry schema as Anthropic's strict mode accepts it: the keywords that
// mode rejects (minimum, maximum, multipleOf, minLength, maxLength, maxItems,
// minItems above 1, minProperties, maxProperties) move out of every node and
// into that node's description, as "(minimum 1, maximum 100)". Everything
// else is kept. The registry keeps the full schema, and native validation
// still enforces every keyword before a call runs.
nlohmann::json anthropic_strict_schema(const nlohmann::json& schema);

class AnthropicCodec final : public IProviderCodec
{
public:
    ProviderRequest          build(const TurnTranscript& transcript, const ProviderProfile& profile, RequestKind kind) const override;
    void                     reset() override;
    std::vector<StreamEvent> parse(const std::string& payload) override;

private:
    struct Block
    {
        nlohmann::json content;      // as content_block_start gave it, deltas applied
        std::string    partial_json; // tool_use: the input as it streams
    };

    StreamEvent finish();

    std::map<int, Block> m_blocks;
    AgentUsage           m_usage;
    std::string          m_stop_reason;
    nlohmann::json       m_stop_details;
};

} // namespace Slic3r::GUI::JusPrin::Agent
