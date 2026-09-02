#include "CanvasPresentationController.hpp"

namespace Slic3r::GUI::JusPrin {

void CanvasPresentationController::attach(GLCanvas3D& canvas)
{
    if (m_canvas == &canvas)
        return;
    detach();
    m_canvas = &canvas;
    m_previous_hidden = canvas.legacy_overlays_hidden();
    canvas.set_legacy_overlays_hidden(true);
}

void CanvasPresentationController::detach()
{
    if (m_canvas != nullptr && m_previous_hidden)
        m_canvas->set_legacy_overlays_hidden(*m_previous_hidden);
    m_previous_hidden.reset();
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
