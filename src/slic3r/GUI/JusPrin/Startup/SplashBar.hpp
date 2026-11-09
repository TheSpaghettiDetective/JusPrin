#pragma once

class wxColour;
class wxRect;
class wxWindow;

namespace Slic3r::GUI::JusPrin {

// Adds the splash's activity bar to window as Core Animation layers: a track,
// and a segment segment_width wide sliding from one end of it to the other in
// sweep_ms, easing in and out, and back, for as long as the window lives. The
// animation runs in the window server, so it keeps moving while the main
// thread is blocked loading presets. track is in the window's logical
// coordinates, from its top-left. macOS only.
void add_splash_bar(wxWindow& window, const wxRect& track, int segment_width, int sweep_ms,
                    const wxColour& track_colour, const wxColour& segment_colour);

} // namespace Slic3r::GUI::JusPrin
