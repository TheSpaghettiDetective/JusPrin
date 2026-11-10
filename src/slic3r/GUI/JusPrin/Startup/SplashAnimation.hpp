#pragma once

#include <wx/imagwebp.h>

#include <vector>

class wxRect;
class wxWindow;

namespace Slic3r::GUI::JusPrin {

// Plays frames (read_name_animation) in box, looping, as a Core Animation
// layer for as long as the window lives. The animation runs in the window
// server, so it keeps playing while the main thread is blocked loading
// presets. box is in the window's logical coordinates, from its top-left; the
// frames are scaled to fill it. macOS only.
void add_splash_animation(wxWindow& window, const wxRect& box, std::vector<wxWebPAnimationFrame> frames);

} // namespace Slic3r::GUI::JusPrin
