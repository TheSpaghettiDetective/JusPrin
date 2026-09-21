#pragma once

#include "../GLCanvas3D.hpp"

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

    // The strip this controller put on the canvas, or nullptr while detached.
    const ViewportToolStrip* tool_strip() const { return m_tool_strip.get(); }

private:
    GLCanvas3D*                        m_canvas{nullptr};
    std::optional<bool>                m_previous_hidden;
    std::optional<bool>                m_previous_outline;
    std::unique_ptr<ViewportToolStrip> m_tool_strip;
};

} // namespace Slic3r::GUI::JusPrin
