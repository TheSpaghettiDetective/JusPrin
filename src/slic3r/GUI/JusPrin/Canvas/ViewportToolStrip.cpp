#include "ViewportToolStrip.hpp"

#include "CanvasIcons.hpp"

#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Slic3r::GUI::JusPrin {

namespace {

// Each tool's icon under resources/jusprin/ui/icons, in StripTool order.
constexpr std::array<const char*, kStripToolCount> kIconNames{"move", "rotate-cw", "maximize-2", "copy", "ellipsis"};

GLGizmosManager::EType gizmo_for(StripTool tool)
{
    switch (tool) {
    case StripTool::Move:   return GLGizmosManager::Move;
    case StripTool::Rotate: return GLGizmosManager::Rotate;
    case StripTool::Scale:  return GLGizmosManager::Scale;
    default:                return GLGizmosManager::Undefined;
    }
}

ImU32 im_color(const wxColour& color) { return IM_COL32(color.Red(), color.Green(), color.Blue(), color.Alpha()); }

// elevation.medium, approximated: ImGui 1.83 has no shadow primitive, so
// translucent rounded rectangles are stacked with their edges stepping across
// the blur width.
void draw_shadow(ImDrawList& draw, const StripRect& rect, float radius, const ElevationMetrics& elevation,
                 const wxColour& color, float scale)
{
    constexpr int steps = 6;
    const float blur = elevation.blur * scale;
    const ImVec2 offset(elevation.offset_x * scale, elevation.offset_y * scale);
    const ImU32 layer = IM_COL32(color.Red(), color.Green(), color.Blue(), color.Alpha() / steps);
    for (int i = 0; i < steps; ++i) {
        const float grow = elevation.spread * scale - blur / 2 + blur * (i + 0.5f) / steps;
        draw.AddRectFilled(ImVec2(rect.x + offset.x - grow, rect.y + offset.y - grow),
                           ImVec2(rect.x + rect.w + offset.x + grow, rect.y + rect.h + offset.y + grow),
                           layer, std::max(0.f, radius + grow));
    }
}

} // namespace

StripGeometry tool_strip_geometry(const ShellMetrics& metrics)
{
    StripGeometry geometry;
    geometry.button  = metrics.button.icon.width;
    geometry.padding = metrics.space_1;
    geometry.gap     = metrics.space_1;
    geometry.inset   = metrics.space_3;
    return geometry;
}

ViewportToolStrip::ViewportToolStrip(GLCanvas3D& canvas, CanvasPresentationController& controller)
    : m_canvas(canvas), m_controller(controller), m_values(canvas), m_self(std::make_shared<ViewportToolStrip*>(this))
{}

ViewportToolStrip::~ViewportToolStrip() = default;

