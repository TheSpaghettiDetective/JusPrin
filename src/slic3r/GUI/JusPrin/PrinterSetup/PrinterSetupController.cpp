#include "PrinterSetupController.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

constexpr const char* kCurrentMatchMarker = "\nCurrent match: ";

} // namespace

PrinterSetupController::PrinterSetupController(PrinterCatalog catalog,
                                               std::unique_ptr<IPrinterRecognitionService> recognition,
                                               ApplyFn apply)
    : m_catalog(std::move(catalog)), m_recognition(std::move(recognition)), m_apply(std::move(apply)) {}

PrinterSetupController::~PrinterSetupController()
{
    if (m_recognition) m_recognition->cancel();
}

void PrinterSetupController::fail(std::string message, bool retryable)
{
    m_state = FlowState::Error;
    m_error = std::move(message);
    m_retryable = retryable;
}

bool PrinterSetupController::recognize(PrinterEvidence evidence)
{
    if (evidence.description.empty() && evidence.image_bytes.empty()) return false;
    m_evidence = std::move(evidence);
    m_candidates.clear();
    m_error.clear();
    m_retryable = false;
    m_apply_failed = false;
    m_uncertain = false;
    m_unresolved_correction.clear();
    ++m_generation;
    const auto choices = m_catalog.models();
    m_allowed_candidate_ids.clear();
    for (const PrinterCandidate* choice : choices)
        if (choice) m_allowed_candidate_ids.insert(choice->id);
    if (!m_recognition || !m_recognition->ready()) {
        fail("Printer recognition needs the OpenAI key configured in the Agent panel. You can still use a network printer or set it up manually.", false);
        return false;
    }
    if (!m_recognition->start(m_generation, m_evidence, choices)) {
        fail("Printer recognition could not start.", true);
        return false;
    }
    m_state = FlowState::Recognizing;
    return true;
}

void PrinterSetupController::poll()
{
    if (!m_recognition) return;
    while (auto event = m_recognition->poll()) accept(*event);
}

void PrinterSetupController::accept(const RecognitionEvent& event)
{
    if (event.generation != m_generation || m_state != FlowState::Recognizing) return;
    if (event.error) {
        fail(event.error->message, event.error->retryable);
        return;
    }
    if (!event.result) return;
    std::set<std::string> seen;
    for (const std::string& id : event.result->candidate_ids) {
        if (!seen.insert(id).second) continue;
        if (!m_allowed_candidate_ids.count(id)) continue;
        if (const PrinterCandidate* candidate = m_catalog.find(id)) m_candidates.push_back(candidate);
    }
    m_evidence_summary = event.result->evidence_summary;
    m_assumption = event.result->assumption;
    m_unresolved_correction = event.result->unresolved_correction;
    const RecognitionDisposition disposition = event.result->disposition;
    if (disposition == RecognitionDisposition::Recognized && m_candidates.size() == 1) {
        // The provider names a model at its standard nozzle. A stated nozzle
        // picks that variant; one the model does not ship is not applied.
        const std::string& nozzle = event.result->nozzle_mm;
        const PrinterCandidate& model = *m_candidates.front();
        const auto& all = m_catalog.candidates();
        const auto stated = std::find_if(all.begin(), all.end(), [&](const PrinterCandidate& item) {
            return item.vendor_id == model.vendor_id && item.model_id == model.model_id && item.variant == nozzle;
        });
        if (stated != all.end())
            m_candidates = {&*stated};
        else if (!nozzle.empty())
            m_unresolved_correction = nozzle + " mm nozzle" + (m_unresolved_correction.empty() ? "" : ", " + m_unresolved_correction);
        m_state = FlowState::Recognized;
    } else if ((disposition == RecognitionDisposition::Ambiguous && m_candidates.size() >= 2) ||
               (disposition != RecognitionDisposition::NoMatch && m_candidates.size() == 1)) {
        // One surviving candidate here is either an ambiguous answer with a
        // single plausible model or one whose other IDs failed validation.
        // Either way the user chooses; nothing is presumed.
        m_uncertain = m_candidates.size() == 1;
        m_state = FlowState::Ambiguous;
    } else {
        m_candidates.clear();
        fail("That evidence did not match a supported printer profile. Add a clearer description or photo, or set it up manually.", false);
    }
}

