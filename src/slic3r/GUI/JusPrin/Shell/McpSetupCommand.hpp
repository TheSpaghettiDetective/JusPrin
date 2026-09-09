#pragma once

#include <functional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r::GUI::JusPrin {
struct McpSetupResult { bool success; std::string diagnostic; };
McpSetupResult run_mcp_setup_command(wxWindow* parent, const std::vector<std::string>& argv);
void start_mcp_setup_command(wxWindow* parent, const std::vector<std::string>& arguments,
                             std::function<void(McpSetupResult)> done);
}
