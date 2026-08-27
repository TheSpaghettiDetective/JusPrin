#include "GLCanvas3DWrapper.hpp"

namespace Slic3r::GUI::JusPrin {

namespace {

GLCanvasPresentationOptions jusprin_presentation_options()
{
    GLCanvasPresentationOptions options;
    options.render_overlays       = false;
    options.handle_overlay_input  = false;
    options.render_plate_controls = false;
    options.handle_plate_input    = false;
    return options;
}

} // namespace

GLCanvas3DWrapper::GLCanvas3DWrapper(GLCanvas3D& canvas)
    : m_canvas(canvas)
    , m_previous_options(canvas.presentation_options())
{
    m_canvas.set_presentation_options(jusprin_presentation_options());
}

GLCanvas3DWrapper::~GLCanvas3DWrapper()
{
    m_canvas.set_presentation_options(m_previous_options);
}

bool GLCanvas3DWrapper::activate_move_gizmo()
{
    return activate_gizmo(GLGizmosManager::EType::Move);
}

bool GLCanvas3DWrapper::activate_rotate_gizmo()
{
    return activate_gizmo(GLGizmosManager::EType::Rotate);
}

bool GLCanvas3DWrapper::activate_gizmo(GLGizmosManager::EType type)
{
    GLGizmosManager& gizmos = m_canvas.get_gizmos_manager();
    const bool opened = gizmos.get_current_type() == type || gizmos.open_gizmo(type);
    m_canvas.set_as_dirty();
    m_canvas.request_extra_frame();
    return opened;
}

} // namespace Slic3r::GUI::JusPrin
