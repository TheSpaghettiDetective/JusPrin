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
    void detach();
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
