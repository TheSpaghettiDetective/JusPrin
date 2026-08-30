#pragma once

#include "../GLCanvas3D.hpp"

#include <optional>

namespace Slic3r::GUI::JusPrin {

class CanvasPresentationController final
{
public:
    CanvasPresentationController() = default;
    explicit CanvasPresentationController(GLCanvas3D& canvas) { attach(canvas); }
    ~CanvasPresentationController() { detach(); }

    CanvasPresentationController(const CanvasPresentationController&) = delete;
    CanvasPresentationController& operator=(const CanvasPresentationController&) = delete;

    void attach(GLCanvas3D& canvas);
    void attach(GLCanvas3D& canvas, const GLCanvasPresentationOptions& options);
    void detach();
    // Forget the canvas without restoring it: for teardown paths where the
    // canvas is already being destroyed and must not be touched again.
    void abandon()
    {
        m_previous_options.reset();
        m_canvas = nullptr;
    }
    bool is_attached() const { return m_canvas != nullptr; }

    bool activate_move();
    bool activate_rotate();

    static GLCanvasPresentationOptions presentation_options();

private:
    bool activate(GLGizmosManager::EType type);

    GLCanvas3D*                              m_canvas{nullptr};
    std::optional<GLCanvasPresentationOptions> m_previous_options;
};

} // namespace Slic3r::GUI::JusPrin
