#include "fake_printer_recognition.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

// The first of the usual nozzle sizes the text names, or empty.
std::string stated_nozzle(const std::string& text)
{
    for (const char* size : {"0.2", "0.4", "0.6", "0.8"})
        if (text.find(size) != std::string::npos) return size;
    return {};
}

} // namespace

bool FakePrinterRecognition::start(std::uint64_t generation, const PrinterEvidence& evidence,
                                   const std::vector<const PrinterCandidate*>& choices)
{
    RecognitionEvent event;
    event.generation = generation;
    RecognitionResult result;
    const std::string description = lowercase(evidence.description);
    const std::size_t match_at = description.find("\ncurrent match: ");
    const std::size_t correction_at = description.rfind("\ncorrection: ");
    const std::string correction = correction_at == std::string::npos
        ? std::string() : evidence.description.substr(correction_at + std::string("\ncorrection: ").size());
    result.evidence_summary = evidence.description.empty() ? "the supplied photo" : evidence.description.substr(0, match_at);
    // A correction's nozzle wins; a correction without one keeps the current
    // match's nozzle, as the real provider is instructed to.
    result.nozzle_mm = stated_nozzle(correction_at == std::string::npos ? description : description.substr(correction_at));
    if (result.nozzle_mm.empty() && match_at != std::string::npos)
        result.nozzle_mm = stated_nozzle(description.substr(match_at));
    if (!correction.empty() && stated_nozzle(lowercase(correction)).empty())
        result.unresolved_correction = correction;
    // A correction names the current match, which already settled the model;
    // re-offering the touchscreen choice would discard the user's pick.
    if (match_at == std::string::npos && description.find("touchscreen") != std::string::npos) {
        static const std::array<std::string, 2> touchscreen_models = {
            "creality ender-3 v2", "creality ender-3 v2 neo"
        };
        for (const std::string& wanted : touchscreen_models) {
            const auto found = std::find_if(choices.begin(), choices.end(), [&](const PrinterCandidate* choice) {
                return choice && lowercase(choice->model_name) == wanted;
            });
            if (found != choices.end()) result.candidate_ids.push_back((*found)->id);
        }
        if (result.candidate_ids.size() == 2)
            result.assumption = "Two Ender 3s have a touchscreen. The V2 has a knob under the screen; the Neo doesn’t.";
    }
    if (result.candidate_ids.empty() && !description.empty()) {
        // The longest named model wins, so "A1 mini" is not read as "A1".
        const PrinterCandidate* named = nullptr;
        for (const PrinterCandidate* choice : choices)
            if (choice && description.find(lowercase(choice->model_name)) != std::string::npos &&
                (!named || choice->model_name.size() > named->model_name.size()))
                named = choice;
        if (named) result.candidate_ids = {named->id};
    }
    if (result.candidate_ids.empty()) {
        // Nothing named: offer the first two models as a choice.
        for (const PrinterCandidate* choice : choices) {
            if (result.candidate_ids.size() == 2) break;
            if (choice) result.candidate_ids.push_back(choice->id);
        }
    }
    if (result.candidate_ids.empty()) result.disposition = RecognitionDisposition::NoMatch;
    else if (result.candidate_ids.size() == 1) result.disposition = RecognitionDisposition::Recognized;
    else result.disposition = RecognitionDisposition::Ambiguous;
    event.result = std::move(result);
    m_event = std::move(event);
    return true;
}

std::optional<RecognitionEvent> FakePrinterRecognition::poll()
{
    auto event = std::move(m_event);
    m_event.reset();
    return event;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
