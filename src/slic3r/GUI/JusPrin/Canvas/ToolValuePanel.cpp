#include "ToolValuePanel.hpp"

#include "CanvasIcons.hpp"

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Gizmos/GizmoObjectManipulation.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoUtils.hpp"

#include <imgui/imgui.h>
// GetActiveID, as OrcaSlicer's own panels use to decide when a field commits.
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

// The three windows below are ported from OrcaSlicer's own gizmo panels --
// GizmoObjectManipulation::do_render_{move,rotate,scale_input}_window, as of
// upstream f9e9cff53a -- so the rows, the widgets, the clamping, the commit
// rule and the coordinate-space handling stay exactly upstream's. What the
// fork changed, and all it changed:
//
//   * where the window sits: anchored to the tool's own button on the strip
//     rather than under OrcaSlicer's gizmo toolbar;
//   * the style: ShellTheme tokens in place of push_toolbar_style and
//     push_combo_style, which is why those two calls are gone;
//   * the axis letters, the reset buttons and the checkbox, which are drawn
//     from the fork's palette and its own SVG icons;
//   * the keyboard-shortcut popover is dropped, because its list is private
//     to OrcaSlicer's class.
//
// Values, units, imperial conversion and every write still go through
// GizmoObjectManipulation, so this is a copy of the presentation only. When
// upstream changes those functions, diff them against this file.

namespace Slic3r::GUI::JusPrin {

using namespace std::string_view_literals;

namespace {

// Copied with the windows: the field ids and the clamps they apply.
#define MAX_NUM 9999.99
#define MAX_SIZE std::string_view{"9999.99"}

const char* label_values[3][3] = {
    {"##position_x", "##position_y", "##position_z"},
    {"##rotation_x", "##rotation_y", "##rotation_z"},
    {"##absolute_rotation_x", "##absolute_rotation_y", "##absolute_rotation_z"}};

const char* label_scale_values[2][3] = {{"##scale_x", "##scale_y", "##scale_z"}, {"##size_x", "##size_y", "##size_z"}};

ImU32 im_color(const wxColour& color) { return IM_COL32(color.Red(), color.Green(), color.Blue(), color.Alpha()); }

ImVec4 im_vec4(const wxColour& color)
{
    return ImVec4(color.Red() / 255.f, color.Green() / 255.f, color.Blue() / 255.f, color.Alpha() / 255.f);
}

// The tools this panel draws. Everything else keeps OrcaSlicer's own window.
bool is_ours(GLGizmosManager::EType type)
{
    return type == GLGizmosManager::Move || type == GLGizmosManager::Rotate || type == GLGizmosManager::Scale;
}

StripTool button_for(GLGizmosManager::EType type)
{
    switch (type) {
    case GLGizmosManager::Move:   return StripTool::Move;
    case GLGizmosManager::Rotate: return StripTool::Rotate;
    case GLGizmosManager::Scale:  return StripTool::Scale;
    default:                      return StripTool::More;
    }
}

} // namespace

void ToolValuePanel::render(const ShellTheme& theme, const ShellPalette& palette, const StripLayout& layout, float scale)
{
    GLGizmosManager& gizmos = m_canvas.get_gizmos_manager();
    const GLGizmosManager::EType open_tool = gizmos.get_current_type();
    if (open_tool == GLGizmosManager::Undefined)
        return;

    const ShellMetrics& m = theme.metrics();
    const StripRect& button = layout.buttons[size_t(button_for(open_tool))];
    // The card hangs below the strip, its left edge under the button that
    // opened it, a panel padding clear of the row.
    const float x = button.x;
    const float y = layout.strip.y + layout.strip.h + std::round(m.tool_panel.padding * scale);

    if (!is_ours(open_tool)) {
        // OrcaSlicer positions its own windows by their top-left corner too.
        if (GLGizmoBase* gizmo = gizmos.get_gizmo(open_tool); gizmo != nullptr)
            gizmo->render_input_window(x, y, float(m_canvas.get_canvas_size().get_height()));
        return;
    }

    GizmoObjectManipulation& manip = gizmos.get_object_manipulation();
    // The values are only meaningful once the selection has been measured.
    if (!manip.get_cache().is_valid())
        return;

    // Only this card's own controls, so a tool that is no longer open leaves
    // no stale rectangles behind for the harness to click.
    m_field_rects.clear();

    const float padding = std::round(m.tool_panel.padding * scale);
    const float field_h = std::round(m.field.height * scale);

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, std::round(m.tool_panel.radius * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::round(m.form_row.gap * scale), std::round(m.tool_panel.row_gap * scale)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::round(m.field.radius * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(std::round(m.field.padding_x * scale), std::max(0.f, (field_h - ImGui::GetFontSize()) / 2)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, std::round(m.popover.radius * scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, im_vec4(palette.surface_raised));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, im_vec4(palette.surface_raised));
    ImGui::PushStyleColor(ImGuiCol_Border, im_vec4(palette.border_subtle));
    ImGui::PushStyleColor(ImGuiCol_Text, im_vec4(palette.text_primary));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, im_vec4(palette.surface_canvas));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, im_vec4(palette.surface_selected));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, im_vec4(palette.surface_canvas));
    ImGui::PushStyleColor(ImGuiCol_Button, im_vec4(palette.surface_raised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, im_vec4(palette.surface_selected));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, im_vec4(palette.surface_selected));
    ImGui::PushStyleColor(ImGuiCol_Header, im_vec4(palette.surface_selected));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, im_vec4(palette.surface_selected));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, im_vec4(palette.surface_selected));
    ImGui::PushStyleColor(ImGuiCol_Separator, im_vec4(palette.border_subtle));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, im_vec4(palette.surface_selected));

    ImGui::Begin("##jusprin_tool_values", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    switch (open_tool) {
    case GLGizmosManager::Move:   render_move(theme, palette, manip, scale); break;
    case GLGizmosManager::Rotate: render_rotate(theme, palette, manip, scale); break;
    case GLGizmosManager::Scale:  render_scale(theme, palette, manip, scale); break;
    default: break;
    }

    ImGui::End();
    ImGui::PopStyleColor(15);
    ImGui::PopStyleVar(8);
}

