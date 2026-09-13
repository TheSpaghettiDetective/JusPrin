#pragma once

#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterRecognition.hpp"

#include <optional>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// The harness's stand-in for OpenAI recognition. It follows the same
// poll-based contract, answers from the description alone, and never invents
// an identifier outside the supplied catalogue.
class FakePrinterRecognition final : public IPrinterRecognitionService
{
public:
    bool ready() const override { return true; }
    bool start(std::uint64_t generation, const PrinterEvidence& evidence,
               const std::vector<const PrinterCandidate*>& choices) override;
    void cancel() override { m_event.reset(); }
    std::optional<RecognitionEvent> poll() override;

private:
    std::optional<RecognitionEvent> m_event;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