void ViewportToolStrip::render()
{
    // Without the token file the app keeps OrcaSlicer's stock presentation,
    // which has no strip; the failure was logged when the tokens were read.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr || m_canvas.get_selection().is_empty())
        return;

    const ShellMetrics& m = theme->metrics();
    const ShellPalette& p = theme->palette(GUI_App::dark_mode());
    const float scale = wxGetApp().imgui()->get_style_scaling();
    const Size canvas_size = m_canvas.get_canvas_size();
    const StripLayout layout = layout_tool_strip(tool_strip_geometry(m), scale, float(canvas_size.get_width()),
                                                 float(canvas_size.get_height()));

    const int icon_px = int(std::lround(m.button.icon.icon_size * scale));

    ImGui::SetNextWindowPos(ImVec2(layout.strip.x, layout.strip.y));
    ImGui::SetNextWindowSize(ImVec2(layout.strip.w, layout.strip.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::Begin("##jusprin_viewport_tool_strip", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    // Only the strip's own window is bare; its tooltips keep the stock style.
    ImGui::PopStyleVar(3);

    ImDrawList& draw = *ImGui::GetWindowDrawList();
    // The shadow falls outside the window's own rectangle.
    draw.PushClipRectFullScreen();

    const float strip_radius = std::round(m.radius_container * scale);
    const ImVec2 strip_min(layout.strip.x, layout.strip.y);
    const ImVec2 strip_max(layout.strip.x + layout.strip.w, layout.strip.y + layout.strip.h);
    draw_shadow(draw, layout.strip, strip_radius, m.elevation_medium, p.elevation_medium, scale);
    draw.AddRectFilled(strip_min, strip_max, im_color(p.surface_raised), strip_radius);
    // Hairlines are one device pixel, as the shell's own borders are.
    draw.AddRect(ImVec2(strip_min.x + 0.5f, strip_min.y + 0.5f), ImVec2(strip_max.x - 0.5f, strip_max.y - 0.5f),
                 im_color(p.border_subtle), strip_radius, 0, 1.f);
    draw.AddLine(ImVec2(layout.divider_x + 0.5f, layout.divider_y0), ImVec2(layout.divider_x + 0.5f, layout.divider_y1),
                 im_color(p.border_subtle), 1.f);

    const GLGizmosManager& gizmos = m_canvas.get_gizmos_manager();
    const GLGizmosManager::EType open_tool = gizmos.get_current_type();
    const float button_radius = std::round(m.button.icon.radius * scale);
    for (std::size_t i = 0; i < kStripToolCount; ++i) {
        const StripTool tool = StripTool(i);
        const StripRect& button = layout.buttons[i];
        const GLGizmosManager::EType gizmo_type = gizmo_for(tool);
        const GLGizmoBase* gizmo = gizmo_type == GLGizmosManager::Undefined ? nullptr : gizmos.get_gizmo(gizmo_type);
        const bool active  = gizmo != nullptr && gizmo_type == open_tool;
        const bool enabled = gizmo != nullptr ? gizmo->is_activable() :
                             tool == StripTool::Duplicate ? wxGetApp().plater()->can_increase_instances() : true;

        ImGui::SetCursorScreenPos(ImVec2(button.x, button.y));
        ImGui::PushID(int(i));
        const bool clicked = ImGui::InvisibleButton("##tool", ImVec2(button.w, button.h));
        ImGui::PopID();
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemActive();

        // Idle buttons read like the header's quiet buttons; the open tool
        // takes the primary action fill in its hover and pressed shades.
        const wxColour* fill = nullptr;
        if (enabled && active)
            fill = pressed ? &p.action_primary_pressed : hovered ? &p.action_primary_hover : &p.action_primary;
        else if (enabled && (hovered || pressed))
            fill = &p.surface_selected;
        const wxColour& icon_color = !enabled ? p.action_disabled_text : active ? p.text_on_action : p.text_secondary;

        const ImVec2 button_min(button.x, button.y);
        const ImVec2 button_max(button.x + button.w, button.y + button.h);
        if (fill != nullptr)
            draw.AddRectFilled(button_min, button_max, im_color(*fill), button_radius);
        const ImVec2 icon_min(button.x + std::round((button.w - icon_px) / 2), button.y + std::round((button.h - icon_px) / 2));
        draw.AddImage((ImTextureID)(intptr_t)canvas_icon_texture(kIconNames[i], icon_px), icon_min, ImVec2(icon_min.x + icon_px, icon_min.y + icon_px),
                      ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), im_color(icon_color));

        if (hovered) {
            const std::string tooltip = gizmo != nullptr ? gizmo->get_name(true) :
                                        tool == StripTool::Duplicate ? _u8L("Add instance") + " [+]" : _u8L("More");
            // Past the end of the row, level with it: clear of the pointer,
            // which is always on a button inside the row, and clear of the
            // value card, which hangs below it. At the pointer it would sit
            // under the cursor itself; below the row it would cover the card.
            ImGui::SetNextWindowPos(ImVec2(layout.strip.x + layout.strip.w + std::round(m.space_3 * scale),
                                           layout.strip.y + layout.strip.h / 2),
                                    ImGuiCond_Always, ImVec2(0.f, 0.5f));
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::EndTooltip();
        }
        if (clicked && enabled)
            run_after_frame(tool, button);
    }

    draw.PopClipRect();
    ImGui::End();

    // The open tool's card, drawn after the strip so it sits above it.
    m_values.render(*theme, p, layout, scale);
}

void ViewportToolStrip::run_after_frame(StripTool tool, const StripRect& button)
{
    wxGetApp().CallAfter([self = std::weak_ptr<ViewportToolStrip*>(m_self), tool, button]() {
        if (const auto strip = self.lock())
            (*strip)->run(tool, button);
    });
}

void ViewportToolStrip::run(StripTool tool, const StripRect& button)
{
    switch (tool) {
    case StripTool::Move:
    case StripTool::Rotate:
    case StripTool::Scale:
        m_controller.toggle_tool(gizmo_for(tool));
        break;
    case StripTool::Duplicate:
        wxGetApp().plater()->increase_instances(1);
        break;
    case StripTool::More: {
        // The canvas's own right-click path, so the menu matches the
        // selection exactly as it does on a right-click over the object. It
        // opens below the button, from its left edge. Plater expects logical
        // coordinates; ImGui's are device pixels on Retina.
        const double retina = m_canvas.get_scale();
        const Vec2d anchor(button.x / retina, (button.y + button.h) / retina);
        m_canvas.post_event(RBtnEvent(EVT_GLCANVAS_RIGHT_CLICK, {anchor, false}));
        break;
    }
    }
}

} // namespace Slic3r::GUI::JusPrin
