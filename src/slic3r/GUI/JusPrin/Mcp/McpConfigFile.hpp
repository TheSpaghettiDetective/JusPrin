#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace Slic3r::GUI::JusPrin::Mcp {
// The client's configuration file refuses the edit, and the person can act on
// it: fix the file, copy the entry by hand, or review again. Its what() is
// the line shown, marked for translation. Any other failure is unexpected.
struct ConfigRefusal : std::runtime_error
{
    using std::runtime_error::runtime_error;
};
// The file changed after the person reviewed the edit.
struct StalePreview : ConfigRefusal
{
    using ConfigRefusal::ConfigRefusal;
};

struct ConfigEdit
{
    std::filesystem::path path;
    std::optional<std::string> before;
    std::string after;
    std::string preview;
    nlohmann::json previous;
    nlohmann::json next;
};

// Preserve bytes outside the JusPrin entry, including JSONC comments. Preparing
// an edit is read-only; applying it requires the user's separate confirmation.
ConfigEdit prepare_json_connection(const std::filesystem::path& path, const std::string& root,
                                   const nlohmann::json& server);
// Refuse a file changed since preview; return the recovery backup, if any.
std::filesystem::path apply_config_edit(const ConfigEdit& edit);
} // namespace Slic3r::GUI::JusPrin::Mcp
