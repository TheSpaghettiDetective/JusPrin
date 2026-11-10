#pragma once

#include <wx/imagwebp.h>

#include <vector>

namespace Slic3r::GUI::JusPrin {

// The two versions of the nozzle printing the name (resources/jusprin/startup,
// drawn by Brand/splash_animation.py), each played over its lockup's name.
enum class NameAnimation {
    // In a loop that starts as the name finishes: the splash is up for
    // anything from under a second to twenty.
    Splash,
    // Once, from an empty name to the finished logo, which it ends on.
    About,
};

// That version in the current mode's ink: every frame composited to the
// file's full size, with how long it shows. Throws if the file cannot be read.
std::vector<wxWebPAnimationFrame> read_name_animation(NameAnimation version);

} // namespace Slic3r::GUI::JusPrin
