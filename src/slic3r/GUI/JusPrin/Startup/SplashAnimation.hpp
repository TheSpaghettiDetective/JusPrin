#pragma once

#include <string>

class wxRect;
class wxWindow;

namespace Slic3r::GUI::JusPrin {

// Plays the animated WebP at webp_path in box, looping, as a Core Animation
// layer for as long as the window lives. The animation runs in the window
// server, so it keeps playing while the main thread is blocked loading
// presets. box is in the window's logical coordinates, from its top-left; the
// file's frames are scaled to fill it. Throws if the file cannot be read.
// macOS only.
void add_splash_animation(wxWindow& window, const wxRect& box, const std::string& webp_path);

} // namespace Slic3r::GUI::JusPrin
