// Contract tests for the viewport tool strip's layout: a row in the canvas's
// upper-left corner, and where each button sits in it, independent of the
// selection and the camera.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Canvas/ViewportToolStripLayout.hpp"

#include <cmath>

using namespace Slic3r::GUI::JusPrin;

namespace {

// The token file's values: the 26 DIP icon button, 4 DIP padding and gaps,
// 12 DIP from the canvas edges. test_shell_theme.cpp checks that
// tool_strip_geometry() reads exactly these.
constexpr StripGeometry kGeometry{26, 4, 4, 12};

} // namespace

TEST_CASE("the strip sits in the canvas's upper-left corner", "[jusprin][tool_strip]")
{
    const StripLayout layout = layout_tool_strip(kGeometry, 1.f, 1000.f, 700.f);

    CHECK(layout.strip.x == 12.f);
    CHECK(layout.strip.y == 12.f);
    CHECK(layout.strip.w == 158.f); // 4 + 5 * 26 + 5 * 4 + 4
    CHECK(layout.strip.h == 34.f);  // 4 + 26 + 4
}

TEST_CASE("the buttons run left to right with the divider before More", "[jusprin][tool_strip]")
{
    const StripLayout layout = layout_tool_strip(kGeometry, 1.f, 1000.f, 700.f);

    for (std::size_t i = 0; i < kStripToolCount; ++i) {
        INFO("button " << i);
        CHECK(layout.buttons[i].y == layout.strip.y + 4.f);
        CHECK(layout.buttons[i].w == 26.f);
        CHECK(layout.buttons[i].h == 26.f);
    }
    CHECK(layout.buttons[0].x == layout.strip.x + 4.f);
    CHECK(layout.buttons[1].x == layout.buttons[0].x + 30.f);
    CHECK(layout.buttons[3].x == layout.buttons[2].x + 30.f);

    const StripRect& duplicate = layout.buttons[size_t(StripTool::Duplicate)];
    const StripRect& more      = layout.buttons[size_t(StripTool::More)];
    CHECK(layout.divider_x == duplicate.x + 26.f + 4.f);
    CHECK(more.x == layout.divider_x + 4.f);
    CHECK(more.x + more.w + 4.f == layout.strip.x + layout.strip.w);
    // The divider stops one gap short of each end of the buttons.
    CHECK(layout.divider_y0 == duplicate.y + 4.f);
    CHECK(layout.divider_y1 == duplicate.y + 26.f - 4.f);
}

TEST_CASE("the strip scales with the display and stays on whole pixels", "[jusprin][tool_strip]")
{
    const StripLayout layout = layout_tool_strip(kGeometry, 1.5f, 1001.f, 700.f);

    CHECK(layout.strip.x == 18.f);
    CHECK(layout.strip.y == 18.f);
    CHECK(layout.strip.w == 237.f); // 6 + 5 * 39 + 5 * 6 + 6
    CHECK(layout.strip.h == 51.f);  // 6 + 39 + 6
    for (const StripRect& button : layout.buttons) {
        CHECK(button.x == std::round(button.x));
        CHECK(button.w == 39.f);
    }
}

TEST_CASE("a canvas too narrow for the row keeps the strip on screen", "[jusprin][tool_strip]")
{
    const StripLayout layout = layout_tool_strip(kGeometry, 1.f, 100.f, 700.f);

    CHECK(layout.strip.x == 0.f);
    CHECK(layout.strip.y == 12.f);
}
