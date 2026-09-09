#include "ShellTheme.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

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
    palette.status_success_on_action = parse_color(mode_tokens, "status", "successOnAction");
    palette.status_danger           = parse_color(mode_tokens, "status", "danger");
    return palette;
}

// A DIP value or a font weight: the token file promises whole numbers, so
// anything else is a malformed file rather than something to round.
int parse_int(const nlohmann::json& group, const std::string& path, const char* name)
{
    const nlohmann::json& value = group.at(name);
    if (!value.is_number_integer())
        throw std::runtime_error("design token " + path + "." + name + " is not a whole number");
    return value.get<int>();
}

TextRole parse_text_role(const nlohmann::json& recipe, const std::string& path)
{
    const std::string name = recipe.at("textRole").get<std::string>();
    if (name == "pageTitle") return TextRole::PageTitle;
    if (name == "section")   return TextRole::Section;
    if (name == "body")      return TextRole::Body;
    if (name == "bodyBold")  return TextRole::BodyBold;
    if (name == "label")     return TextRole::Label;
    if (name == "labelBold") return TextRole::LabelBold;
    if (name == "metadata")  return TextRole::Metadata;
    throw std::runtime_error("design token " + path + ".textRole names an unknown role: " + name);
}

TypeStyle parse_type_style(const nlohmann::json& roles, const char* name)
{
    const std::string path = std::string("typography.roles.") + name;
    const nlohmann::json& role = roles.at(name);
    TypeStyle style;
    style.size        = parse_int(role, path, "size");
    style.line_height = parse_int(role, path, "lineHeight");
    style.weight      = parse_int(role, path, "weight");
    if (style.weight != 400 && style.weight != 700)
        throw std::runtime_error("design token " + path + ".weight must be 400 or 700");
    return style;
}

ButtonMetrics parse_buttons(const nlohmann::json& buttons)
{
    ButtonMetrics b;
    // Each recipe reads exactly the fields the token file gives it, so a
    // recipe that loses a field fails loudly instead of measuring as 0.
    const nlohmann::json& compact = buttons.at("compact");
    b.compact.padding_x = parse_int(compact, "component.button.compact", "paddingX");
    b.compact.padding_y = parse_int(compact, "component.button.compact", "paddingY");
    b.compact.radius    = parse_int(compact, "component.button.compact", "radius");
    b.compact.text_role = parse_text_role(compact, "component.button.compact");

    const nlohmann::json& window = buttons.at("window");
    b.window.min_width = parse_int(window, "component.button.window", "minWidth");
    b.window.height    = parse_int(window, "component.button.window", "height");
    b.window.radius    = parse_int(window, "component.button.window", "radius");
    b.window.text_role = parse_text_role(window, "component.button.window");

    const nlohmann::json& choice = buttons.at("choice");
    b.choice.min_width = parse_int(choice, "component.button.choice", "minWidth");
    b.choice.height    = parse_int(choice, "component.button.choice", "height");
    b.choice.padding_x = parse_int(choice, "component.button.choice", "paddingX");
    b.choice.padding_y = parse_int(choice, "component.button.choice", "paddingY");
    b.choice.radius    = parse_int(choice, "component.button.choice", "radius");
    b.choice.text_role = parse_text_role(choice, "component.button.choice");

    const nlohmann::json& parameter = buttons.at("parameter");
    b.parameter.width     = parse_int(parameter, "component.button.parameter", "width");
    b.parameter.height    = parse_int(parameter, "component.button.parameter", "height");
    b.parameter.radius    = parse_int(parameter, "component.button.parameter", "radius");
    b.parameter.text_role = parse_text_role(parameter, "component.button.parameter");

    const nlohmann::json& icon = buttons.at("icon");
    b.icon.width     = parse_int(icon, "component.button.icon", "width");
    b.icon.height    = parse_int(icon, "component.button.icon", "height");
    b.icon.icon_size = parse_int(icon, "component.button.icon", "iconSize");
    b.icon.radius    = parse_int(icon, "component.button.icon", "radius");

    const nlohmann::json& expanded = buttons.at("expanded");
    b.expanded.min_height = parse_int(expanded, "component.button.expanded", "minHeight");
    b.expanded.padding_x  = parse_int(expanded, "component.button.expanded", "paddingX");
    b.expanded.padding_y  = parse_int(expanded, "component.button.expanded", "paddingY");
    b.expanded.radius     = parse_int(expanded, "component.button.expanded", "radius");
    b.expanded.text_role  = parse_text_role(expanded, "component.button.expanded");
    return b;
}