// Ported from GizmoObjectManipulation::do_render_move_window.
void ToolValuePanel::render_move(const ShellTheme& theme, const ShellPalette& palette, GizmoObjectManipulation& manip,
                                 float scale)
{
    ImGuiWrapper* imgui = wxGetApp().imgui();
    auto update = [this, &manip](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    manip.on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    const float space_size = imgui->get_style_scaling() * 8;

    Vec3d original_position;
    if (manip.m_imperial_units)
        original_position = manip.m_new_position * GizmoObjectManipulation::mm_to_in;
    else
        original_position = manip.m_new_position;
    Vec3d display_position = manip.m_buffered_position;

    const float unit_size = imgui->calc_text_size(MAX_SIZE).x + space_size;
    int index = 1;
    int index_unit = 1;

    ImGui::AlignTextToFramePadding();
    const unsigned int current_active_id = ImGui::GetActiveID();

    Selection& selection = m_canvas.get_selection();
    std::vector<std::string> modes = {_u8L("World coordinates"), _u8L("Object coordinates")};
    if (selection.is_multiple_full_object() || selection.is_wipe_tower())
        modes.pop_back();
    size_t selection_idx = (int) manip.get_coordinates_type();
    if (selection_idx >= modes.size()) {
        manip.set_coordinates_type(ECoordinatesType::World);
        selection_idx = 0;
    }

    const float caption_size = imgui->calc_text_size(""sv).x + 2 * space_size;
    const float combox_content_size = imgui->calc_text_size(_L("Object coordinates")).x * 1.2 + imgui->calc_text_size("xxx"sv).x + imgui->scaled(3);
    bool combox_changed = manip.render_combo(imgui, "", modes, selection_idx, caption_size, combox_content_size);
    const float caption_max = combox_content_size - 4 * space_size;

    render_axis_captions(palette, caption_max, unit_size, space_size);

    index = 1;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    if (selection.is_single_full_instance() && manip.is_instance_coordinates())
        imgui->text(_L("Translate(Relative)"));
    else
        imgui->text(_L("Position"));

    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_values[0][0], &display_position[0]);
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_values[0][1], &display_position[1]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_values[0][2], &display_position[2]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui->text(manip.m_new_unit_string);

    bool is_avoid_one_update = false;
    if (combox_changed) {
        manip.set_coordinates_type((ECoordinatesType) selection_idx);
        manip.UpdateAndShow(true);
        is_avoid_one_update = true;
    }
    if (!is_avoid_one_update) {
        for (int i = 0; i < display_position.size(); i++) {
            if (display_position[i] > MAX_NUM) display_position[i] = MAX_NUM;
            if (display_position[i] < -MAX_NUM) display_position[i] = -MAX_NUM;
        }
        manip.m_buffered_position = display_position;
        update(current_active_id, "position", original_position, manip.m_buffered_position);
    }
    // the init position values are not zero, won't add reset button

    send_focus_to_canvas(current_active_id, label_values[0]);
    render_footer(theme, palette, scale);
    m_last_active_item = current_active_id;
}

