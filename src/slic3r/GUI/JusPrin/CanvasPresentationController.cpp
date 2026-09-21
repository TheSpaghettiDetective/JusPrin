#include "CanvasPresentationController.hpp"

#include "Canvas/ViewportToolStrip.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r::GUI::JusPrin {

CanvasPresentationController::CanvasPresentationController() = default;

CanvasPresentationController::CanvasPresentationController(GLCanvas3D& canvas) { attach(canvas); }

CanvasPresentationController::~CanvasPresentationController() { detach(); }

void CanvasPresentationController::attach(GLCanvas3D& canvas)
{
    if (m_canvas == &canvas)
        return;
    detach();
    m_canvas = &canvas;
    m_previous_hidden = canvas.legacy_overlays_hidden();
    canvas.set_legacy_overlays_hidden(true);
    // OrcaSlicer's silhouette outline around the selected volumes, which it
    // ships switched off. It is the fork's selection highlight, so the shell
    // owns the setting for as long as it is installed and hands it back on
    // detach.
    m_previous_outline = wxGetApp().show_outline();
    if (!*m_previous_outline)
        wxGetApp().toggle_show_outline();
    m_tool_strip = std::make_unique<ViewportToolStrip>(canvas, *this);
    canvas.set_overlay_renderer([strip = m_tool_strip.get()]() { strip->render(); });
}

void CanvasPresentationController::detach()
{
    if (m_canvas != nullptr) {
        m_canvas->set_overlay_renderer(nullptr);
        if (m_previous_hidden)
            m_canvas->set_legacy_overlays_hidden(*m_previous_hidden);
        if (m_previous_outline && *m_previous_outline != wxGetApp().show_outline())
            wxGetApp().toggle_show_outline();
    }
    m_tool_strip.reset();
    m_previous_hidden.reset();
    m_previous_outline.reset();
    m_canvas = nullptr;
}

void CanvasPresentationController::abandon()
{
    m_tool_strip.reset();
    m_previous_hidden.reset();
    m_previous_outline.reset();
    m_canvas = nullptr;
}

bool CanvasPresentationController::toggle_tool(GLGizmosManager::EType type)
{
    if (m_canvas == nullptr)
        return false;

    // open_gizmo closes the tool when it is already the open one.
    if (!m_canvas->get_gizmos_manager().open_gizmo(type))
        return false;

    m_canvas->set_as_dirty();
    m_canvas->request_extra_frame();
    return true;
}

} // namespace Slic3r::GUI::JusPrin
