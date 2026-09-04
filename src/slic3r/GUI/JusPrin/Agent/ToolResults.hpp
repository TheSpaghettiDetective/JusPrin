#pragma once

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {
// Bounded canonical read results, shared by all adapters.
inline constexpr std::size_t kToolListLimit = 64;
inline constexpr std::size_t kToolLabelLimit = 256;
nlohmann::json workspace_inspection(const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json selection_inspection(const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json settings_search_result(const Workspace::SettingsSearchResult&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_read_result(const Workspace::SettingsReadResult&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_preview_result(const Workspace::SettingsPreview&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_apply_result(const Workspace::SettingsPreview&, const Workspace::WorkspaceSnapshot&, bool applied);
nlohmann::json setting_issue_result(const Workspace::SettingIssue&);
} // namespace Slic3r::GUI::JusPrin::Agent
