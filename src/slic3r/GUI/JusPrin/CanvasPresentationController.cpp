#include "CanvasPresentationController.hpp"

namespace Slic3r::GUI::JusPrin {

GLCanvasPresentationOptions CanvasPresentationController::presentation_options()
{
    GLCanvasPresentationOptions options;
    options.main_toolbar_visible          = false;
    options.main_toolbar_input_enabled    = false;
    options.assemble_toolbar_visible      = false;
    options.assemble_toolbar_input_enabled = false;
    options.gizmo_picker_visible          = false;
    options.gizmo_picker_input_enabled    = false;
    options.active_gizmo_visible          = true;
    options.active_gizmo_input_enabled    = true;
    options.plate_controls_visible        = false;
    options.plate_controls_input_enabled  = false;
    options.canvas_toolbar_visible        = false;
    options.canvas_toolbar_input_enabled  = false;
    options.object_labels_visible         = false;
    options.navigator_visible             = false;
    return options;
}

void CanvasPresentationController::attach(GLCanvas3D& canvas)
{
    if (m_canvas == &canvas)
        return;
    detach();
    m_canvas = &canvas;
    m_previous_options = canvas.presentation_options();
    canvas.set_presentation_options(presentation_options());
}

void CanvasPresentationController::detach()
{
    if (m_canvas != nullptr && m_previous_options)
        m_canvas->set_presentation_options(*m_previous_options);
    m_previous_options.reset();
    m_canvas = nullptr;
}

bool CanvasPresentationController::activate_move()
{
    return activate(GLGizmosManager::EType::Move);
}

bool CanvasPresentationController::activate_rotate()
{
    return activate(GLGizmosManager::EType::Rotate);
}

bool CanvasPresentationController::activate(GLGizmosManager::EType type)
{
    if (m_canvas == nullptr)
        return false;

    GLGizmosManager& gizmos = m_canvas->get_gizmos_manager();
    if (gizmos.get_current_type() == type)
        return true;
    if (!gizmos.open_gizmo(type))
        return false;

    m_canvas->set_as_dirty();
    m_canvas->request_extra_frame();
    return true;
}

} // namespace Slic3r::GUI::JusPrin