ShellMetrics parse_metrics(const nlohmann::json& tokens)
{
    ShellMetrics m;
    const nlohmann::json& radius = tokens.at("dimension").at("radius");
    m.radius_standard  = parse_int(radius, "dimension.radius", "standard");
    m.radius_compact   = parse_int(radius, "dimension.radius", "compact");
    m.radius_container = parse_int(radius, "dimension.radius", "container");
    m.radius_window    = parse_int(radius, "dimension.radius", "window");
    m.radius_pill      = parse_int(radius, "dimension.radius", "pill");

    const nlohmann::json& space = tokens.at("dimension").at("space");
    m.space_1  = parse_int(space, "dimension.space", "1");
    m.space_2  = parse_int(space, "dimension.space", "2");
    m.space_3  = parse_int(space, "dimension.space", "3");
    m.space_4  = parse_int(space, "dimension.space", "4");
    m.space_5  = parse_int(space, "dimension.space", "5");
    m.space_6  = parse_int(space, "dimension.space", "6");
    m.space_8  = parse_int(space, "dimension.space", "8");
    m.space_10 = parse_int(space, "dimension.space", "10");
    m.space_12 = parse_int(space, "dimension.space", "12");

    const nlohmann::json& component = tokens.at("component");
    m.button = parse_buttons(component.at("button"));

    const nlohmann::json& chip = component.at("chip");
    m.chip.height = parse_int(chip, "component.chip", "height");
    m.chip.radius = parse_int(chip, "component.chip", "radius");

    const nlohmann::json& menu_row = component.at("menuRow");
    m.menu_row.height = parse_int(menu_row, "component.menuRow", "height");
    m.menu_row.radius = parse_int(menu_row, "component.menuRow", "radius");
    m.menu_row.side_inset = parse_int(menu_row, "component.menuRow", "sideInset");

    const nlohmann::json& popover = component.at("popover");
    m.popover.padding_y = parse_int(popover, "component.popover", "paddingY");
    m.popover.row_gap   = parse_int(popover, "component.popover", "rowGap");
    m.popover.radius    = parse_int(popover, "component.popover", "radius");

    m.status_row.height = parse_int(component.at("statusRow"), "component.statusRow", "height");

    const nlohmann::json& agent_pane = component.at("agentPane");
    m.agent_pane.min_width                = parse_int(agent_pane, "component.agentPane", "minWidth");
    m.agent_pane.workspace_min_width      = parse_int(agent_pane, "component.agentPane", "workspaceMinWidth");
    m.agent_pane.resize_handle_width      = parse_int(agent_pane, "component.agentPane", "resizeHandleWidth");
    m.agent_pane.resize_handle_line_width = parse_int(agent_pane, "component.agentPane", "resizeHandleLineWidth");
    m.agent_pane.drag_collapse_width      = parse_int(agent_pane, "component.agentPane", "dragCollapseWidth");

    const nlohmann::json& swatch = component.at("swatch");
    m.swatch.size   = parse_int(swatch, "component.swatch", "size");
    m.swatch.radius = parse_int(swatch, "component.swatch", "radius");
    return m;
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
        theme.m_light   = parse_palette(tokens.at("semantic").at("light"));
        theme.m_dark    = parse_palette(tokens.at("semantic").at("dark"));
        theme.m_metrics = parse_metrics(tokens);

        const nlohmann::json& roles = tokens.at("typography").at("roles");
        theme.m_type_styles[size_t(TextRole::PageTitle)] = parse_type_style(roles, "pageTitle");
        theme.m_type_styles[size_t(TextRole::Section)]   = parse_type_style(roles, "section");
        theme.m_type_styles[size_t(TextRole::Body)]      = parse_type_style(roles, "body");
        theme.m_type_styles[size_t(TextRole::BodyBold)]  = parse_type_style(roles, "bodyBold");
        theme.m_type_styles[size_t(TextRole::Label)]     = parse_type_style(roles, "label");
        theme.m_type_styles[size_t(TextRole::LabelBold)] = parse_type_style(roles, "labelBold");
        theme.m_type_styles[size_t(TextRole::Metadata)]  = parse_type_style(roles, "metadata");
        return theme;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(std::string("design token file could not be parsed: ") + error.what());
    }
}

const wxFont& ShellTheme::font(TextRole role) const
{
    wxFont& font = m_fonts[size_t(role)];
    if (font.IsOk())
        return font;
    const TypeStyle& style = type_style(role);
    font = Label::sysFont(style.size, style.weight == 700);
    return font;
}

const wxFont& ShellTheme::mono_font(TextRole role) const
{
    wxFont& font = m_mono_fonts[size_t(role)];
    if (font.IsOk())
        return font;
    // Sized from the built role font rather than the raw token so it follows
    // the same platform point-size scaling Label::sysFont applies.
    font = wxFont(wxFontInfo(this->font(role).GetPointSize()).Family(wxFONTFAMILY_TELETYPE));
    return font;
}

} // namespace Slic3r::GUI::JusPrin
