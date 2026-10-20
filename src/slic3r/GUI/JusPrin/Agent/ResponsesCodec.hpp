#pragma once

// The OpenAI Responses API wire format, for OpenAI and any host that serves a
// compatible endpoint. Pure JSON in and out; see ProviderCodec.hpp.

#include "ProviderCodec.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

class ResponsesCodec final : public IProviderCodec
{
public:
    ProviderRequest          build(const TurnTranscript& transcript, const ProviderProfile& profile, RequestKind kind) const override;
    void                     reset() override { m_completed = false; }
    std::vector<StreamEvent> parse(const std::string& payload) override;

private:
    // A response ends once; a second response.completed in the same stream
    // says nothing more, valid or not.
    bool m_completed{false};
};

} // namespace Slic3r::GUI::JusPrin::Agent
