#include "IssueOverlay.hpp"

#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Canvas/ViewportToolStrip.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "libslic3r/Model.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Slic3r::GUI::JusPrin::PrintIssues {

// One frame's sizes and colours, read once from the theme.
struct IssueOverlay::Style
{
    const ShellMetrics& m;
    const ShellPalette& p;
    float scale, canvas_w, canvas_h;
    float inset, pad, gap, line, dot;
};

namespace {

constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

ImU32  im_color(const wxColour& color) { return IM_COL32(color.Red(), color.Green(), color.Blue(), color.Alpha()); }
ImVec4 im_vec(const wxColour& color) { return ImVec4(color.Red() / 255.f, color.Green() / 255.f, color.Blue() / 255.f, color.Alpha() / 255.f); }

// A window that is only a place to draw and to take the pointer.
void begin_bare(const char* name, const OverlayRect& rect)
{
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::Begin(name, nullptr, kBareWindow);
    ImGui::PopStyleVar(3);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
}

void panel(ImDrawList& draw, const OverlayRect& rect, const ShellPalette& p, float radius)
{
    const ImVec2 min(rect.x, rect.y), max(rect.x + rect.w, rect.y + rect.h);
    draw.AddRectFilled(min, max, im_color(p.surface_raised), radius);
    // Hairlines are one device pixel, as the shell's own borders are.
    draw.AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), im_color(p.border_subtle), radius, 0, 1.f);
}

// A button drawn from a recipe: the colours are the caller's, the click is ImGui's.
bool button(ImDrawList& draw, const char* id, const OverlayRect& rect, const std::string& label, float radius, bool enabled,
            const wxColour& fill, const wxColour& fill_hover, const wxColour& text, const wxColour* border, const ShellPalette& p)
{
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(rect.w, rect.h));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min(rect.x, rect.y), max(rect.x + rect.w, rect.y + rect.h);
    draw.AddRectFilled(min, max, im_color(!enabled ? p.action_disabled : hovered ? fill_hover : fill), radius);
    if (border != nullptr && enabled)
        draw.AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), im_color(*border), radius, 0, 1.f);
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(rect.x + std::round((rect.w - size.x) / 2), rect.y + std::round((rect.h - size.y) / 2)),
                 im_color(enabled ? text : p.action_disabled_text), label.c_str());
    return clicked && enabled;
}

