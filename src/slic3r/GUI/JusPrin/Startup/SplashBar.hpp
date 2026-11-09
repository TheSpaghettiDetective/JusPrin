#pragma once

#include <string>

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

// Plays the animated WebP at webp_path in box, looping, as a Core Animation
// layer, so like the bar it keeps playing while the main thread is blocked.
// box is in the window's logical coordinates; the file's frames are scaled to
// fill it. Throws if the file cannot be read. macOS only.
void add_splash_animation(wxWindow& window, const wxRect& box, const std::string& webp_path);

} // namespace Slic3r::GUI::JusPrin
