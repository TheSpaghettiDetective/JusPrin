#pragma once

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {
// Bounded canonical read results, shared by all adapters.
inline constexpr std::size_t kToolListLimit = 64;
inline constexpr std::size_t kToolLabelLimit = 256;
nlohmann::json workspace_inspection(const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json selection_inspection(const Workspace::WorkspaceSnapshot& snapshot);
} // namespace Slic3r::GUI::JusPrin::Agent
