#pragma once

// Which of the warnings OrcaSlicer stores on a slice result JusPrin shows: the
// bed-temperature, nozzle-hardness and timelapse checks the G-code processor
// runs after a slice (GCodeProcessorResult::warnings). The print-issue chip and
// slice_report both read this rule, so the person and the agent are told the
// same warnings.
//
// Which slice-result warnings JusPrin shows follows OrcaSlicer's send dialog
// (SelectMachineDialog, the loop over plate->get_slice_result()->warnings):
// the only place OrcaSlicer itself shows them. That rule is an if/else inside
// the dialog, not a list we can call. Making it callable would mean moving
// upstream lines in SelectMachine.cpp, which changes often upstream (about 367
// commits since 2025) and conflicts at rebase, so we copy the rule here
// instead. The guard test fails when OrcaSlicer adds a warning kind, so every
// new kind gets classified. It does not notice if OrcaSlicer changes its
// show/hide choice for an existing kind; whoever rebases a change to that loop
// must update this table. Accepted 2026-10-28. Known effect: OrcaSlicer shows
// these warnings only in its Bambu send dialogs, so for other printers
// OrcaSlicer shows none of them, and this table does not try to do better.

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

enum class SliceResultWarningShown {
    Always,
    // Only for a print that records a timelapse, which is chosen when the
    // print is sent; the chip does not count it before then.
    WhenRecordingTimelapse,
    Never,
};

struct SliceResultWarningRule
{
    // OrcaSlicer's id for the kind: one of the slice warning constants in
    // libslic3r/GCode/GCodeProcessor.hpp, never its display text.
    std::string             kind;
    SliceResultWarningShown shown;
    // OrcaSlicer stores some kinds twice under two error codes with the same
    // words; the send dialog shows them once.
    bool                    once;
};

// Every kind OrcaSlicer defines, each classified once.
const std::vector<SliceResultWarningRule>& slice_result_warning_rules();

// What OrcaSlicer's send dialog does with a warning of `kind`. A kind the
// table does not name, which a file from a newer OrcaSlicer could carry, is
// not shown, as the send dialog does not show it.
SliceResultWarningShown slice_result_warning_shown(const std::string& kind);

// Of a result's warnings, given by kind in OrcaSlicer's order, the positions
// of the ones shown: Never kinds left out, a `once` kind kept the first time.
std::vector<std::size_t> shown_slice_result_warnings(const std::vector<std::string>& kinds);

} // namespace Slic3r::GUI::JusPrin::Workspace