bool PrinterSetupController::use_discovered(const DiscoveredPrinter& printer)
{
    const PrinterCandidate* candidate = m_catalog.find_device_model(printer.device_model_id);
    if (!candidate) {
        fail("The network printer was found, but its model does not match a supported local profile.", false);
        return false;
    }
    if (m_recognition) m_recognition->cancel();
    ++m_generation;
    m_evidence = {};
    m_evidence.discovered_device = printer;
    m_allowed_candidate_ids.clear();
    m_candidates = {candidate};
    m_evidence_summary = printer.name;
    m_assumption = printer.connection.empty() ? "Found on the local network."
                                               : "Connected · " + printer.connection;
    m_error.clear();
    m_retryable = false;
    m_apply_failed = false;
    m_uncertain = false;
    m_unresolved_correction.clear();
    m_state = FlowState::Recognized;
    return true;
}

bool PrinterSetupController::choose(const std::string& candidate_id)
{
    if (m_state != FlowState::Ambiguous) return false;
    const auto found = std::find_if(m_candidates.begin(), m_candidates.end(), [&](const PrinterCandidate* candidate) {
        return candidate && candidate->id == candidate_id;
    });
    if (found == m_candidates.end()) return false;
    const PrinterCandidate* candidate = *found;
    m_candidates = {candidate};
    m_uncertain = false;
    m_state = FlowState::Recognized;
    return true;
}

bool PrinterSetupController::correct(const std::string& correction)
{
    if (m_state != FlowState::Recognized || m_candidates.size() != 1 || correction.empty()) return false;
    const PrinterCandidate& current = *m_candidates.front();
    PrinterEvidence evidence = m_evidence;
    // A second correction replaces the first rather than stacking on it.
    if (const auto marker = evidence.description.find(kCurrentMatchMarker); marker != std::string::npos)
        evidence.description.resize(marker);
    evidence.description += kCurrentMatchMarker + current.model_name + " " + current.variant +
                            " mm nozzle\nCorrection: " + correction;
    return recognize(std::move(evidence));
}

bool PrinterSetupController::clarify(const std::string& detail)
{
    if (m_state != FlowState::Ambiguous || detail.empty()) return false;
    PrinterEvidence evidence = m_evidence;
    if (!evidence.description.empty()) evidence.description += "\n";
    evidence.description += detail;
    return recognize(std::move(evidence));
}

bool PrinterSetupController::confirm()
{
    if (m_state != FlowState::Recognized || m_candidates.size() != 1 || !m_apply) return false;
    std::string error;
    if (!m_apply(*m_candidates.front(), error)) {
        fail(error.empty() ? "The printer setup could not be applied. Your previous setup is unchanged." : error, true);
        m_apply_failed = true;
        return false;
    }
    m_state = FlowState::Complete;
    return true;
}

bool PrinterSetupController::retry()
{
    if (m_state != FlowState::Error || !m_retryable) return false;
    if (m_apply_failed) {
        // The validated candidate is still held; only the apply step failed.
        m_apply_failed = false;
        m_error.clear();
        m_retryable = false;
        m_state = FlowState::Recognized;
        return true;
    }
    return recognize(m_evidence);
}

void PrinterSetupController::start_over()
{
    if (m_recognition) m_recognition->cancel();
    ++m_generation;
    m_state = FlowState::Initial;
    m_evidence = {};
    m_candidates.clear();
    m_allowed_candidate_ids.clear();
    m_evidence_summary.clear();
    m_assumption.clear();
    m_error.clear();
    m_retryable = false;
    m_apply_failed = false;
    m_uncertain = false;
    m_unresolved_correction.clear();
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
