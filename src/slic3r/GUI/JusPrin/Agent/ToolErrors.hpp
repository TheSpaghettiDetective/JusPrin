#pragma once

// Every code a tool call can fail with. The in-app agent and MCP clients read
// the same spelling. Each code stands for one next step, named in the comment
// above its group; the message says what happened, including what a call that
// stopped part-way had already changed. ToolExecutionCoordinator::fail refuses
// a code that is not listed here, so a new failure picks the code whose next
// step fits instead of inventing one. The table in the tool guide's "Error
// codes" section lists the same codes for people.

#include <algorithm>
#include <iterator>
#include <string_view>

namespace Slic3r::GUI::JusPrin::Agent::ToolErrors {

inline constexpr std::string_view kCodes[] = {
    // Fix the arguments and call again.
    "invalid_arguments",
    "unknown_tool",
    "unknown_skill",
    // Not run because an earlier call of the same response failed; decide again.
    "not_run",
    // Read the state again, then call again.
    "not_found",       // an id that is not in the open project
    "stale_workspace", // the project changed since the read the call was based on
    "feature_expired", // a face, edge or hole handle from an earlier revision
    "connection_lost", // MCP bridge only: the outcome is unknown
    // Wait, then call again.
    "busy",
    // Do the step the message names first, with another tool.
    "needs_step",
    // Ask the person: the decision or the action is theirs.
    "opened_before",
    "workspace_unavailable", // no open project, or no FFF printer selected
    // Stop and tell the person.
    "refused",           // OrcaSlicer does not do this here
    "failed",            // OrcaSlicer tried and failed
    "local_save_failed", // the project could not be saved
    "cancelled",

    // The settings tools' corrections (tool guide, "Error codes").
    "unknown_setting",
    "unsupported_scope",
    "invalid_setting_value",
    "incompatible_settings",
    "unsupported_setting_mutation",
    "withheld_setting",
    "unknown_preset",
    "incompatible_preset",
    "unsupported_plate",
    "not_selected",
    "read_only_preset",
    "name_taken",
    "invalid_preset_name",
    "not_in_project",

    // The printer and filament chats' own refusals.
    "unknown_nozzle",
    "not_in_this_conversation",
    "no_printer",
    "other_printer",
    "stock_profile",
    "unknown_printer",
    "connection_unavailable",
    "unknown_device",
    "address_needed",
    "too_many",
    "already_added",
    "add_failed",
    "change_failed",
    "nothing_to_change",
    "not_saved",

    // The internal record tools, which only the page calls.
    "slice_required",
    "missing_plate",
    "missing_build",
    "invalid_progress",
};

inline bool registered(std::string_view code)
{
    return std::find(std::begin(kCodes), std::end(kCodes), code) != std::end(kCodes);
}

} // namespace Slic3r::GUI::JusPrin::Agent::ToolErrors
