#pragma once

#include <vector>

namespace Slic3r::GUI::JusPrin {

enum class PrintAction { Slice, SliceAll, Cancel, CheckPrint, Print, PrintAll, Export, Prepare };

struct PrintActionState
{
    bool slicing{false}, sliced{false}, can_slice{false}, can_slice_all{false}, needs_review{false};
    bool can_print{false}, can_print_all{false}, can_export{false}, preview{false};
    int plate_number{1}, plate_count{1}, sliced_count{0};
};

struct PrintActionItem { PrintAction action; bool enabled; };
struct PrimaryPrintAction
{
    PrintActionItem primary;
    std::vector<PrintActionItem> menu;
};

// The same reducer drives rendering, command eligibility and contract tests.
inline PrimaryPrintAction primary_print_action(const PrintActionState& state)
{
    PrimaryPrintAction result;
    if (state.slicing) {
        result.primary = {PrintAction::Cancel, false};
        result.menu = {{PrintAction::Cancel, true}};
    } else if (!state.sliced) {
        result.primary = {PrintAction::Slice, state.can_slice};
        if (state.plate_count > 1) result.menu.push_back({PrintAction::SliceAll, state.can_slice_all});
    } else {
        result.primary = state.needs_review ? PrintActionItem{PrintAction::CheckPrint, true} :
                                             PrintActionItem{PrintAction::Print, state.can_print};
        result.menu = {{state.needs_review ? PrintAction::Print : PrintAction::CheckPrint,
                        state.needs_review ? state.can_print : true},
                       {PrintAction::PrintAll, state.plate_count > 1 && state.can_print_all},
                       {PrintAction::Export, state.can_export}};
    }
    // Navigation stays available without changing what the primary button means.
    if (state.preview && !state.slicing) result.menu.push_back({PrintAction::Prepare, true});
    return result;
}

} // namespace Slic3r::GUI::JusPrin