std::string first_line(const std::string& text)
{
    std::string line = text.substr(0, text.find('\n'));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

std::string trimmed(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

const wxColour& tone(const PrintIssue& issue, const ShellPalette& p)
{
    return issue.stale ? p.text_secondary : issue.severity == IssueSeverity::Blocker ? p.status_danger : p.status_warning;
}

std::string checking_label() { return _u8L("Checking..."); }
std::string plate_label() { return _u8L("Whole plate"); }

// What the pill says: never "no issues" while OrcaSlicer has not looked.
std::string status_label(const PrintIssueSnapshot& snapshot, std::size_t blockers)
{
    if (!snapshot.checked)
        return checking_label();
    if (snapshot.issues.empty())
        return _u8L("No print issues");
    const int count = int(snapshot.issues.size());
    std::string label = format_wxstr(_L_PLURAL("%1% print issue", "%1% print issues", count), count).ToUTF8().data();
    if (blockers != 0)
        label += " \xC2\xB7 " + format(_u8L("%1% blocking"), blockers);
    return label;
}

} // namespace

IssueOverlay::IssueOverlay(GLCanvas3D& canvas, PrintIssueMonitor& monitor, Actions actions)
    : m_canvas(canvas), m_monitor(monitor), m_actions(std::move(actions)), m_self(std::make_shared<IssueOverlay*>(this))
{
    // The list changed without the scene changing: the canvas has to draw.
    m_monitor.set_changed_callback([this] { request_frame(); });
}

IssueOverlay::~IssueOverlay() { m_monitor.set_changed_callback({}); }

void IssueOverlay::request_frame()
{
    m_canvas.set_as_dirty();
    m_canvas.request_extra_frame();
}

void IssueOverlay::after_frame(std::function<void(IssueOverlay&)> action)
{
    // A click may change the selection or post a message; neither belongs
    // inside the canvas's render pass.
    wxGetApp().CallAfter([self = std::weak_ptr<IssueOverlay*>(m_self), action = std::move(action)]() {
        if (const auto overlay = self.lock())
            action(**overlay);
    });
}

void IssueOverlay::select(const std::string& issue_id)
{
    m_selected = issue_id;
    if (const PrintIssue* issue = m_monitor.snapshot().find(issue_id); issue != nullptr && m_actions.highlight)
        m_actions.highlight(*issue);
    request_frame();
}

void IssueOverlay::set_list_open(bool open)
{
    m_list_open = open;
    request_frame();
}

std::optional<IssueOverlay::Target> IssueOverlay::target_of(const PrintIssue& issue) const
{
    const Model* model = m_canvas.get_model();
    if (model == nullptr || issue.object == 0)
        return std::nullopt;
    // The object's box in the scene as it is drawn now, so a drag that has
    // not been committed to the model still carries the bubble.
    BoundingBoxf3 box;
    for (const GLVolume* volume : m_canvas.get_volumes().volumes) {
        const int object_index = volume->object_idx(), instance_index = volume->instance_idx();
        if (object_index < 0 || object_index >= int(model->objects.size()))
            continue;
        const ModelObject* object = model->objects[object_index];
        if (object->id().id != issue.object || instance_index < 0 || instance_index >= int(object->instances.size()))
            continue;
        if (issue.instance != 0 && object->instances[instance_index]->id().id != issue.instance)
            continue;
        box.merge(volume->transformed_bounding_box());
    }
    if (!box.defined)
        return std::nullopt;

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Eigen::Matrix4d to_clip = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
    const std::array<int, 4>& viewport = camera.get_viewport();
    const auto project = [&](double x, double y, double z) -> std::optional<ImVec2> {
        const Eigen::Vector4d clip = to_clip * Eigen::Vector4d(x, y, z, 1.0);
        if (clip.w() <= 1e-9)
            return std::nullopt; // behind the camera
        return ImVec2(float((0.5 + 0.5 * clip.x() / clip.w()) * viewport[2]), float((0.5 - 0.5 * clip.y() / clip.w()) * viewport[3]));
    };

    Target target;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const auto point = project(corner & 1 ? box.max.x() : box.min.x(), corner & 2 ? box.max.y() : box.min.y(),
                                   corner & 4 ? box.max.z() : box.min.z());
        if (!point)
            return std::nullopt;
        x0 = corner == 0 ? point->x : std::min(x0, point->x);
        y0 = corner == 0 ? point->y : std::min(y0, point->y);
        x1 = corner == 0 ? point->x : std::max(x1, point->x);
        y1 = corner == 0 ? point->y : std::max(y1, point->y);
    }
    const auto top = project(box.center().x(), box.center().y(), box.max.z());
    if (!top)
        return std::nullopt;
    target.rect     = {x0, y0, x1 - x0, y1 - y0};
    target.anchor_x = top->x;
    target.anchor_y = top->y;
    return target;
}

void IssueOverlay::render()
{
    m_frame = {};
    // Without the token file the app keeps OrcaSlicer's stock presentation,
    // which has no overlay; the failure was logged when the tokens were read.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr)
        return;
    const PrintIssueSnapshot& snapshot = m_monitor.snapshot();
    const ShellMetrics&       m        = theme->metrics();
    const float               scale    = wxGetApp().imgui()->get_style_scaling();
    const Size                canvas   = m_canvas.get_canvas_size();
    const Style style{m, theme->palette(GUI_App::dark_mode()), scale, float(canvas.get_width()), float(canvas.get_height()),
                      std::round(m.space_3 * scale), std::round(m.space_3 * scale), std::round(m.space_2 * scale),
                      ImGui::GetTextLineHeight(), std::round(m.space_2 * scale)};

    if (!m_selected.empty() && snapshot.find(m_selected) == nullptr)
        m_selected.clear(); // the native source stopped reporting it
    if (snapshot.issues.empty())
        m_list_open = false;

    std::size_t blockers = 0;
    for (const PrintIssue& issue : snapshot.issues)
        blockers += !issue.stale && issue.severity == IssueSeverity::Blocker ? 1 : 0;
    m_frame.pill_text = status_label(snapshot, blockers);

    const float pill_w     = style.pad + style.dot + style.gap + ImGui::CalcTextSize(m_frame.pill_text.c_str()).x + style.pad;
    const float row_height = std::round(m.menu_row.height * scale) + style.line; // two lines of text
    const float list_height = m_list_open ? float(snapshot.issues.size()) * row_height + 2 * std::round(m.popover.padding_y * scale) : 0.f;
    const StatusLayout layout = layout_status(style.canvas_w, style.canvas_h, style.inset, style.gap, pill_w,
                                              std::round(m.chip.height * scale), std::round(m.print_issues.list_width * scale), list_height);

    draw_pill(style, layout, snapshot);
    if (m_list_open)
        draw_list(style, layout, snapshot, row_height);
    if (const PrintIssue* issue = m_selected.empty() ? nullptr : snapshot.find(m_selected))
        draw_bubble(style, layout, snapshot, *issue);
}

