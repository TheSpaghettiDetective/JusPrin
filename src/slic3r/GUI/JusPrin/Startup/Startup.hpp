#pragma once

namespace Slic3r::GUI {
class GUI_App;
class MainFrame;
} // namespace Slic3r::GUI

namespace Slic3r::GUI::JusPrin {

// JusPrin's part of OrcaSlicer's startup, GUI_App::on_init_inner. Each stage
// is one call from upstream, so a later stage costs one more upstream line.

// Where OrcaSlicer would show its splash: shows JusPrin's instead, unless this
// is not the editor, the person chose classic view, or the design tokens did
// not load, or OrcaSlicer's show_splash_screen preference is off.
// Returns whether it did, and then OrcaSlicer shows none.
bool startup_begin(GUI_App& app);

// Closes JusPrin's splash the first time frame is shown, where OrcaSlicer
// closes its own. attach_shell calls this from the frame's constructor, so the
// close costs no upstream line. Does nothing when no splash is open.
void close_splash_when_shown(MainFrame& frame);

} // namespace Slic3r::GUI::JusPrin
