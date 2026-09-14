#pragma once

#include "PrinterCatalog.hpp"
#include "PrinterRecognition.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

enum class FlowState { Initial, Recognizing, Recognized, Ambiguous, Error, Complete };

// A single recognized candidate below this provider confidence is offered as
// a choice the user must make rather than presented as the identification.
constexpr double kRecognitionConfidenceThreshold = 0.7;

class PrinterSetupController
{
public:
    using ApplyFn = std::function<bool(const PrinterCandidate&, std::string&)>;

    PrinterSetupController(PrinterCatalog catalog, std::unique_ptr<IPrinterRecognitionService> recognition,
                           ApplyFn apply);
    ~PrinterSetupController();

    FlowState state() const { return m_state; }
    const PrinterEvidence& evidence() const { return m_evidence; }
    const std::vector<const PrinterCandidate*>& candidates() const { return m_candidates; }
    const std::string& evidence_summary() const { return m_evidence_summary; }
    const std::string& assumption() const { return m_assumption; }
    const std::string& error() const { return m_error; }
    bool retryable() const { return m_retryable; }
    // Ambiguous because the provider was not confident, not because several
    // models matched.
    bool uncertain() const { return m_uncertain; }
    const std::string& unresolved_correction() const { return m_unresolved_correction; }
    const PrinterCatalog& catalog() const { return m_catalog; }

    bool recognize(PrinterEvidence evidence);
    void poll();
    bool use_discovered(const DiscoveredPrinter& printer);
    bool choose(const std::string& candidate_id);
    // Re-recognizes the current evidence with the correction and the current
    // match attached, keeping any photo and network device.
    bool correct(const std::string& correction);
    // Re-recognizes an ambiguous description with the detail the user added.
    bool clarify(const std::string& detail);
    bool confirm();
    // Repeats whatever failed: the apply step, or the recognition request.
    bool retry();
    void start_over();

private:
    void accept(const RecognitionEvent& event);
    void fail(std::string message, bool retryable);

    PrinterCatalog m_catalog;
    std::unique_ptr<IPrinterRecognitionService> m_recognition;
    ApplyFn m_apply;
    FlowState m_state{FlowState::Initial};
    PrinterEvidence m_evidence;
    std::vector<const PrinterCandidate*> m_candidates;
    std::set<std::string> m_allowed_candidate_ids;
    std::string m_evidence_summary;
    std::string m_assumption;
    std::string m_error;
    bool m_retryable{false};
    bool m_apply_failed{false};
    bool m_uncertain{false};
    std::string m_unresolved_correction;
    std::uint64_t m_generation{0};
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
