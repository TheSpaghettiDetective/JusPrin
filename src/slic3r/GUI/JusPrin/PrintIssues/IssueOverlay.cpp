#include "IssueOverlay.hpp"

#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Canvas/CanvasIcons.hpp"
#include "slic3r/GUI/JusPrin/Canvas/ViewportToolStrip.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/format.hpp"
#include "libslic3r/Model.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <wx/event.h>
#include <wx/glcanvas.h>

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
    // The icon button recipe: its side and the icon inside it.
    float icon_button;
    int   icon;
};

// Bound to the canvas's window after the canvas bound its own handlers, so
// wxWidgets calls these first. The window forgets a handler object that is
// destroyed, so nothing is unbound by hand.
class IssueOverlay::Input final : public wxEvtHandler
{
public:
    Input(IssueOverlay& overlay, wxWindow& window) : m_overlay(overlay)
    {
        window.Bind(wxEVT_CHAR, &Input::on_char, this);
        window.Bind(wxEVT_LEFT_DOWN, &Input::on_press, this);
        window.Bind(wxEVT_MIDDLE_DOWN, &Input::on_press, this);
        window.Bind(wxEVT_RIGHT_DOWN, &Input::on_press, this);
    }

private:
    void on_char(wxKeyEvent& event)
    {
        // Escape closes one thing at a time: what it closed here it does not
        // also deselect in the canvas.
        if (event.GetKeyCode() != WXK_ESCAPE || !m_overlay.on_escape())
            event.Skip();
    }
    void on_press(wxMouseEvent& event)
    {
        // The canvas scales the pointer the same way before ImGui sees it.
        const float scale = m_overlay.m_canvas.get_scale();
        m_overlay.on_pointer_down(float(event.GetX()) * scale, float(event.GetY()) * scale);
        event.Skip(); // the press is still the canvas's
    }

    IssueOverlay& m_overlay;
};

namespace {

constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

ImU32  im_color(const wxColour& color) { return IM_COL32(color.Red(), color.Green(), color.Blue(), color.Alpha()); }

// How long the pill's ring runs when a new issue arrives, and how many times
// it spreads in that time. Long enough to be seen from the chat panel, short
// enough to be over before it is a distraction; it never repeats on its own.
constexpr std::chrono::milliseconds kAnnouncement{1400};
constexpr int                       kAnnouncementRings = 2;
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

bool contains(const OverlayRect& rect, float x, float y)
{
    return !rect.empty() && x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

void panel(ImDrawList& draw, const OverlayRect& rect, const ShellPalette& p, float radius, const wxColour& fill)
{
    const ImVec2 min(rect.x, rect.y), max(rect.x + rect.w, rect.y + rect.h);
    draw.AddRectFilled(min, max, im_color(fill), radius);
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

void icon(ImDrawList& draw, const char* name, int size, float x, float y, const wxColour& color)
{
    draw.AddImage((ImTextureID)(intptr_t)canvas_icon_texture(name, size), ImVec2(x, y), ImVec2(x + float(size), y + float(size)),
                  ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), im_color(color));
}

// The icon button recipe: a square with the icon in its middle.
bool icon_button(ImDrawList& draw, const char* id, const OverlayRect& rect, const char* name, int size, float radius, const ShellPalette& p)
{
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(rect.w, rect.h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        draw.AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), im_color(p.surface_selected), radius);
    icon(draw, name, size, rect.x + std::round((rect.w - float(size)) / 2), rect.y + std::round((rect.h - float(size)) / 2),
         hovered ? p.text_primary : p.text_secondary);
    return clicked;
}

// An action that is not one of the bubble's two buttons: words, underlined
// under the pointer.
bool link(ImDrawList& draw, const char* id, const OverlayRect& rect, const std::string& label, const ShellPalette& p)
{
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(rect.w, rect.h));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 size  = ImGui::CalcTextSize(label.c_str());
    const float  y     = rect.y + std::round((rect.h - size.y) / 2);
    const ImU32  color = im_color(hovered ? p.text_primary : p.text_secondary);
    draw.AddText(ImVec2(rect.x, y), color, label.c_str());
    if (hovered)
        draw.AddLine(ImVec2(rect.x, y + size.y + 0.5f), ImVec2(rect.x + size.x, y + size.y + 0.5f), color, 1.f);
    return clicked;
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
    return issue.stale || issue.ignored ? p.text_secondary :
           issue.severity == IssueSeverity::Blocker ? p.status_danger : p.status_warning;
}

