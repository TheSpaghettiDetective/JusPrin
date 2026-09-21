#include "ViewportToolStripLayout.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::GUI::JusPrin {

StripLayout layout_tool_strip(const StripGeometry& dip, float scale, float canvas_width, float canvas_height)
{
    const auto px = [scale](int value) { return std::round(value * scale); };
    const float button  = px(dip.button);
    const float padding = px(dip.padding);
    const float gap     = px(dip.gap);
    const float inset   = px(dip.inset);

    // Four buttons, the divider, then More; the divider is a line with no
    // width of its own, so a gap sits on each side of it.
    const float count = float(kStripToolCount);
    StripLayout layout;
    layout.strip.w = count * button + count * gap + 2 * padding;
    layout.strip.h = button + 2 * padding;
    layout.strip.x = inset;
    layout.strip.y = inset;
    // A canvas too narrow for the whole row keeps the left inset and runs on;
    // the buttons that fall outside are simply not reachable.
    if (canvas_width > 0.f)
        layout.strip.x = std::min(layout.strip.x, std::max(0.f, std::round(canvas_width) - layout.strip.w));
    if (canvas_height > 0.f)
        layout.strip.y = std::min(layout.strip.y, std::max(0.f, std::round(canvas_height) - layout.strip.h));

    float x = layout.strip.x + padding;
    for (std::size_t i = 0; i < kStripToolCount; ++i) {
        if (StripTool(i) == StripTool::More) {
            layout.divider_x = x;
            x += gap;
        }
        layout.buttons[i] = {x, layout.strip.y + padding, button, button};
        x += button + gap;
    }
    layout.divider_y0 = layout.strip.y + padding + gap;
    layout.divider_y1 = layout.strip.y + padding + button - gap;
    return layout;
}

} // namespace Slic3r::GUI::JusPrin
