#pragma once

#include "../GLCanvas3D.hpp"

#include <optional>

namespace Slic3r::GUI::JusPrin {

// Owns the "hide the legacy canvas overlays" decision for one GLCanvas3D while
// the shell is installed. Attaching flips the canvas into the fork's clean
// presentation (all legacy toolbars/gizmo picker/plate controls/navigator/
// canvas menu hidden; the active gizmo stays interactive); detaching restores
// whatever the canvas had before.
class CanvasPresentationController final
{
public:
    CanvasPresentationController() = default;
    explicit CanvasPresentationController(GLCanvas3D& canvas) { attach(canvas); }
    ~CanvasPresentationController() { detach(); }

    CanvasPresentationController(const CanvasPresentationController&) = delete;
    CanvasPresentationController& operator=(const CanvasPresentationController&) = delete;

    void attach(GLCanvas3D& canvas);
    void detach();
    // Forget the canvas without restoring it: for teardown paths where the
    // canvas is already being destroyed and must not be touched again.
    void abandon()
    {
        m_previous_hidden.reset();
        m_canvas = nullptr;
    }
    bool is_attached() const { return m_canvas != nullptr; }

    bool activate_move();
    bool activate_rotate();

private:
    bool activate(GLGizmosManager::EType type);

    GLCanvas3D*         m_canvas{nullptr};
    std::optional<bool> m_previous_hidden;
};

} // namespace Slic3r::GUI::JusPrin