// What the status counts: the issues nobody asked it to leave out.
struct Tally
{
    std::size_t counted{0}, current{0}, blockers{0}, ignored{0};
};

Tally tally(const PrintIssueSnapshot& snapshot)
{
    Tally t;
    for (const PrintIssue& issue : snapshot.issues) {
        t.ignored += issue.ignored ? 1 : 0;
        t.counted += issue.ignored ? 0 : 1;
        t.current += !issue.ignored && !issue.stale ? 1 : 0;
        t.blockers += !issue.stale && issue.severity == IssueSeverity::Blocker ? 1 : 0;
    }
    return t;
}

std::string checking_label() { return _u8L("Checking..."); }
std::string plate_label() { return _u8L("Whole plate"); }
std::string ignored_label() { return _u8L("Ignored"); }

// What the pill says while there is an issue to say it about.
std::string status_label(const PrintIssueSnapshot& snapshot, const Tally& t)
{
    if (!snapshot.checked)
        return checking_label();
    // Nothing left that counts, and still not all clear: say what was done.
    if (t.counted == 0)
        return format_wxstr(_L_PLURAL("%1% warning ignored", "%1% warnings ignored", int(t.ignored)), t.ignored).ToUTF8().data();
    const int count = int(t.counted);
    std::string label = format_wxstr(_L_PLURAL("%1% print issue", "%1% print issues", count), count).ToUTF8().data();
    if (t.blockers != 0)
        label += " \xC2\xB7 " + format(_u8L("%1% blocking"), t.blockers);
    return label;
}

} // namespace

IssueOverlay::IssueOverlay(GLCanvas3D& canvas, PrintIssueMonitor& monitor, Actions actions)
    : m_canvas(canvas), m_monitor(monitor), m_actions(std::move(actions)), m_self(std::make_shared<IssueOverlay*>(this))
{
    // The list changed without the scene changing: the canvas has to draw.
    m_monitor.set_changed_callback([this] { request_frame(); });
    m_input = std::make_unique<Input>(*this, *m_canvas.get_wxglcanvas());
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
    m_selected  = issue_id;
    m_bound     = false;
    m_list_open = false;
    if (const PrintIssue* issue = m_monitor.snapshot().find(issue_id); issue != nullptr && m_actions.highlight)
        m_actions.highlight(*issue);
    request_frame();
}

void IssueOverlay::close_bubble()
{
    m_selected.clear();
    request_frame();
}

void IssueOverlay::set_list_open(bool open)
{
    m_list_open = open;
    if (open)
        m_selected.clear();
    request_frame();
}

void IssueOverlay::press_pill()
{
    const PrintIssueSnapshot& snapshot = m_monitor.snapshot();
    if (snapshot.issues.empty())
        return;
    if (snapshot.issues.size() > 1)
        set_list_open(!m_list_open);
    else if (m_selected.empty())
        select(snapshot.issues.front().id);
    else
        close_bubble();
}

bool IssueOverlay::selection_holds(const PrintIssue& issue) const
{
    const Model* model = m_canvas.get_model();
    if (model == nullptr)
        return false;
    for (const auto& [object_index, instances] : m_canvas.get_selection().get_content()) {
        // The wipe tower is selected under an index that is no object's.
        if (object_index < 0 || object_index >= int(model->objects.size()))
            continue;
        const ModelObject* object = model->objects[object_index];
        if (object->id().id != issue.object)
            continue;
        if (issue.instance == 0)
            return true;
        for (const int instance_index : instances)
            if (instance_index >= 0 && instance_index < int(object->instances.size()) &&
                object->instances[instance_index]->id().id == issue.instance)
                return true;
    }
    return false;
}

