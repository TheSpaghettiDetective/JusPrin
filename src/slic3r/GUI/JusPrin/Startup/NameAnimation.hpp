#pragma once

#include <wx/imagwebp.h>

#include <vector>

namespace Slic3r::GUI::JusPrin {

// The nozzle printing the name, in a loop (resources/jusprin/startup, drawn by
// Brand/splash_animation.py), in the current mode's ink: every frame
// composited to the file's full size, with how long it shows. The splash and
// the About window play it over their lockup's name. Throws if the file cannot
// be read.
std::vector<wxWebPAnimationFrame> read_name_animation();

} // namespace Slic3r::GUI::JusPrin
