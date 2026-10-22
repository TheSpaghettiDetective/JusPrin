#pragma once

#include "../GLCanvas3D.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace Slic3r::GUI::JusPrin {

class ViewportToolStrip;

// Owns the "hide the legacy canvas overlays" decision for one GLCanvas3D while
// the shell is installed. Attaching flips the canvas into the fork's clean
// presentation (all legacy toolbars/gizmo picker/plate controls/navigator/
// canvas menu hidden; the active gizmo stays interactive) and puts the fork's
// object-tool strip in their place; detaching restores whatever the canvas
// had before.
class CanvasPresentationController final
{
public:
    CanvasPresentationController();
    explicit CanvasPresentationController(GLCanvas3D& canvas);
    ~CanvasPresentationController();

    CanvasPresentationController(const CanvasPresentationController&) = delete;
    CanvasPresentationController& operator=(const CanvasPresentationController&) = delete;

    void attach(GLCanvas3D& canvas);
    void detach();
    // Forget the canvas without restoring it: for teardown paths where the
    // canvas is already being destroyed and must not be touched again.
    void abandon();
    bool is_attached() const { return m_canvas != nullptr; }

    // Opens the tool, or closes it when it is the open one. Returns whether
    // the gizmo manager made the change.
    bool toggle_tool(GLGizmosManager::EType type);

    // A second fork-owned layer drawn after the tool strip in the same pass:
    // the print-issue overlay, which the shell owns.
    void set_extra_overlay(std::function<void()> renderer) { m_extra_overlay = std::move(renderer); }

    // The strip this controller put on the canvas, or nullptr while detached.
    const ViewportToolStrip* tool_strip() const { return m_tool_strip.get(); }

private:
    GLCanvas3D*                        m_canvas{nullptr};
    std::optional<bool>                m_previous_hidden;
    std::optional<bool>                m_previous_outline;
    std::unique_ptr<ViewportToolStrip> m_tool_strip;
    std::function<void()>              m_extra_overlay;
};

// How the Check print window presents OrcaSlicer's G-code preview canvas.
// Constructing the controller records what it changes and destroying it puts
// all of it back, whichever way the window closes:
// - OrcaSlicer's legacy overlay layer (the plate selector strip, the 3D
//   navigator cube, the canvas toolbar) is hidden, as Prepare's controller
//   hides it;
// - the G-code listing, which is the app-wide `show_gcode_window`
//   preference, is switched off while the window is open; the person may
//   switch it on again inside the window, and the preference they had comes
//   back on close;
// - the legend, with its view-type dropdown, is its own switch.
// The sliders are not items here: the window shows them as OrcaSlicer does.
class PreviewPresentationController final
{
public:
    explicit PreviewPresentationController(GLCanvas3D& canvas);
    ~PreviewPresentationController();

    PreviewPresentationController(const PreviewPresentationController&) = delete;
    PreviewPresentationController& operator=(const PreviewPresentationController&) = delete;

    void set_legend_shown(bool shown);
    bool legend_shown() const;

    // Forget the canvas without restoring it, for a canvas already going away.
    // The app-wide G-code listing preference is still restored.
    void abandon() { m_canvas = nullptr; }

private:
    GLCanvas3D* m_canvas{nullptr};
    bool        m_previous_legend{true};
    bool        m_previous_overlays_hidden{false};
    bool        m_previous_gcode_window{true};
};

} // namespace Slic3r::GUI::JusPrin