void IssueOverlay::draw_pill(const Style& s, const StatusLayout& layout, const PrintIssueSnapshot& snapshot)
{
    const OverlayRect& pill = layout.pill;
    m_frame.pill = pill;

    std::size_t current = 0, blockers = 0;
    for (const PrintIssue& issue : snapshot.issues) {
        current += issue.stale ? 0 : 1;
        blockers += !issue.stale && issue.severity == IssueSeverity::Blocker ? 1 : 0;
    }
    const wxColour& dot = !snapshot.checked || (current == 0 && !snapshot.issues.empty()) ? s.p.text_secondary :
                          snapshot.issues.empty() ? s.p.status_success : blockers != 0 ? s.p.status_danger : s.p.status_warning;

    begin_bare("##jusprin_issue_pill", pill);
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(pill.x, pill.y));
    if (ImGui::InvisibleButton("##pill", ImVec2(pill.w, pill.h)) && !snapshot.issues.empty())
        after_frame([](IssueOverlay& self) { self.set_list_open(!self.m_list_open); });
    panel(draw, pill, s.p, std::round(s.m.chip.radius * s.scale));
    draw.AddCircleFilled(ImVec2(pill.x + s.pad + s.dot / 2, pill.y + pill.h / 2), s.dot / 2, im_color(dot));
    const ImVec2 size = ImGui::CalcTextSize(m_frame.pill_text.c_str());
    draw.AddText(ImVec2(pill.x + s.pad + s.dot + s.gap, pill.y + std::round((pill.h - size.y) / 2)), im_color(s.p.text_primary),
                 m_frame.pill_text.c_str());
    ImGui::End();
}

