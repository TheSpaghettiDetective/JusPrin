#pragma once

#include "ShellTheme.hpp"
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r::GUI::JusPrin {
struct McpSetupResult { bool success; std::string diagnostic; };
McpSetupResult run_mcp_setup_command(wxWindow* parent, const std::vector<std::string>& argv);
void show_mcp_connection_dialog(wxWindow* parent, const ShellTheme& theme, const std::string& discovery_path,
                                const std::string& live_url, const std::string& startup_error);
}
