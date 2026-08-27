#pragma once

#include "../GLCanvas3D.hpp"

namespace Slic3r::GUI::JusPrin {

class GLCanvas3DWrapper final
{
public:
    explicit GLCanvas3DWrapper(GLCanvas3D& canvas);
    ~GLCanvas3DWrapper();

    GLCanvas3DWrapper(const GLCanvas3DWrapper&) = delete;
    GLCanvas3DWrapper& operator=(const GLCanvas3DWrapper&) = delete;

    bool activate_move_gizmo();
    bool activate_rotate_gizmo();

private:
    bool activate_gizmo(GLGizmosManager::EType type);

    GLCanvas3D&                 m_canvas;
    GLCanvasPresentationOptions m_previous_options;
};

} // namespace Slic3r::GUI::JusPrin