// Ported from GizmoObjectManipulation::do_render_rotate_window.
void ToolValuePanel::render_rotate(const ShellTheme& theme, const ShellPalette& palette, GizmoObjectManipulation& manip,
                                   float scale)
{
    ImGuiWrapper* imgui = wxGetApp().imgui();
    auto update = [this, &manip](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    manip.on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    const float space_size = imgui->get_style_scaling() * 8;
    const float end_text_size = imgui->calc_text_size(manip.m_new_unit_string).x;

    Vec3d rotation = manip.m_buffered_rotation;
    Vec3d absolute_rotation = manip.m_buffered_absolute_rotation;
    const float unit_size = imgui->calc_text_size(MAX_SIZE).x + space_size;
    int index = 1;
    int index_unit = 1;

    ImGui::AlignTextToFramePadding();
    const unsigned int current_active_id = ImGui::GetActiveID();
    ImGui::PushItemWidth(imgui->calc_text_size(_L("World coordinates")).x + 2 * space_size);
    imgui->text(_L("World coordinates"));

    const float caption_max = imgui->calc_text_size(_L("Object coordinates")).x + 2 * space_size;
    render_axis_captions(palette, caption_max, unit_size, space_size);

    index = 1;
    index_unit = 1;
    bool is_relative_input = false;
    ImGui::AlignTextToFramePadding();
    imgui->text(_L("Rotate (relative)"));
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    if (input_double(palette, label_values[1][0], &rotation[0])) is_relative_input = true;
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (input_double(palette, label_values[1][1], &rotation[1])) is_relative_input = true;
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (input_double(palette, label_values[1][2], &rotation[2])) is_relative_input = true;
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui->text("°");
    manip.m_buffered_rotation = rotation;
    if (is_relative_input)
        manip.m_last_rotate_type = GizmoObjectManipulation::RotateType::Relative;
    if (manip.m_last_rotate_type == GizmoObjectManipulation::RotateType::Relative) {
        if (update(current_active_id, "rotation", manip.m_new_rotation, manip.m_buffered_rotation) >= 0)
            manip.m_last_rotate_type = GizmoObjectManipulation::RotateType::None;
    }

    if (manip.m_show_clear_rotation) {
        ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size + end_text_size);
        if (render_reset(theme, palette, "rotate-ccw", _L("Reset current rotation to the value when open the rotation tool."), scale))
            manip.reset_rotation(true);
    }
    // Both rotation rows share one focus: clearing after each row in turn
    // would undo the hint the other just set, which is why upstream tracks
    // them together and only clears when neither has the caret.
    const bool relative_focused = send_focus_to_canvas(current_active_id, label_values[1], false);

    index = 1;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    imgui->text(_L("Rotate (absolute)"));
    ImGui::SameLine(caption_max + index * space_size);
    ImGui::PushItemWidth(unit_size);
    bool is_absolute_input = false;
    if (input_double(palette, label_values[2][0], &absolute_rotation[0])) is_absolute_input = true;
    ImGui::SameLine(caption_max + unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (input_double(palette, label_values[2][1], &absolute_rotation[1])) is_absolute_input = true;
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    if (input_double(palette, label_values[2][2], &absolute_rotation[2])) is_absolute_input = true;
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui->text("°");
    manip.m_buffered_absolute_rotation = absolute_rotation;
    if (is_absolute_input)
        manip.m_last_rotate_type = GizmoObjectManipulation::RotateType::Absolute;
    if (manip.m_last_rotate_type == GizmoObjectManipulation::RotateType::Absolute) {
        if (update(current_active_id, "absolute_rotation", manip.m_new_absolute_rotation, manip.m_buffered_absolute_rotation) >= 0)
            manip.m_last_rotate_type = GizmoObjectManipulation::RotateType::None;
    }

    if (manip.m_show_reset_0_rotation) {
        ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size + end_text_size);
        if (render_reset(theme, palette, "undo-dot", _L("Reset current rotation to real zeros."), scale))
            manip.reset_rotation(false);
    }
    const bool absolute_focused = send_focus_to_canvas(current_active_id, label_values[2], false);
    if (!relative_focused && !absolute_focused)
        m_canvas.handle_sidebar_focus_event("", false);

    render_footer(theme, palette, scale);
    m_last_active_item = current_active_id;
}

