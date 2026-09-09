#pragma once

#include <wx/colour.h>
#include <wx/font.h>

#include <array>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin {

// Semantic colors for one appearance mode, resolved from
// resources/jusprin/ui/design-tokens.json. That file is the authoritative
// source; this struct only carries the values the shell consumes.
struct ShellPalette
{
    wxColour surface_canvas;
    wxColour surface_subtle;
    wxColour surface_raised;
    wxColour surface_selected;
    wxColour text_primary;
    wxColour text_secondary;
    wxColour text_on_action;
    wxColour border_subtle;
    wxColour border_strong;
    wxColour border_focus;
    wxColour action_primary;
    wxColour action_primary_hover;
    wxColour action_primary_pressed;
    wxColour action_primary_text;
    wxColour action_secondary;
    wxColour action_secondary_hover;
    wxColour action_secondary_pressed;
    wxColour action_secondary_text;
    wxColour action_secondary_border;
    wxColour action_disabled;
    wxColour action_disabled_text;
    wxColour status_warning;
    wxColour status_success;
    wxColour status_success_on_action;
    wxColour status_danger;
};

// The text roles the token file defines under typography.roles.
enum class TextRole { PageTitle, Section, Body, BodyBold, Label, LabelBold, Metadata };
constexpr size_t kTextRoleCount = 7;

// The numbers behind one text role. Sizes are DIP; weight is 400 or 700.
struct TypeStyle
{
    int size{0};
    int line_height{0};
    int weight{0};
};

// One entry of component.button. Each recipe names only some of these
// fields; the ones it leaves out stay 0.
struct ButtonRecipe
{
    int min_width{0};
    int width{0};
    int height{0};
    int min_height{0};
    int padding_x{0};
    int padding_y{0};
    int icon_size{0};
    int radius{0};
    std::optional<TextRole> text_role;
};

struct ButtonMetrics
{
    ButtonRecipe compact;
    ButtonRecipe window;
    ButtonRecipe choice;
    ButtonRecipe parameter;
    ButtonRecipe icon;
    ButtonRecipe expanded;
};

struct ChipMetrics      { int height{0}; int radius{0}; };
struct MenuRowMetrics   { int height{0}; int radius{0}; int side_inset{0}; };
struct PopoverMetrics   { int padding_y{0}; int row_gap{0}; int radius{0}; };
struct StatusRowMetrics { int height{0}; };
struct AgentPaneMetrics {
    int min_width{0};
    int workspace_min_width{0};
    int resize_handle_width{0};
    int resize_handle_line_width{0};
};
struct SwatchMetrics    { int size{0}; int radius{0}; };

// Geometry from the token file's dimension and component sections. Every
// value is DIP; callers wrap it in FromDIP().
struct ShellMetrics
{
    int radius_standard{0};
    int radius_compact{0};
    int radius_container{0};
    int radius_window{0};
    int radius_pill{0};

    // The spacing scale, named by its step: space_2 is dimension.space["2"].
    int space_1{0};
    int space_2{0};
    int space_3{0};
    int space_4{0};
    int space_5{0};
    int space_6{0};
    int space_8{0};
    int space_10{0};
    int space_12{0};

    ButtonMetrics    button;
    ChipMetrics      chip;
    MenuRowMetrics   menu_row;
    PopoverMetrics   popover;
    StatusRowMetrics status_row;
    AgentPaneMetrics agent_pane;
    SwatchMetrics    swatch;
};

class ShellTheme
{
public:
    // Loads both semantic palettes, the metrics and the text roles from the
    // packaged token file. Throws std::runtime_error when the file is missing
    // or malformed so the caller can fall back to the standard Orca
    // presentation. Needs no wx initialisation.
    static ShellTheme load_from_resources();

    const ShellPalette& palette(bool dark) const { return dark ? m_dark : m_light; }
    const ShellMetrics& metrics() const { return m_metrics; }
    const TypeStyle&    type_style(TextRole role) const { return m_type_styles[size_t(role)]; }

    // The wxFont for a role, built once on first use through OrcaSlicer's
    // Label::sysFont so it carries the same face as the rest of the
    // application, then sized so the role's DIP size is its em on every
    // platform (sysFont's own point scaling rounds the token off on Windows
    // and Linux). Requires wx to be initialised.
    const wxFont& font(TextRole role) const;

    // The same role in the system teletype face at regular weight, for
    // measurements, temperatures, filenames and machine status. Requires wx
    // to be initialised.
    const wxFont& mono_font(TextRole role) const;

private:
    ShellPalette m_light;
    ShellPalette m_dark;
    ShellMetrics m_metrics;
    std::array<TypeStyle, kTextRoleCount> m_type_styles{};
    mutable std::array<wxFont, kTextRoleCount> m_fonts{};
    mutable std::array<wxFont, kTextRoleCount> m_mono_fonts{};
};

} // namespace Slic3r::GUI::JusPrin
