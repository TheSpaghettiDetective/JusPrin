#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Mcp {
struct ConnectionEntry
{
    std::string id, name, text;
    bool cli{false};
    std::vector<std::string> arguments;
    std::string subtitle;
};

// All paths are literal arguments, never interpolated shell fragments. Windows
// copy commands target PowerShell; other platforms target a POSIX shell.
inline std::string quote_argument(const std::string& value, bool powershell)
{
    std::string result = "'";
    for (char c : value) result += c == '\'' ? (powershell ? "''" : "'\"'\"'") : std::string(1, c);
    return result + "'";
}

inline std::vector<ConnectionEntry> connection_entries(const std::string& bridge, const std::string& discovery, bool windows,
                                                      std::vector<std::string> launch_arguments = {})
{
    using nlohmann::json;
    launch_arguments.insert(launch_arguments.end(), {"--discovery", discovery});
    const json server{{"command", bridge}, {"args", launch_arguments}};
    std::string invocation = quote_argument(bridge, windows);
    for (const auto& argument : launch_arguments) invocation += " " + quote_argument(argument, windows);
    std::vector<std::string> claude{"mcp", "add", "--scope", "user", "--transport", "stdio", "jusprin", "--", bridge};
    claude.insert(claude.end(), launch_arguments.begin(), launch_arguments.end());
    std::vector<std::string> codex{"mcp", "add", "jusprin", "--", bridge};
    codex.insert(codex.end(), launch_arguments.begin(), launch_arguments.end());
    const std::string desktop = json{{"mcpServers", {{"jusprin", server}}}}.dump(2);
    auto vscode = server;
    vscode["type"] = "stdio";
    return {
        {"claude", "Claude Code", "claude mcp add --scope user --transport stdio jusprin -- " + invocation, true,
            std::move(claude)},
        {"codex", "Codex", "codex mcp add jusprin -- " + invocation, true,
            std::move(codex)},
        {"desktop", "Claude Desktop", desktop},
        {"cowork", "Cowork", desktop, false, {}, "local sessions"},
        {"cursor", "Cursor", desktop},
        {"code", "VS Code", json{{"servers", {{"jusprin", vscode}}}}.dump(2)}
    };
}
} // namespace Slic3r::GUI::JusPrin::Mcp
