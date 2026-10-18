#pragma once

// The per-plate summary of what OrcaSlicer warned about in a slice: how many
// warnings and the one to read first. Plain data, so the rule can be tested
// without the application.

#include "Workspace.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

// The summary of `findings`, numbered `sequence`; nothing when there are none.
inline std::optional<SliceWarnings> summarize_slice_warnings(const std::vector<SliceWarningFinding>& findings,
                                                             ProjectSessionId session, std::uint64_t sequence)
{
    if (findings.empty())
        return std::nullopt;
    // Critical first, then one the person has not ignored; otherwise
    // OrcaSlicer's order.
    const auto rank = [](const SliceWarningFinding& finding) { return (finding.critical ? 0 : 2) + (finding.ignored ? 1 : 0); };
    const auto top  = std::min_element(findings.begin(), findings.end(),
                                       [&rank](const SliceWarningFinding& a, const SliceWarningFinding& b) { return rank(a) < rank(b); });
    SliceWarnings summary;
    summary.count       = findings.size();
    summary.ignored     = std::size_t(std::count_if(findings.begin(), findings.end(),
                                                    [](const SliceWarningFinding& finding) { return finding.ignored; }));
    summary.severity    = top->critical ? "critical" : "warning";
    summary.message     = top->message;
    summary.setting     = top->setting;
    summary.code        = top->code;
    summary.object_name = top->object_name;
    if (top->object != 0)
        summary.object = ObjectId(session, top->object);
    summary.sequence = sequence;
    return summary;
}

// Whether `findings` holds a warning `known` (the ids of the plate's last
// record) does not: the moment a reader has something new to be told.
inline bool has_new_slice_warning(const std::vector<SliceWarningFinding>& findings, const std::set<std::string>& known)
{
    return std::any_of(findings.begin(), findings.end(),
                       [&known](const SliceWarningFinding& finding) { return known.count(finding.id) == 0; });
}

} // namespace Slic3r::GUI::JusPrin::Workspace
