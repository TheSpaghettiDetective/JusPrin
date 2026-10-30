#pragma once

// The tools the project chat offers a turn with no skills listed, in the
// registry's order: every in-app tool whose availability is Always, as
// ProviderAgent's available_in_app picks them for a session that names no
// tools of its own. Tests about a request's tools compare with this, so a new
// tool fails only test_tool_registry.cpp, the one place that lists them.

#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"

#include <string>
#include <vector>

namespace JusPrinTest {
inline std::vector<std::string> project_chat_tool_names()
{
    using namespace Slic3r::GUI::JusPrin::Agent;
    std::vector<std::string> names;
    for (const ToolDefinition& definition : ToolRegistry::instance().exposed(ToolExposure::InApp))
        if (definition.availability == ToolAvailability::Always)
            names.push_back(definition.name);
    return names;
}
}
