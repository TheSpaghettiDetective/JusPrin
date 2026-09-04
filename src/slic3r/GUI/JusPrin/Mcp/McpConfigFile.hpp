#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Mcp {
struct ConfigEdit
{
    std::filesystem::path path;
    std::optional<std::string> before;
    std::string after;
    std::string preview;
};

// Preserve bytes outside the JusPrin entry, including JSONC comments. Preparing
// an edit is read-only; applying it requires the user's separate confirmation.
ConfigEdit prepare_json_connection(const std::filesystem::path& path, const std::string& root,
                                   const nlohmann::json& server);
// Refuse a file changed since preview; return the recovery backup, if any.
std::filesystem::path apply_config_edit(const ConfigEdit& edit);
} // namespace Slic3r::GUI::JusPrin::Mcp
