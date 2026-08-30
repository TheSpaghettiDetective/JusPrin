#pragma once

#include <wx/colour.h>

#include <string>

namespace Slic3r::GUI::JusPrin {

// Semantic colors for one appearance mode, resolved from
// resources/jusprin/ui/design-tokens.json. That file is the authoritative
// source; this struct only carries the values the shell consumes.
struct ShellPalette
{
    wxColour surface_canvas;
    wxColour surface_subtle;
    wxColour text_primary;
    wxColour text_secondary;
    wxColour border_subtle;
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
};

class ShellTheme
{
public:
    // Loads both semantic palettes from the packaged token file.
    // Throws std::runtime_error when the file is missing or malformed so the
    // caller can fall back to the standard Orca presentation.
    static ShellTheme load_from_resources();

    const ShellPalette& palette(bool dark) const { return dark ? m_dark : m_light; }

private:
    ShellPalette m_light;
    ShellPalette m_dark;
};

} // namespace Slic3r::GUI::JusPrin
