#include "ShellTheme.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

wxColour parse_color(const nlohmann::json& tokens, const char* group, const char* name)
{
    const nlohmann::json& value = tokens.at(group).at(name);
    wxColour color(wxString::FromUTF8(value.get<std::string>()));
    if (!color.IsOk())
        throw std::runtime_error(std::string("design token ") + group + "." + name + " is not a valid color");
    return color;
}

ShellPalette parse_palette(const nlohmann::json& mode_tokens)
{
    ShellPalette palette;
    palette.surface_canvas          = parse_color(mode_tokens, "surface", "canvas");
    palette.surface_subtle          = parse_color(mode_tokens, "surface", "subtle");
    palette.surface_raised          = parse_color(mode_tokens, "surface", "raised");
    palette.surface_selected        = parse_color(mode_tokens, "surface", "selected");
    palette.text_primary            = parse_color(mode_tokens, "text", "primary");
    palette.text_secondary          = parse_color(mode_tokens, "text", "secondary");
    palette.text_on_action          = parse_color(mode_tokens, "text", "onAction");
    palette.border_subtle           = parse_color(mode_tokens, "border", "subtle");
    palette.border_strong           = parse_color(mode_tokens, "border", "strong");
    palette.border_focus            = parse_color(mode_tokens, "border", "focus");
    palette.action_primary          = parse_color(mode_tokens, "action", "primary");
    palette.action_primary_hover    = parse_color(mode_tokens, "action", "primaryHover");
    palette.action_primary_pressed  = parse_color(mode_tokens, "action", "primaryPressed");
    palette.action_primary_text     = parse_color(mode_tokens, "action", "primaryText");
    palette.action_secondary        = parse_color(mode_tokens, "action", "secondary");
    palette.action_secondary_hover  = parse_color(mode_tokens, "action", "secondaryHover");
    palette.action_secondary_pressed = parse_color(mode_tokens, "action", "secondaryPressed");
    palette.action_secondary_text   = parse_color(mode_tokens, "action", "secondaryText");
    palette.action_secondary_border = parse_color(mode_tokens, "action", "secondaryBorder");
    palette.action_disabled         = parse_color(mode_tokens, "action", "disabled");
    palette.action_disabled_text    = parse_color(mode_tokens, "action", "disabledText");
    palette.status_warning          = parse_color(mode_tokens, "status", "warning");
    palette.status_success          = parse_color(mode_tokens, "status", "success");
    palette.status_danger           = parse_color(mode_tokens, "status", "danger");
    return palette;
}

} // namespace

ShellTheme ShellTheme::load_from_resources()
{
    const boost::filesystem::path path = boost::filesystem::path(resources_dir()) / "jusprin" / "ui" / "design-tokens.json";
    std::ifstream file(path.string());
    if (!file.is_open())
        throw std::runtime_error("design token file is missing: " + path.string());

    try {
        const nlohmann::json tokens = nlohmann::json::parse(file);
        ShellTheme theme;
        theme.m_light = parse_palette(tokens.at("semantic").at("light"));
        theme.m_dark  = parse_palette(tokens.at("semantic").at("dark"));
        return theme;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(std::string("design token file could not be parsed: ") + error.what());
    }
}

} // namespace Slic3r::GUI::JusPrin