void IssueOverlay::watch_for_arrivals(const PrintIssueSnapshot& snapshot)
{
    if (snapshot.session == m_known_session && snapshot.generation == m_known_generation)
        return;
    // Another project's issues are all new, whatever their ids.
    if (snapshot.session != m_known_session)
        m_known.clear();
    m_known_session    = snapshot.session;
    m_known_generation = snapshot.generation;
    std::set<std::string> ids;
    bool arrived = false;
    for (const PrintIssue& issue : snapshot.issues) {
        arrived = arrived || m_known.count(issue.id) == 0;
        ids.insert(issue.id);
    }
    m_known = std::move(ids);
    if (arrived) {
        m_announced = std::chrono::steady_clock::now();
        ++m_announcements;
    }
}

bool IssueOverlay::on_escape()
{
    if (m_list_open) {
        set_list_open(false);
        return true;
    }
    // A bubble that follows the selection closes with it, and Escape is the
    // canvas's own "select nothing". Any other bubble is closed here.
    if (!m_selected.empty() && !m_bound) {
        close_bubble();
        return true;
    }
    return false;
}

void IssueOverlay::on_pointer_down(float x, float y)
{
    if (m_list_open && !contains(m_frame.list, x, y) && !contains(m_frame.pill, x, y))
        set_list_open(false);
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
                      ImGui::GetTextLineHeight(), std::round(m.space_2 * scale), std::round(m.button.icon.width * scale),
                      int(std::lround(m.button.icon.icon_size * scale))};

    const PrintIssue* open = m_selected.empty() ? nullptr : snapshot.find(m_selected);
    if (open != nullptr && open->object != 0) {
        if (selection_holds(*open))
            m_bound = true;
        else if (m_bound)
            open = nullptr; // the person let go of the object
    }
    if (open == nullptr)
        m_selected.clear(); // closed, or the native source stopped reporting it
    // A list of one is never opened; one that shrank to one closes.
    if (snapshot.issues.size() < 2)
        m_list_open = false;

    watch_for_arrivals(snapshot);
    // No issue, no pill: nothing to say is said by saying nothing.
    const bool pill_shown = !snapshot.issues.empty();
    if (pill_shown)
        m_frame.pill_text = status_label(snapshot, tally(snapshot));
    m_frame.pill_opens_list = snapshot.issues.size() > 1;

    const float pill_w     = !pill_shown ? 0.f :
                             style.pad + style.dot + style.gap + ImGui::CalcTextSize(m_frame.pill_text.c_str()).x +
                                 (m_frame.pill_opens_list ? style.gap + float(style.icon) : 0.f) + style.pad;
    const float row_height = std::round(m.menu_row.height * scale) + style.line; // two lines of text
    const float list_height = m_list_open ? float(snapshot.issues.size()) * row_height + 2 * std::round(m.popover.padding_y * scale) : 0.f;
    StatusLayout layout = layout_status(style.canvas_w, style.canvas_h, style.inset, style.gap, pill_w,
                                        pill_shown ? std::round(m.chip.height * scale) : 0.f,
                                        std::round(m.print_issues.list_width * scale), list_height);

    if (pill_shown)
        draw_pill(style, layout, snapshot);
    if (m_list_open)
        draw_list(style, layout, snapshot, row_height);
    else
        draw_notices(style, layout, snapshot); // a docked bubble then starts above them
    if (open != nullptr)
        draw_bubble(style, layout, snapshot, *open);
}