void IssueOverlay::draw_list(const Style& s, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, float row_height)
{
    const OverlayRect& list = layout.list;
    m_frame.list_open = true;

    // A real ImGui window rather than a bare one: with more issues than the
    // canvas has room for, it scrolls.
    ImGui::SetNextWindowPos(ImVec2(list.x, list.y));
    ImGui::SetNextWindowSize(ImVec2(list.w, list.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(s.m.menu_row.side_inset * s.scale), std::round(s.m.popover.padding_y * s.scale)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, std::round(s.m.popover.radius * s.scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, im_vec(s.p.surface_raised));
    ImGui::PushStyleColor(ImGuiCol_Border, im_vec(s.p.border_subtle));
    ImGui::Begin("##jusprin_issue_list", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    ImDrawList& draw = *ImGui::GetWindowDrawList();
    int index = 0;
    for (const PrintIssue& issue : snapshot.issues) {
        ImGui::PushID(index++);
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(ImGui::GetContentRegionAvail().x, row_height));
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        m_frame.rows.emplace_back(issue.id, OverlayRect{min.x, min.y, max.x - min.x, max.y - min.y});
        if (ImGui::IsItemHovered() || issue.id == m_selected)
            draw.AddRectFilled(min, max, im_color(s.p.surface_selected), std::round(s.m.menu_row.radius * s.scale));
        draw.AddCircleFilled(ImVec2(min.x + s.pad / 2 + s.dot / 2, (min.y + max.y) / 2), s.dot / 2, im_color(tone(issue, s.p)));
        const float text_x = min.x + s.pad / 2 + s.dot + s.gap;
        const float text_y = min.y + std::round((row_height - 2 * s.line) / 2);
        const std::string where = (issue.object_name.empty() ? plate_label() : issue.object_name) +
                                  (issue.stale ? " \xC2\xB7 " + checking_label() : std::string());
        draw.PushClipRect(ImVec2(text_x, min.y), ImVec2(max.x - s.gap, max.y), true);
        draw.AddText(ImVec2(text_x, text_y), im_color(s.p.text_primary), first_line(issue.message).c_str());
        draw.AddText(ImVec2(text_x, text_y + s.line), im_color(s.p.text_secondary), where.c_str());
        draw.PopClipRect();
        if (clicked)
            after_frame([id = issue.id](IssueOverlay& self) { self.select(id); });
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

void IssueOverlay::draw_bubble(const Style& s, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, const PrintIssue& issue)
{
    // Every finding on the same object shares the bubble; none is dropped.
    // OrcaSlicer names a copy for some findings and only the object for
    // others, so the object is what they have in common.
    std::vector<const PrintIssue*> group;
    for (const PrintIssue& other : snapshot.issues)
        if (other.object == issue.object)
            group.push_back(&other);
    const int group_index = int(std::find(group.begin(), group.end(), &issue) - group.begin());

    const std::optional<Target> target = target_of(issue);
    const bool in_view = target && target->anchor_x >= 0 && target->anchor_x <= s.canvas_w && target->anchor_y >= 0 &&
                         target->anchor_y <= s.canvas_h;
    const bool busy    = m_actions.chat_busy && m_actions.chat_busy();
    const bool enabled = snapshot.checked && !issue.stale && !busy;

    const std::string severity = issue.stale ? checking_label() :
                                 issue.severity == IssueSeverity::Blocker ? _u8L("Blocks printing") : _u8L("Warning");
    const std::string where    = issue.object == 0 ? plate_label() :
                                 issue.object_name + (target && !in_view ? " \xC2\xB7 " + _u8L("out of view") : std::string());
    const std::string message  = trimmed(issue.message);
    const std::string pager    = group.size() > 1 ? format(_u8L("%1% of %2%"), group_index + 1, group.size()) : std::string();
    const std::string note     = busy ? _u8L("The assistant is replying...") : std::string();

    const float width = std::round(s.m.print_issues.bubble_width * s.scale);
    const float wrap  = width - 2 * s.pad;
    const ImVec2 message_size = ImGui::CalcTextSize(message.c_str(), nullptr, false, wrap);
    const ButtonRecipe& ask_recipe     = s.m.button.secondary;
    const ButtonRecipe& resolve_recipe = s.m.button.primary;
    // Both recipes size a button by padding around its label; the taller one
    // sets the row so the pair lines up.
    const float button_h = std::round(s.line + 2 * std::max(ask_recipe.padding_y, resolve_recipe.padding_y) * s.scale);

    BubbleRequest request;
    request.canvas_w    = s.canvas_w;
    request.canvas_h    = s.canvas_h;
    request.inset       = s.inset;
    request.leader      = std::round(s.m.space_4 * s.scale);
    request.pad         = s.pad;
    request.bubble_w    = width;
    request.bubble_h    = s.pad + s.line + s.gap + message_size.y + (note.empty() ? 0 : s.gap + s.line) + s.pad + button_h + s.pad;
    request.has_target  = in_view;
    request.dock_right  = layout.dock_right;
    request.dock_bottom = layout.dock_bottom;
    request.taken       = {layout.pill, layout.list};
    if (in_view) {
        request.target   = target->rect;
        request.anchor_x = target->anchor_x;
        request.anchor_y = target->anchor_y;
    }
    if (!m_canvas.get_selection().is_empty()) {
        const StripRect strip = layout_tool_strip(tool_strip_geometry(s.m), s.scale, s.canvas_w, s.canvas_h).strip;
        request.taken.push_back({strip.x, strip.y, strip.w, strip.h});
    }
    const BubblePlacement placement = place_bubble(request);
    const OverlayRect&    bubble    = placement.rect;

    m_frame.bubble          = true;
    m_frame.side            = placement.side;
    m_frame.target_in_view  = in_view;
    m_frame.bubble_rect     = bubble;
    m_frame.issue_id        = issue.id;
    m_frame.group_index     = group_index;
    m_frame.group_size      = int(group.size());
    m_frame.actions_enabled = enabled;
    if (target) {
        m_frame.anchor_x = target->anchor_x;
        m_frame.anchor_y = target->anchor_y;
    }

    begin_bare("##jusprin_issue_bubble", bubble);
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    draw.PushClipRectFullScreen();
    if (placement.side != BubbleSide::Docked) {
        // The leader: from the bubble's nearest edge to the object.
        const ImVec2 to(placement.leader_to_x, placement.leader_to_y);
        draw.AddLine(ImVec2(placement.leader_from_x, placement.leader_from_y), to, im_color(tone(issue, s.p)), 2.f * s.scale);
        draw.AddCircleFilled(to, s.dot / 2, im_color(tone(issue, s.p)));
    }
    panel(draw, bubble, s.p, std::round(s.m.radius_container * s.scale));

    float y = bubble.y + s.pad;
    draw.AddCircleFilled(ImVec2(bubble.x + s.pad + s.dot / 2, y + s.line / 2), s.dot / 2, im_color(tone(issue, s.p)));
    draw.AddText(ImVec2(bubble.x + s.pad + s.dot + s.gap, y), im_color(s.p.text_primary), severity.c_str());
    const ImVec2 severity_size = ImGui::CalcTextSize(severity.c_str());
    draw.PushClipRect(ImVec2(bubble.x, bubble.y), ImVec2(bubble.x + bubble.w - s.pad, bubble.y + bubble.h), true);
    draw.AddText(ImVec2(bubble.x + s.pad + s.dot + s.gap + severity_size.x + s.gap, y), im_color(s.p.text_secondary), where.c_str());
    draw.PopClipRect();
    if (!pager.empty()) {
        // The pager is the way to the object's other findings.
        const ImVec2 pager_size = ImGui::CalcTextSize(pager.c_str());
        const OverlayRect next{bubble.x + bubble.w - s.pad - pager_size.x - s.gap, y - s.gap / 2, pager_size.x + s.gap, s.line + s.gap};
        m_frame.next = next;
        if (button(draw, "##next", next, pager, std::round(s.m.radius_compact * s.scale), true, s.p.surface_subtle, s.p.surface_selected,
                   s.p.text_secondary, nullptr, s.p))
            after_frame([id = group[(group_index + 1) % group.size()]->id](IssueOverlay& self) { self.select(id); });
    }
    y += s.line + s.gap;
    draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(bubble.x + s.pad, y), im_color(s.p.text_primary), message.c_str(), nullptr, wrap);
    y += message_size.y;
    if (!note.empty()) {
        y += s.gap;
        draw.AddText(ImVec2(bubble.x + s.pad, y), im_color(s.p.text_secondary), note.c_str());
        y += s.line;
    }
    y += s.pad;

    const float button_w = std::round((bubble.w - 2 * s.pad - s.gap) / 2);
    const OverlayRect ask{bubble.x + s.pad, y, button_w, button_h};
    const OverlayRect resolve{ask.x + ask.w + s.gap, y, bubble.w - 2 * s.pad - s.gap - button_w, button_h};
    m_frame.ask     = ask;
    m_frame.resolve = resolve;
    if (button(draw, "##ask", ask, _u8L("Ask AI"), std::round(ask_recipe.radius * s.scale), enabled, s.p.action_secondary,
               s.p.action_secondary_hover, s.p.action_secondary_text, &s.p.action_secondary_border, s.p))
        after_frame([id = issue.id](IssueOverlay& self) {
            if (self.m_actions.submit) self.m_actions.submit(id, IssueIntent::Explain);
        });
    if (button(draw, "##resolve", resolve, _u8L("Resolve with AI"), std::round(resolve_recipe.radius * s.scale), enabled, s.p.action_primary,
               s.p.action_primary_hover, s.p.text_on_action, nullptr, s.p))
        after_frame([id = issue.id](IssueOverlay& self) {
            if (self.m_actions.submit) self.m_actions.submit(id, IssueIntent::Resolve);
        });
    draw.PopClipRect();
    ImGui::End();
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
