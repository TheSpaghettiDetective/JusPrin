#include "fake_printer_recognition.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
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
    // A nozzle size counts only where the user stated it: in the correction
    // when there is one, since the current-match line names the old variant.
    const std::string variant_text = correction_at == std::string::npos ? description : description.substr(correction_at);
    result.evidence_summary = evidence.description.empty() ? "the supplied photo" : evidence.description.substr(0, match_at);
    result.confidence = 1.;
    // A correction names the current match, which already settled the model;
    // re-offering the touchscreen choice would discard the user's pick.
    if (match_at == std::string::npos && description.find("touchscreen") != std::string::npos) {
        static const std::array<std::string, 2> touchscreen_models = {
            "creality ender-3 v2", "creality ender-3 v2 neo"
        };
        for (const std::string& wanted : touchscreen_models) {
            const auto found = std::find_if(choices.begin(), choices.end(), [&](const PrinterCandidate* choice) {
                return choice && lowercase(choice->model_name) == wanted && choice->variant == "0.4";
            });
            if (found != choices.end()) result.candidate_ids.push_back((*found)->id);
        }
        if (result.candidate_ids.size() == 2)
            result.assumption = "Two Ender 3s have a touchscreen. The V2 has a knob under the screen; the Neo doesn’t.";
    }
    if (result.candidate_ids.empty() && !description.empty()) {
        // The longest named model wins, so "A1 mini" is not read as "A1";
        // within it a stated nozzle beats the standard one.
        const PrinterCandidate* stated = nullptr;
        const PrinterCandidate* standard = nullptr;
        for (const PrinterCandidate* choice : choices) {
            if (!choice || description.find(lowercase(choice->model_name)) == std::string::npos) continue;
            const PrinterCandidate* best = stated ? stated : standard;
            if (best && choice->model_name.size() < best->model_name.size()) continue;
            if (best && choice->model_name.size() > best->model_name.size()) stated = standard = nullptr;
            if (!stated && variant_text.find(choice->variant) != std::string::npos) stated = choice;
            if (!standard && choice->variant == "0.4") standard = choice;
        }
        if (const PrinterCandidate* chosen = stated ? stated : standard) {
            result.candidate_ids = {chosen->id};
            if (!correction.empty() && chosen != stated) result.unresolved_correction = correction;
        }
    }
    if (result.candidate_ids.empty()) {
        result.confidence = .5;
        std::vector<const PrinterCandidate*> distinct;
        std::map<std::string, std::size_t> model_indexes;
        for (const PrinterCandidate* choice : choices) {
            if (!choice) continue;
            const std::string model_key = choice->vendor_id + "\n" + choice->model_id;
            const auto [found, inserted] = model_indexes.emplace(model_key, distinct.size());
            if (inserted)
                distinct.push_back(choice);
            else if (choice->variant == "0.4" && distinct[found->second]->variant != "0.4")
                distinct[found->second] = choice;
        }
        const size_t count = std::min<size_t>(distinct.size(), 2);
        for (size_t i = 0; i < count; ++i)
            result.candidate_ids.push_back(distinct[i]->id);
    }
    if (result.candidate_ids.empty()) result.disposition = RecognitionDisposition::NoMatch;
    else if (result.candidate_ids.size() == 1) result.disposition = RecognitionDisposition::Recognized;
    else result.disposition = RecognitionDisposition::Ambiguous;
    if (result.assumption.empty())
        result.assumption = "The standard nozzle variant is selected unless the evidence names another size.";
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