// What OrcaSlicer holds that is not a print problem: the outcome of an action,
// or work still running. Each is OrcaSlicer's own words in a panel above the
// pill, there for as long as OrcaSlicer holds the notification, with a close
// button that closes it the way its own did.
void IssueOverlay::draw_notices(const Style& s, StatusLayout& layout, const PrintIssueSnapshot& snapshot)
{
    // Work that is still running is shown in the chat, where it has its bar
    // and its Cancel; here are the outcomes only.
    std::vector<Notice> results;
    for (const Notice& notice : snapshot.notices)
        if (notice.route == NoticeRoute::Result)
            results.push_back(notice);
    // The newest few; an older one returns when a newer one goes.
    constexpr std::size_t kShown = 3;
    constexpr int         kLines = 4;
    const std::size_t first = results.size() > kShown ? results.size() - kShown : 0;
    const float       width = std::round(s.m.print_issues.list_width * s.scale);
    const float       wrap  = width - 2 * s.pad - s.dot - s.gap - s.line - s.gap;
    std::vector<std::string> texts;
    std::vector<float>       heights;
    for (std::size_t index = first; index < results.size(); ++index) {
        texts.push_back(trimmed(results[index].text));
        const float text_h = std::min(ImGui::CalcTextSize(texts.back().c_str(), nullptr, false, wrap).y, kLines * s.line);
        heights.push_back(s.pad + text_h + s.pad);
    }
    const std::vector<OverlayRect> rects = stack_notices(layout, s.inset, s.gap, width, heights);

    for (std::size_t slot = 0; slot < rects.size(); ++slot) {
        const OverlayRect& rect = rects[slot];
        if (rect.empty())
            continue;
        const Notice& notice = results[first + slot];
        const wxColour& dot = notice.level == NoticeLevel::Error ? s.p.status_danger :
                              notice.level == NoticeLevel::Warning ? s.p.status_warning :
                              notice.route == NoticeRoute::Progress ? s.p.text_secondary : s.p.status_success;
        const OverlayRect close{rect.x + rect.w - s.pad - s.line, rect.y + s.pad, s.line, s.line};
        m_frame.notices.push_back({notice.kind, rect, close});

        begin_bare(("##jusprin_notice_" + std::to_string(slot)).c_str(), rect);
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        panel(draw, rect, s.p, std::round(s.m.radius_container * s.scale), s.p.surface_raised);
        draw.AddCircleFilled(ImVec2(rect.x + s.pad + s.dot / 2, rect.y + s.pad + s.line / 2), s.dot / 2, im_color(dot));
        const ImVec2 text_at(rect.x + s.pad + s.dot + s.gap, rect.y + s.pad);
        draw.PushClipRect(text_at, ImVec2(text_at.x + wrap, rect.y + rect.h - s.pad), true);
        draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_at, im_color(s.p.text_primary), texts[slot].c_str(), nullptr, wrap);
        draw.PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(close.x, close.y));
        if (ImGui::InvisibleButton("##close", ImVec2(close.w, close.h)))
            after_frame([kind = notice.kind](IssueOverlay& self) { self.m_monitor.dismiss(kind); });
        const char*  cross = "\xC3\x97";
        const ImVec2 size  = ImGui::CalcTextSize(cross);
        draw.AddText(ImVec2(close.x + std::round((close.w - size.x) / 2), close.y + std::round((close.h - size.y) / 2)),
                     im_color(ImGui::IsItemHovered() ? s.p.text_primary : s.p.text_secondary), cross);
        ImGui::End();
    }
}

