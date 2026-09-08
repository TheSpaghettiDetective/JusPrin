#include "ShellRecipes.hpp"

#include "slic3r/GUI/Widgets/Button.hpp"

#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace Slic3r::GUI::JusPrin {

void style_label(wxStaticText& label, const ShellTheme& theme, TextRole role, const wxColour& foreground)
{
    label.SetFont(theme.font(role));
    label.SetForegroundColour(foreground);
}

void style_text_field(wxTextCtrl& field, const ShellTheme& theme, const ShellPalette& palette)
{
    field.SetFont(theme.font(TextRole::Body));
    field.SetBackgroundColour(palette.surface_canvas);
    field.SetForegroundColour(palette.text_primary);
}

void style_button(Button& button, const ShellTheme& theme, const ShellPalette& palette, const ButtonRecipe& recipe)
{
    // A recipe names either a fixed size or a minimum; Button has one
    // minimum-size slot, so the fixed size is applied as that minimum. A
    // dimension the recipe leaves out stays unconstrained.
    const int width  = recipe.min_width  > 0 ? recipe.min_width  : recipe.width;
    const int height = recipe.min_height > 0 ? recipe.min_height : recipe.height;
    button.SetMinSize(button.FromDIP(wxSize(width > 0 ? width : wxDefaultCoord, height > 0 ? height : wxDefaultCoord)));
    if (recipe.padding_x > 0 || recipe.padding_y > 0)
        button.SetPaddingSize(button.FromDIP(wxSize(recipe.padding_x, recipe.padding_y)));
    button.SetCornerRadius(button.FromDIP(recipe.radius));
    if (recipe.text_role)
        button.SetFont(theme.font(*recipe.text_role));
    button.SetBorderWidth(button.FromDIP(1));

    // The same state tables Button::SetStyle builds, ordered so the more
    // specific state is matched first, with focus drawn as a border rather
    // than as hover.
    StateColor background(
        std::pair(palette.action_disabled,          int(StateColor::Disabled)),
        std::pair(palette.action_secondary_pressed, int(StateColor::Pressed)),
        std::pair(palette.action_secondary_hover,   int(StateColor::Hovered)),
        std::pair(palette.action_secondary,         int(StateColor::Normal)));
    background.setTakeFocusedAsHovered(false);
    button.SetBackgroundColor(background);

    StateColor border(
        std::pair(palette.action_disabled,         int(StateColor::Disabled)),
        std::pair(palette.border_focus,            int(StateColor::Focused)),
        std::pair(palette.action_secondary_border, int(StateColor::Normal)));
    border.setTakeFocusedAsHovered(false);
    button.SetBorderColor(border);

    button.SetTextColor(StateColor(
        std::pair(palette.action_disabled_text,   int(StateColor::Disabled)),
        std::pair(palette.action_secondary_text,  int(StateColor::Normal))));
}

} // namespace Slic3r::GUI::JusPrin
