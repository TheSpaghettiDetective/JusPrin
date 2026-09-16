#pragma once

#include "ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {
// Bounded canonical read results, shared by all adapters.
inline constexpr std::size_t kToolListLimit = 64;
inline constexpr std::size_t kToolLabelLimit = 256;
// Prose the agent writes for a person to read -- one intent answer, one plan
// decision -- rather than a label projected from Orca state.
inline constexpr std::size_t kToolTextLimit = 2048;
// Which sections of workspace_inspect a call asked for. Summary is what every
// caller got before sections existed, and what a caller that names none gets.
struct InspectSections
{
    bool summary{true};
    bool intent{false};
    bool plan{false};
};

nlohmann::json workspace_inspection(const Workspace::WorkspaceSnapshot& snapshot, InspectSections sections = {});
nlohmann::json intent_section_result(const std::vector<IntentField>& fields);
nlohmann::json plan_section_result(const PlanRecord& plan);
nlohmann::json selection_inspection(const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json settings_search_result(const Workspace::SettingsSearchResult&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_read_result(const Workspace::SettingsReadResult&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_preview_result(const Workspace::SettingsPreview&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_apply_result(const Workspace::SettingsPreview&, const Workspace::WorkspaceSnapshot&, bool applied);
nlohmann::json setting_issue_result(const Workspace::SettingIssue&);
} // namespace Slic3r::GUI::JusPrin::Agent