void IssueOverlay::draw_pill(const Style& s, const StatusLayout& layout, const PrintIssueSnapshot& snapshot)
{
    const OverlayRect& pill = layout.pill;
    m_frame.pill = pill;

    const Tally t = tally(snapshot);
    const wxColour& dot = !snapshot.checked || (t.current == 0 && !snapshot.issues.empty()) ? s.p.text_secondary :
                          snapshot.issues.empty() ? s.p.status_success : t.blockers != 0 ? s.p.status_danger : s.p.status_warning;
    // The pill reads as held down while what it opened is open.
    const bool opened = m_list_open || (!m_frame.pill_opens_list && !m_selected.empty());

    begin_bare("##jusprin_issue_pill", pill);
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(pill.x, pill.y));
    if (ImGui::InvisibleButton("##pill", ImVec2(pill.w, pill.h)))
        after_frame([](IssueOverlay& self) { self.press_pill(); });
    const bool  hovered = ImGui::IsItemHovered();
    const float radius  = std::round(s.m.chip.radius * s.scale);
    if (m_announced) {
        const float turn = std::chrono::duration<float>(std::chrono::steady_clock::now() - *m_announced) / kAnnouncement * kAnnouncementRings;
        if (turn < float(kAnnouncementRings)) {
            // Each ring leaves the pill's edge quickly, slows, and fades as it goes.
            const float phase  = turn - std::floor(turn);
            const float spread = (1 - (1 - phase) * (1 - phase)) * s.gap;
            const ImU32 ring   = IM_COL32(dot.Red(), dot.Green(), dot.Blue(), int(std::lround(float(dot.Alpha()) * (1 - phase))));
            draw.PushClipRectFullScreen();
            draw.AddRect(ImVec2(pill.x - spread, pill.y - spread), ImVec2(pill.x + pill.w + spread, pill.y + pill.h + spread), ring,
                         radius + spread, 0, s.m.focus.width * s.scale);
            draw.PopClipRect();
            m_frame.attention = true;
            m_canvas.request_extra_frame(); // until the ring has run
        } else {
            m_announced.reset();
        }
    }
    panel(draw, pill, s.p, radius, opened ? s.p.surface_selected : hovered ? s.p.surface_subtle : s.p.surface_raised);
    draw.AddCircleFilled(ImVec2(pill.x + s.pad + s.dot / 2, pill.y + pill.h / 2), s.dot / 2, im_color(dot));
    const ImVec2 size = ImGui::CalcTextSize(m_frame.pill_text.c_str());
    draw.AddText(ImVec2(pill.x + s.pad + s.dot + s.gap, pill.y + std::round((pill.h - size.y) / 2)), im_color(s.p.text_primary),
                 m_frame.pill_text.c_str());
    if (m_frame.pill_opens_list)
        // The list opens upward.
        icon(draw, m_list_open ? "chevron-down" : "chevron-up", s.icon, pill.x + pill.w - s.pad - float(s.icon),
             pill.y + std::round((pill.h - float(s.icon)) / 2), s.p.text_secondary);
    ImGui::End();
}

