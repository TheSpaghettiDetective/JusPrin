#pragma once

namespace Slic3r::GUI::JusPrin {

// Installs the JusPrin palette into the color tables OrcaSlicer's widgets and
// icons already read from, for the current appearance mode, and points the
// resource lookup at the brand overlay. GUI_App::init_label_colours calls
// this, so it runs once at startup and again when the appearance mode changes.
//
// The mapping from OrcaSlicer's color literals to JusPrin tokens lives here
// and nowhere else; upstream files only carry the neutral override hooks.
void apply_brand_palette();

} // namespace Slic3r::GUI::JusPrin