// Ported from GizmoObjectManipulation::do_render_scale_input_window.
void ToolValuePanel::render_scale(const ShellTheme& theme, const ShellPalette& palette, GizmoObjectManipulation& manip,
                                  float scale)
{
    ImGuiWrapper* imgui = wxGetApp().imgui();
    auto update = [this, &manip](unsigned int active_id, std::string opt_key, Vec3d original_value, Vec3d new_value) -> int {
        for (int i = 0; i < 3; i++) {
            if (original_value[i] != new_value[i]) {
                if (active_id != m_last_active_item) {
                    manip.on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };

    const float space_size = imgui->get_style_scaling() * 8;
    const float end_text_size = imgui->calc_text_size(manip.m_new_unit_string).x;
    ImGui::AlignTextToFramePadding();
    const unsigned int current_active_id = ImGui::GetActiveID();

    Vec3d scale_values = manip.m_buffered_scale;
    Vec3d display_size = manip.m_buffered_size;
    const float unit_size = imgui->calc_text_size(MAX_SIZE).x + space_size;
    int index = 2;
    int index_unit = 1;

    Selection& selection = m_canvas.get_selection();
    std::vector<std::string> modes = {_u8L("World coordinates"), _u8L("Object coordinates"), _u8L("Part coordinates")};
    if (selection.is_single_full_object())
        modes.pop_back();
    if (selection.is_multiple_full_object()) {
        modes.pop_back();
        modes.pop_back();
    }
    size_t selection_idx = (int) manip.get_coordinates_type();
    if (selection_idx >= modes.size()) {
        manip.set_coordinates_type(ECoordinatesType::World);
        selection_idx = 0;
    }

    const float caption_size = imgui->calc_text_size(""sv).x + 2 * space_size;
    const float combox_content_size = imgui->calc_text_size(_L("Object coordinates")).x * 1.2 + imgui->calc_text_size("xxx"sv).x + imgui->scaled(3);
    bool combox_changed = manip.render_combo(imgui, "", modes, selection_idx, caption_size, combox_content_size);
    const float caption_max = combox_content_size - 4 * space_size;

    render_axis_captions(palette, caption_max, unit_size, space_size, 2);

    index = 2;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    imgui->text(_L("Scale"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_scale_values[0][0], &scale_values[0]);
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_scale_values[0][1], &scale_values[1]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_scale_values[0][2], &scale_values[2]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui->text("%");
    if (scale_values.x() > 0 && scale_values.y() > 0 && scale_values.z() > 0)
        manip.m_buffered_scale = scale_values;

    if (manip.m_show_clear_scale) {
        ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size + end_text_size);
        if (render_reset(theme, palette, "rotate-ccw", _L("Reset scale"), scale))
            manip.reset_scale();
    }

    Vec3d original_size;
    if (manip.m_imperial_units)
        original_size = manip.m_new_size * GizmoObjectManipulation::mm_to_in;
    else
        original_size = manip.m_new_size;

    index = 2;
    index_unit = 1;
    ImGui::AlignTextToFramePadding();
    imgui->text(_L("Size"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_scale_values[1][0], &display_size[0]);
    ImGui::SameLine(caption_max + unit_size + index * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_scale_values[1][1], &display_size[1]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    ImGui::PushItemWidth(unit_size);
    input_double(palette, label_scale_values[1][2], &display_size[2]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size);
    imgui->text(manip.m_new_unit_string);
    for (int i = 0; i < display_size.size(); i++) {
        if (std::abs(display_size[i]) > MAX_NUM)
            display_size[i] = MAX_NUM;
    }
    if (display_size.x() > 0 && display_size.y() > 0 && display_size.z() > 0)
        manip.m_buffered_size = display_size;

    ImGui::AlignTextToFramePadding();
    bool is_avoid_one_update = false;
    if (combox_changed) {
        manip.set_coordinates_type((ECoordinatesType) selection_idx);
        manip.UpdateAndShow(true);
        is_avoid_one_update = true;
    }

    int size_sel = -1;
    if (!is_avoid_one_update)
        size_sel = update(current_active_id, "size", original_size, manip.m_buffered_size);

    if (render_checkbox(theme, palette, _L("Uniform scale"), manip.get_uniform_scaling(), scale))
        manip.set_uniform_scaling(!manip.get_uniform_scaling());

    const int scale_sel = update(current_active_id, "scale", manip.m_new_scale, manip.m_buffered_scale);
    if (scale_sel >= 0) {
        for (int i = 0; i < 3; ++i) {
            if (i != scale_sel) ImGui::ClearInputTextInitialData(label_scale_values[0][i], manip.m_buffered_scale[i]);
            ImGui::ClearInputTextInitialData(label_scale_values[1][i], manip.m_buffered_size[i]);
        }
    }
    if (size_sel >= 0) {
        for (int i = 0; i < 3; ++i) {
            ImGui::ClearInputTextInitialData(label_scale_values[0][i], manip.m_buffered_scale[i]);
            if (i != size_sel) ImGui::ClearInputTextInitialData(label_scale_values[1][i], manip.m_buffered_size[i]);
        }
    }

    bool focused = false;
    for (int i = 0; i < 2 && !focused; i++)
        focused = send_focus_to_canvas(current_active_id, label_scale_values[i], false);
    if (!focused)
        m_canvas.handle_sidebar_focus_event("", false);

    render_footer(theme, palette, scale);
    m_last_active_item = current_active_id;
}

// OrcaSlicer's own numeric field, plus a note of where it landed so the
// integration harness can click it. BBLInputDouble is upstream's widget:
// keeping it keeps the typing, the select-on-focus and the commit behaviour
// identical to its panels.
bool ToolValuePanel::input_double(const ShellPalette& palette, const char* label, double* value)
{
    const bool edited = ImGui::BBLInputDouble(label, value, 0.0f, 0.0f, "%.2f");
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    m_field_rects[label] = StripRect{min.x, min.y, max.x - min.x, max.y - min.y};
    // BBLInputDouble hard-codes OrcaSlicer's teal for the focused frame, and
    // it pushes that colour inside itself where nothing the fork pushes can
    // reach it. The focus ring the design system asks for is drawn over it.
    if (ImGui::IsItemActive()) {
        const float rounding = ImGui::GetStyle().FrameRounding;
        ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
                                            im_color(palette.border_focus), rounding, 0, 2.f);
    }
    return edited;
}

// The coloured X, Y and Z over the three value columns. Upstream draws these
// in OrcaSlicer's own axis colours; the fork reads the same colours from the
// token file, where they are the one group that is equal in both modes.
void ToolValuePanel::render_axis_captions(const ShellPalette& palette, float caption_max, float unit_size,
                                          float space_size, int first_index)
{
    const wxColour* colors[3] = {&palette.axis_x, &palette.axis_y, &palette.axis_z};
    static const char* names[3] = {"X", "Y", "Z"};
    const float offset_to_center = (unit_size - ImGui::CalcTextSize("O").x) / 2;
    int index = first_index;
    int index_unit = 1;
    ImGui::SameLine(caption_max + (first_index == 1 ? index * space_size : space_size) + offset_to_center);
    ImGui::TextColored(im_vec4(*colors[0]), "%s", names[0]);
    ImGui::SameLine(caption_max + unit_size + (first_index == 1 ? (++index) : index) * space_size + offset_to_center);
    ImGui::TextColored(im_vec4(*colors[1]), "%s", names[1]);
    ImGui::SameLine(caption_max + (++index_unit) * unit_size + (++index) * space_size + offset_to_center);
    ImGui::TextColored(im_vec4(*colors[2]), "%s", names[2]);
}

// Upstream's "send focus to m_glcanvas": while a field is being edited the
// canvas highlights the matching axis, and the "+ 2" drops the "##" the field
// ids carry.
bool ToolValuePanel::send_focus_to_canvas(unsigned int active_id, const char* const labels[3], bool clear_when_unfocused)
{
    for (int j = 0; j < 3; j++) {
        if (active_id == ImGui::GetID(labels[j])) {
            m_canvas.handle_sidebar_focus_event(labels[j] + 2, true);
            return true;
        }
    }
    if (clear_when_unfocused)
        m_canvas.handle_sidebar_focus_event("", false);
    return false;
}

// Upstream closes its panels with a right-aligned Done; the shortcut popover
// beside it is dropped, its list being private to OrcaSlicer's class.
void ToolValuePanel::render_footer(const ShellTheme& theme, const ShellPalette& palette, float scale)
{
    ImGui::Separator();
    ImGuiWrapper* imgui = wxGetApp().imgui();
    ImGui::PushStyleColor(ImGuiCol_Button, im_vec4(palette.action_primary));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, im_vec4(palette.action_primary_hover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, im_vec4(palette.action_primary_pressed));
    ImGui::PushStyleColor(ImGuiCol_Text, im_vec4(palette.text_on_action));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::round(theme.metrics().button.primary.radius * scale));
    GLGizmoUtils::begin_right_aligned_buttons({_L("Done")});
    if (imgui->button(_L("Done")))
        m_canvas.reset_all_gizmos();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

// ImGui's own checkbox draws its tick in the text colour inside the frame,
// which leaves the checked state invisible in dark mode; the design system
// fills the box with the action colour and puts the tick on top.
bool ToolValuePanel::render_checkbox(const ShellTheme& theme, const ShellPalette& palette, const wxString& label,
                                     bool checked, float scale)
{
    const ShellMetrics& m = theme.metrics();
    const float side = std::round(m.checkbox.size * scale);
    const int glyph = int(std::lround(m.checkbox.glyph_size * scale));
    const float radius = std::round(m.checkbox.radius * scale);

    ImGui::PushID("uniform");
    const bool toggled = ImGui::InvisibleButton("##box", ImVec2(side, side));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    draw.AddRectFilled(min, max, im_color(checked ? palette.action_primary : palette.surface_canvas), radius);
    draw.AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
                 im_color(ImGui::IsItemHovered() ? palette.border_focus : palette.border_strong), radius, 0, 1.f);
    if (checked) {
        const float inset = (side - glyph) / 2;
        draw.AddImage((ImTextureID)(intptr_t)canvas_icon_texture("check", glyph), ImVec2(min.x + inset, min.y + inset),
                      ImVec2(min.x + inset + glyph, min.y + inset + glyph), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                      im_color(palette.text_on_action));
    }
    ImGui::SameLine(0.f, std::round(m.checkbox.label_gap * scale));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.ToUTF8().data());
    ImGui::PopID();
    return toggled;
}

// Upstream's reset buttons take their glyphs from the gizmo icon atlas; the
// fork draws the same buttons with its own SVGs.
bool ToolValuePanel::render_reset(const ShellTheme& theme, const ShellPalette& palette, const char* icon,
                                  const wxString& tooltip, float scale)
{
    const ShellMetrics& m = theme.metrics();
    const float side = std::round(m.button.icon.width * scale);
    const int icon_px = int(std::lround(m.button.icon.icon_size * scale));
    ImGui::PushID(icon);
    const bool pressed = ImGui::Button("##reset", ImVec2(side, side));
    const ImVec2 min = ImGui::GetItemRectMin();
    m_field_rects[std::string("##reset_") + icon] = StripRect{min.x, min.y, side, side};
    const float inset = (side - icon_px) / 2;
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)canvas_icon_texture(icon, icon_px),
                                         ImVec2(min.x + inset, min.y + inset),
                                         ImVec2(min.x + inset + icon_px, min.y + inset + icon_px), ImVec2(0.f, 0.f),
                                         ImVec2(1.f, 1.f), im_color(palette.text_secondary));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.ToUTF8().data());
    ImGui::PopID();
    return pressed;
}

} // namespace Slic3r::GUI::JusPrin