void IssueOverlay::draw_list(const Style& s, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, float row_height)
{
    const OverlayRect& list = layout.list;
    m_frame.list_open = true;
    m_frame.list      = list;

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

    // The ones that count first; an ignored warning stays within reach below.
    std::vector<const PrintIssue*> rows;
    for (const bool ignored : {false, true})
        for (const PrintIssue& issue : snapshot.issues)
            if (issue.ignored == ignored)
                rows.push_back(&issue);

    ImDrawList& draw = *ImGui::GetWindowDrawList();
    int index = 0;
    for (const PrintIssue* row : rows) {
        const PrintIssue& issue = *row;
        ImGui::PushID(index++);
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(ImGui::GetContentRegionAvail().x, row_height));
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        m_frame.rows.emplace_back(issue.id, OverlayRect{min.x, min.y, max.x - min.x, max.y - min.y});
        if (ImGui::IsItemHovered())
            draw.AddRectFilled(min, max, im_color(s.p.surface_selected), std::round(s.m.menu_row.radius * s.scale));
        draw.AddCircleFilled(ImVec2(min.x + s.pad / 2 + s.dot / 2, (min.y + max.y) / 2), s.dot / 2, im_color(tone(issue, s.p)));
        const float text_x = min.x + s.pad / 2 + s.dot + s.gap;
        const float text_y = min.y + std::round((row_height - 2 * s.line) / 2);
        const std::string where = (issue.object_name.empty() ? plate_label() : issue.object_name) +
                                  (issue.ignored ? " \xC2\xB7 " + ignored_label() : std::string()) +
                                  (issue.stale ? " \xC2\xB7 " + checking_label() : std::string());
        draw.PushClipRect(ImVec2(text_x, min.y), ImVec2(max.x - s.gap, max.y), true);
        draw.AddText(ImVec2(text_x, text_y), im_color(issue.ignored ? s.p.text_secondary : s.p.text_primary), first_line(issue.message).c_str());
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
    // The pager walks every issue that counts, in the list's order, so the
    // list need not be opened again to reach the next one. An ignored warning
    // is not on that walk.
    std::vector<const PrintIssue*> walk;
    if (!issue.ignored)
        for (const PrintIssue& other : snapshot.issues)
            if (!other.ignored)
                walk.push_back(&other);
    const int  walk_index = int(std::find(walk.begin(), walk.end(), &issue) - walk.begin());
    const bool paged      = walk.size() > 1;
    const bool ignorable  = issue.severity == IssueSeverity::Warning;

    const std::optional<Target> target = target_of(issue);
    const bool in_view = target && target->anchor_x >= 0 && target->anchor_x <= s.canvas_w && target->anchor_y >= 0 &&
                         target->anchor_y <= s.canvas_h;
    const bool busy    = m_actions.chat_busy && m_actions.chat_busy();
    const bool enabled = snapshot.checked && !issue.stale && !busy;

    const std::string severity = issue.stale ? checking_label() : issue.ignored ? ignored_label() :
                                 issue.severity == IssueSeverity::Blocker ? _u8L("Blocks printing") : _u8L("Warning");
    const std::string where    = issue.object == 0 ? plate_label() :
                                 issue.object_name + (target && !in_view ? " \xC2\xB7 " + _u8L("out of view") : std::string());
    const std::string message  = trimmed(issue.message);
    const std::string pager    = paged ? format(_u8L("%1% of %2%"), walk_index + 1, walk.size()) : std::string();
    const std::string note     = busy ? _u8L("The assistant is replying...") : std::string();
    const std::string ignore   = !ignorable ? std::string() : issue.ignored ? _u8L("Stop ignoring") : _u8L("Ignore this warning");

    const float width = std::round(s.m.print_issues.bubble_width * s.scale);
    const float wrap  = width - 2 * s.pad;
    const ImVec2 message_size = ImGui::CalcTextSize(message.c_str(), nullptr, false, wrap);
    const ButtonRecipe& ask_recipe     = s.m.button.secondary;
    const ButtonRecipe& resolve_recipe = s.m.button.primary;
    // Both recipes size a button by padding around its label; the taller one
    // sets the row so the pair lines up.
    const float button_h = std::round(s.line + 2 * std::max(ask_recipe.padding_y, resolve_recipe.padding_y) * s.scale);
    // Under the buttons: the lesser action on the left, the pager on the right.
    const float footer_h = paged ? s.icon_button : ignorable ? s.line : 0.f;

    BubbleRequest request;
    request.canvas_w    = s.canvas_w;
    request.canvas_h    = s.canvas_h;
    request.inset       = s.inset;
    request.leader      = std::round(s.m.space_4 * s.scale);
    request.pad         = s.pad;
    request.bubble_w    = width;
    request.bubble_h    = s.pad + s.line + s.gap + message_size.y + (note.empty() ? 0 : s.gap + s.line) + s.pad + button_h +
                          (footer_h > 0 ? s.gap + footer_h : 0) + s.pad;
    request.has_target  = in_view;
    request.dock_right  = layout.dock_right;
    request.dock_bottom = layout.dock_bottom;
    request.taken       = {layout.pill};
    for (const IssueOverlayFrame::NoticeFrame& notice : m_frame.notices)
        request.taken.push_back(notice.rect);
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
    m_frame.pager_index     = walk_index;
    m_frame.pager_size      = int(walk.size());
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
    panel(draw, bubble, s.p, std::round(s.m.radius_container * s.scale), s.p.surface_raised);

    const float icon_radius = std::round(s.m.button.icon.radius * s.scale);
    // An icon button is larger than its icon. It is placed so the icon, not
    // the button, meets the bubble's padding.
    const float overhang = std::round((s.icon_button - float(s.icon)) / 2);
    const float right    = bubble.x + bubble.w - s.pad + overhang;

    float y = bubble.y + s.pad;
    const OverlayRect close{right - s.icon_button, y + std::round((s.line - s.icon_button) / 2), s.icon_button, s.icon_button};
    m_frame.close = close;
    if (icon_button(draw, "##close", close, "close", s.icon, icon_radius, s.p))
        after_frame([](IssueOverlay& self) { self.close_bubble(); });
    draw.AddCircleFilled(ImVec2(bubble.x + s.pad + s.dot / 2, y + s.line / 2), s.dot / 2, im_color(tone(issue, s.p)));
    draw.AddText(ImVec2(bubble.x + s.pad + s.dot + s.gap, y), im_color(s.p.text_primary), severity.c_str());
    const ImVec2 severity_size = ImGui::CalcTextSize(severity.c_str());
    draw.PushClipRect(ImVec2(bubble.x, bubble.y), ImVec2(close.x, bubble.y + bubble.h), true);
    draw.AddText(ImVec2(bubble.x + s.pad + s.dot + s.gap + severity_size.x + s.gap, y), im_color(s.p.text_secondary), where.c_str());
    draw.PopClipRect();
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
    // A press that reached the chat closes the bubble: the answer is there.
    const auto submit = [this, id = issue.id](std::string request) {
        after_frame([id, request](IssueOverlay& self) {
            if (self.m_actions.submit && self.m_actions.submit(id, request) && self.m_selected == id)
                self.close_bubble();
        });
    };
    if (button(draw, "##ask", ask, _u8L("Ask AI"), std::round(ask_recipe.radius * s.scale), enabled, s.p.action_secondary,
               s.p.action_secondary_hover, s.p.action_secondary_text, &s.p.action_secondary_border, s.p))
        submit(_u8L("Explain this issue and my options."));
    if (button(draw, "##resolve", resolve, _u8L("Resolve with AI"), std::round(resolve_recipe.radius * s.scale), enabled, s.p.action_primary,
               s.p.action_primary_hover, s.p.text_on_action, nullptr, s.p))
        submit(_u8L("Help fix this issue."));
    y += button_h + s.gap;

    if (!ignore.empty()) {
        const OverlayRect rect{bubble.x + s.pad, y, ImGui::CalcTextSize(ignore.c_str()).x, footer_h};
        m_frame.ignore = rect;
        if (link(draw, "##ignore", rect, ignore, s.p))
            after_frame([id = issue.id, ignored = !issue.ignored](IssueOverlay& self) {
                self.m_monitor.set_ignored(id, ignored);
                // Ignored is dealt with; shown again is something to read.
                if (ignored && self.m_selected == id)
                    self.close_bubble();
            });
    }
    if (paged) {
        const ImVec2      pager_size = ImGui::CalcTextSize(pager.c_str());
        const OverlayRect next{right - s.icon_button, y, s.icon_button, s.icon_button};
        const float       text_x = next.x - pager_size.x;
        const OverlayRect previous{text_x - s.icon_button, y, s.icon_button, s.icon_button};
        m_frame.previous = previous;
        m_frame.next     = next;
        draw.AddText(ImVec2(text_x, y + std::round((footer_h - pager_size.y) / 2)), im_color(s.p.text_secondary), pager.c_str());
        const auto turn = [this, &walk, walk_index](int by) {
            const int count = int(walk.size());
            after_frame([id = walk[(walk_index + by + count) % count]->id](IssueOverlay& self) { self.select(id); });
        };
        if (icon_button(draw, "##previous", previous, "chevron-left", s.icon, icon_radius, s.p))
            turn(-1);
        if (icon_button(draw, "##next", next, "chevron-right", s.icon, icon_radius, s.p))
            turn(1);
    }
    draw.PopClipRect();
    ImGui::End();
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
