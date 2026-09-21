#pragma once

#include "ToolValuePanel.hpp"
#include "ViewportToolStripLayout.hpp"

#include <memory>

namespace Slic3r::GUI {
class GLCanvas3D;
}

namespace Slic3r::GUI::JusPrin {

class CanvasPresentationController;
struct ShellMetrics;

// The strip's sizes from the token file: the icon-button recipe for the
// buttons, the first spacing step for padding and gaps, the third for the
// inset from the canvas edge.
StripGeometry tool_strip_geometry(const ShellMetrics& metrics);

// The vertical object-tool strip fixed inside the right edge of the Prepare
// canvas: Move, Rotate and Scale toggle their tools, Duplicate adds one
// instance, More opens the selection's context menu.
//
// It draws with ImGui inside the canvas's own frame, through
// GLCanvas3D::set_overlay_renderer, the way OrcaSlicer draws its canvas
// controls, so it needs no child window over the GL surface. ImGui also gives
// it the pointer before the canvas does, so a click on the strip never
// reaches selection or rectangle-select.
//
// Nothing about the tools is remembered between frames: the highlighted tool
// is read from the gizmo manager every frame, so a keyboard shortcut, the
// Agent, or a selection change that opens or closes a tool shows at once.
class ViewportToolStrip final
{
public:
    ViewportToolStrip(GLCanvas3D& canvas, CanvasPresentationController& controller);
    ~ViewportToolStrip();

    ViewportToolStrip(const ViewportToolStrip&) = delete;
    ViewportToolStrip& operator=(const ViewportToolStrip&) = delete;

    // Called by the canvas every frame, inside its ImGui frame.
    void render();

    // The open tool's card, whose field rectangles the harness clicks.
    const ToolValuePanel& values() const { return m_values; }

private:
    // Runs a tool after the frame: an action may open a modal menu or change
    // the scene, neither of which belongs inside the canvas's render pass.
    void run_after_frame(StripTool tool, const StripRect& button);
    void run(StripTool tool, const StripRect& button);

    GLCanvas3D&                             m_canvas;
    CanvasPresentationController&           m_controller;
    // The open tool's numbers, in a card anchored to the tool's own button.
    ToolValuePanel                          m_values;
    // Expires with the strip, so an action queued by the last frame is
    // dropped instead of reaching a detached strip.
    std::shared_ptr<ViewportToolStrip*>     m_self;
};

} // namespace Slic3r::GUI::JusPrin
