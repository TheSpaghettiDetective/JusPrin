#pragma once

#include <wx/imagwebp.h>

#include <vector>

class wxRect;
class wxWindow;

namespace Slic3r::GUI::JusPrin {

// Plays frames (read_name_animation) in box, looping, for as long as the
// window is shown, in a way that keeps playing while the main thread is
// blocked loading presets: on macOS as a Core Animation layer, which the
// window server runs; on Windows in a window of its own, over the box, that a
// thread of its own draws. box is in the window's coordinates, from its
// top-left; the frames are scaled to fill it. On Windows each frame is drawn
// over the window's background colour, so the animation covers whatever the
// box shows beneath it. macOS and Windows only.
void add_splash_animation(wxWindow& window, const wxRect& box, std::vector<wxWebPAnimationFrame> frames);

} // namespace Slic3r::GUI::JusPrin
