#pragma once

// The three ways the shell dresses a stock control from the theme: a label, a
// text field, and an OrcaSlicer Button. Widgets the shell paints itself
// (HeaderButton, the swatch cells) read ShellTheme directly; these recipes
// exist so a wxStaticText or a Button never carries a hand-typed font, radius
// or colour of its own.

#include "ShellTheme.hpp"

class Button;
class wxColour;
class wxStaticText;
class wxTextCtrl;

namespace Slic3r::GUI::JusPrin {

// Font from the role, foreground as given: the caller names the colour because
// the same role reads in text_primary, text_secondary or a status colour.
void style_label(wxStaticText& label, const ShellTheme& theme, TextRole role, const wxColour& foreground);

// Body font on the canvas surface in the primary text colour.
void style_text_field(wxTextCtrl& field, const ShellTheme& theme, const ShellPalette& palette);

// Size, padding, radius and font from one of the component.button recipes;
// colours from the palette's secondary action. This is the fork's counterpart
// to Button::SetStyle(ButtonStyle::Regular, type), built from tokens instead
// of the teal tables.
void style_button(Button& button, const ShellTheme& theme, const ShellPalette& palette, const ButtonRecipe& recipe);

} // namespace Slic3r::GUI::JusPrin
