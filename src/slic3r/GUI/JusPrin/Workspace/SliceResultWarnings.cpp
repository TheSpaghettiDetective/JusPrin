#include "SliceResultWarnings.hpp"

#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::Workspace {

const std::vector<SliceResultWarningRule>& slice_result_warning_rules()
{
    using Shown = SliceResultWarningShown;
    static const std::vector<SliceResultWarningRule> rules{
        {NOZZLE_HRC_CHECKER, Shown::Always, false},
        // Shown only while the dialog's timelapse box is ticked, and once
        // although the processor stores it as 10018003 and again as 1000C003
        // for older A-series firmware.
        {NOT_SUPPORT_TRADITIONAL_TIMELAPSE, Shown::WhenRecordingTimelapse, true},
        {SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER, Shown::WhenRecordingTimelapse, false},
        // Left out of the dialog's warning list; its status line says why no
        // timelapse is recorded instead.
        {NOT_GENERATE_TIMELAPSE, Shown::Never, false},
        // The dialog never names it.
        {BED_TEMP_TOO_HIGH_THAN_FILAMENT, Shown::Never, false},
        // Nothing raises it, and OrcaSlicer has no words for it.
        {LONG_RETRACTION_WHEN_CUT, Shown::Never, false},
    };
    return rules;
}

SliceResultWarningShown slice_result_warning_shown(const std::string& kind)
{
    const auto& rules = slice_result_warning_rules();
    const auto  found = std::find_if(rules.begin(), rules.end(), [&kind](const SliceResultWarningRule& rule) { return rule.kind == kind; });
    return found == rules.end() ? SliceResultWarningShown::Never : found->shown;
}

std::vector<std::size_t> shown_slice_result_warnings(const std::vector<std::string>& kinds)
{
    const auto&              rules = slice_result_warning_rules();
    std::vector<std::size_t> shown;
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto rule = std::find_if(rules.begin(), rules.end(),
                                       [&](const SliceResultWarningRule& candidate) { return candidate.kind == kinds[index]; });
        if (rule == rules.end() || rule->shown == SliceResultWarningShown::Never)
            continue;
        if (rule->once && std::any_of(shown.begin(), shown.end(), [&](std::size_t earlier) { return kinds[earlier] == kinds[index]; }))
            continue;
        shown.push_back(index);
    }
    return shown;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
