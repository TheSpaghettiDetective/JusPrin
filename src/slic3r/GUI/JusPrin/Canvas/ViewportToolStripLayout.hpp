#pragma once

#include <array>
#include <cstddef>

namespace Slic3r::GUI::JusPrin {

// The strip's tools, top to bottom. A divider separates More from the rest.
enum class StripTool { Move, Rotate, Scale, Duplicate, More };
constexpr std::size_t kStripToolCount = 5;

struct StripRect
{
    float x{0};
    float y{0};
    float w{0};
    float h{0};
};

// The strip's sizes in DIP, read from the token file by tool_strip_geometry().
struct StripGeometry
{
    int button{0};  // the square button's side
    int padding{0}; // strip edge to buttons
    int gap{0};     // between items, the divider included, and the divider's inset from the button ends
    int inset{0};   // strip to the canvas's top and left edges
};

// The strip in canvas pixels, whole pixels throughout so edges stay crisp.
struct StripLayout
{
    StripRect strip;
    std::array<StripRect, kStripToolCount> buttons;
    // The hairline before More, from (divider_x, divider_y0) to (divider_x, divider_y1).
    float divider_x{0};
    float divider_y0{0};
    float divider_y1{0};
};

// Places the strip in the canvas's upper-left corner, a row of buttons inset
// from the top and left edges, the way OrcaSlicer's own toolbar runs but
// justified left rather than centred. It depends only on the canvas size: it
// does not follow the selection, the camera, or zoom. `scale` converts DIP to
// canvas pixels.
StripLayout layout_tool_strip(const StripGeometry& dip, float scale, float canvas_width, float canvas_height);

} // namespace Slic3r::GUI::JusPrin
