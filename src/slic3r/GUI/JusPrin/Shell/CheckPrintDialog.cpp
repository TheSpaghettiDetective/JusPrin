#include "CheckPrintDialog.hpp"
#include "CheckPrintReportPanel.hpp"
#include "HeaderControls.hpp"
#include "SetupCommands.hpp"
#include "ShellRecipes.hpp"

#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GCodeViewer.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Preview.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/IMSlider.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <wx/dcbuffer.h>
#include <wx/glcanvas.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

// Two sizes only this window has: its header row, and the report beside the
// preview. Everything else in it comes from the token file.
constexpr int kHeaderHeightDip = 48;
constexpr int kReportWidthDip  = 360;
// The smallest window in which the preview's two sliders, their value boxes
// and the report still sit apart: a 540 x 552 DIP preview beside the report.
constexpr int kMinClientWidthDip  = 900;
constexpr int kMinClientHeightDip = 600;

const wxString& middle_dot() { static const wxString s = wxString::FromUTF8(" \xC2\xB7 "); return s; }

ImU32 im_color(const wxColour& color) { return IM_COL32(color.Red(), color.Green(), color.Blue(), color.Alpha()); }

const ModelObject* find_object(const Model& model, const CheckRow& row)
{
    for (const ModelObject* object : model.objects)
        if (row.object_id && object->id().id == row.object_id->value())
            return object;
    for (const ModelObject* object : model.objects)
        if (!row.object.empty() && object->name == row.object)
            return object;
    return nullptr;
}

} // namespace

CheckPrintDialog::CheckPrintDialog(wxWindow* parent, const ShellTheme& theme, Plater& plater, Workspace::IWorkspace& workspace,
                                   wxString project_name, std::vector<Workspace::RegionRecord> regions,
                                   std::function<bool()> is_current)
    : wxDialog(parent, wxID_ANY, _L("Check print"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_theme(theme)
    , m_plater(plater)
    , m_workspace(workspace)
    , m_project_name(std::move(project_name))
    , m_regions(std::move(regions))
    , m_is_current(std::move(is_current))
{
    // The shell harness and its scenarios find the window by this name.
    SetName(_L("Check Print"));
    const bool          dark    = GUI_App::dark_mode();
    const ShellPalette& palette = theme.palette(dark);
    SetBackgroundColour(palette.surface_raised);

    m_preview = m_plater.create_preview(this);
    GLCanvas3D* canvas = m_preview->get_canvas3d();
    if (canvas == nullptr)
        throw std::runtime_error("Check Print could not create the Orca Preview canvas");

    m_report = new CheckPrintReportPanel(this, theme);
    m_report->set_dark(dark);
    m_report->SetMinSize(wxSize(FromDIP(kReportWidthDip), -1));

    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(build_header(palette), 0, wxEXPAND);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(m_preview, 1, wxEXPAND);
    body->Add(m_report, 0, wxEXPAND);
    layout->Add(body, 1, wxEXPAND);
    SetSizer(layout);
    SetMinClientSize(FromDIP(wxSize(kMinClientWidthDip, kMinClientHeightDip)));
    const wxSize workspace_size = m_plater.GetClientSize();
    SetClientSize(wxSize(std::max(workspace_size.x, FromDIP(kMinClientWidthDip)),
                         std::max(workspace_size.y, FromDIP(kMinClientHeightDip))));
    CentreOnParent();

    canvas->bind_event_handlers();
    // The legend starts hidden, every time the window opens.
    m_presentation = std::make_unique<PreviewPresentationController>(*canvas);
    m_presentation->set_legend_shown(false);
    canvas->set_overlay_renderer([this] { on_frame(); });
    // Prepare and this preview share OrcaSlicer's camera. The window opens on
    // the whole slice (once its first frame has the toolpaths) and gives
    // Prepare its own view back when it closes.
    m_camera_before = m_plater.get_camera();
    m_fit_pending   = true;
    // ImGui is one context for every canvas: until the pointer moves over
    // this one, it must not hover where the pointer last was on Prepare's.
    forget_pointer();
    canvas->get_wxglcanvas()->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        event.Skip();
        CallAfter([this] { forget_pointer(); });
    });
    // OrcaSlicer's canvas turns Tab into its own event, which only the
    // Plater's canvases handle.
    canvas->get_wxglcanvas()->Bind(EVT_GLCANVAS_TAB, [this](SimpleEvent&) { move_focus(false); });

    fill_report();
    m_report->on_select([this](const CheckRow& row) { show_finding(row); });

    m_legend->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        m_presentation->set_legend_shown(event.GetInt() != 0);
        m_preview->get_canvas3d()->get_wxglcanvas()->Refresh();
    });
    m_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    m_subscription = m_plater.subscribe_project_state([this](const ProjectStateChanged&) { close_if_stale(); });
    m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &CheckPrintDialog::on_slice_status_changed, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { close_if_stale(); }, m_stale_timer.GetId());
    m_stale_timer.Start(200);
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE)
            EndModal(wxID_CANCEL);
        else if (event.GetKeyCode() == WXK_TAB && !event.ControlDown() && !event.AltDown())
            move_focus(event.ShiftDown());
        else
            event.Skip();
    });
}

std::vector<wxWindow*> CheckPrintDialog::focus_order() const
{
    return {m_preview->get_canvas3d()->get_wxglcanvas(), m_report, m_legend, m_close};
}

wxWindow* CheckPrintDialog::next_focus(const wxWindow* from, bool backward) const
{
    const std::vector<wxWindow*> order = focus_order();
    std::size_t at = order.size();
    for (std::size_t index = 0; index < order.size(); ++index)
        if (order[index] == from)
            at = index;
    const std::size_t next = at == order.size() ? (backward ? order.size() - 1 : 0)
                                                : (at + (backward ? order.size() - 1 : 1)) % order.size();
    return order[next];
}

void CheckPrintDialog::move_focus(bool backward) { next_focus(wxWindow::FindFocus(), backward)->SetFocus(); }

void CheckPrintDialog::forget_pointer()
{
    // ImGui's own "no pointer" position.
    ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    if (GLCanvas3D* canvas = m_preview->get_canvas3d()) {
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }
}

void CheckPrintDialog::on_frame()
{
    GLCanvas3D* canvas = m_preview->get_canvas3d();
    if (m_fit_pending && canvas != nullptr && canvas->get_gcode_viewer().has_data()) {
        m_fit_pending = false;
        canvas->zoom_to_gcode();
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }
    draw_caption();
}

CheckPrintDialog::~CheckPrintDialog()
{
    m_stale_timer.Stop();
    m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &CheckPrintDialog::on_slice_status_changed, this);
    m_subscription.reset();
    if (GLCanvas3D* canvas = m_preview->get_canvas3d()) {
        canvas->set_overlay_renderer(nullptr);
        m_presentation.reset();
        canvas->unbind_event_handlers();
    } else if (m_presentation) {
        m_presentation->abandon();
    }
    if (m_camera_before) {
        m_plater.get_camera() = *m_camera_before;
        m_plater.set_current_canvas_as_dirty();
    }
    // Prepare's canvas takes the pointer back on its next mouse event.
    ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
}

wxWindow* CheckPrintDialog::build_header(const ShellPalette& palette)
{
    const ShellMetrics& m      = m_theme.metrics();
    auto*               header = new wxPanel(this, wxID_ANY);
    header->SetName("Check print header");
    header->SetBackgroundStyle(wxBG_STYLE_PAINT);
    header->SetMinSize(wxSize(-1, FromDIP(kHeaderHeightDip)));
    header->Bind(wxEVT_PAINT, [this, header](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(header);
        const ShellPalette& p    = m_theme.palette(GUI_App::dark_mode());
        const wxSize        size = header->GetClientSize();
        dc.SetBackground(wxBrush(p.surface_raised));
        dc.Clear();
        dc.SetPen(wxPen(p.border_subtle));
        dc.DrawLine(0, size.y - 1, size.x, size.y - 1);
    });

    auto* title = new wxStaticText(header, wxID_ANY, _L("Check print"));
    style_label(*title, m_theme, TextRole::BodyBold, palette.text_primary);
    title->SetBackgroundColour(palette.surface_raised);
    m_context = new wxStaticText(header, wxID_ANY, context_text(), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    style_label(*m_context, m_theme, TextRole::Label, palette.text_secondary);
    m_context->SetBackgroundColour(palette.surface_raised);
    m_legend = new CheckToggle(header, m_theme, _L("Legend"));
    m_legend->set_dark(GUI_App::dark_mode());
    m_close = new HeaderButton(header, m_theme, HeaderStyle::Quiet, wxEmptyString, HeaderIcon::Cancel);
    m_close->SetName(_L("Close"));
    m_close->SetToolTip(_L("Close and return to Prepare"));
    m_close->set_dark(GUI_App::dark_mode());
    m_close->set_surface(HeaderSurface::Raised);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(FromDIP(m.space_4));
    row->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_4));
    row->Add(m_context, 1, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_4));
    row->Add(m_legend, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_4));
    row->Add(m_close, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_2));
    header->SetSizer(row);
    return header;
}

wxString CheckPrintDialog::context_text() const
{
    const CheckPrintContext context = check_print_context(m_workspace.snapshot(), m_project_name.ToUTF8().data());
    wxString                text    = wxString::FromUTF8(context.project);
    if (!context.plate.empty())
        text += (text.empty() ? wxString() : middle_dot()) + wxString::FromUTF8(context.plate);
    if (!context.printer.empty())
        text += (text.empty() ? wxString() : middle_dot()) +
                wxString::Format(_L("sliced for %s"), wxString::FromUTF8(context.printer));
    return text;
}

void CheckPrintDialog::fill_report()
{
    const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
    CheckPrintReportPanel::Context     context;
    context.currency = snapshot.currency;
    // The short names the header's filament chip shows.
    for (const SetupCommands::FilamentSlot& slot : SetupCommands::filament_slots()) {
        if (context.filament_names.size() <= slot.index)
            context.filament_names.resize(slot.index + 1);
        context.filament_names[slot.index] = slot.filament.alias.ToUTF8().data();
    }
    if (!snapshot.active_plate) {
        m_report->set_report({}, std::move(context));
        return;
    }
    Workspace::SliceReportRequest request;
    request.supports    = true;
    request.seams       = true;
    request.first_layer = true;
    request.islands     = true;
    request.regions     = m_regions;
    m_report->set_report(build_check_print_report(m_workspace.slice_report(*snapshot.active_plate, request), m_regions),
                         std::move(context));
}

void CheckPrintDialog::show_finding(const CheckRow& row)
{
    GLCanvas3D* canvas = m_preview->get_canvas3d();
    if (canvas == nullptr)
        return;

    // The camera goes to the object the finding is about, or to the whole
    // print when it is about the plate.
    if (const ModelObject* object = find_object(m_plater.model(), row)) {
        BoundingBoxf3 box;
        for (std::size_t instance = 0; instance < object->instances.size(); ++instance)
            box.merge(object->instance_bounding_box(instance));
        if (box.defined)
            m_plater.get_camera().zoom_to_box(box);
    } else {
        canvas->zoom_to_gcode();
    }

    m_caption = Caption{m_report->row_message(row), 0, 0};
    if (row.z) {
        const std::vector<double> tops = canvas->get_gcode_layers_zs();
        if (const std::optional<std::size_t> index = layer_index_at(tops, *row.z)) {
            IMSlider* layers = canvas->get_gcode_viewer().get_layers_slider();
            IMSlider* moves  = canvas->get_gcode_viewer().get_moves_slider();
            layers->SetHigherValue(int(*index));
            layers->set_as_dirty();
            moves->SetHigherValue(moves->GetMaxValue());
            moves->set_as_dirty();
            m_caption->layer = *index + 1;
            m_caption->z     = tops[*index];
        }
    }
    canvas->set_as_dirty();
    canvas->request_extra_frame();
    canvas->get_wxglcanvas()->Refresh();
}

void CheckPrintDialog::draw_caption()
{
    m_caption_rect.reset();
    if (!m_caption)
        return;
    GLCanvas3D* canvas = m_preview->get_canvas3d();
    if (canvas == nullptr)
        return;
    const ShellMetrics& m     = m_theme.metrics();
    const ShellPalette& p     = m_theme.palette(GUI_App::dark_mode());
    ImGuiWrapper&       imgui = *wxGetApp().imgui();
    const float         scale = imgui.get_style_scaling();
    const Size          size  = canvas->get_canvas_size();

    std::string title = m_caption->title.ToUTF8().data();
    const std::string where = m_caption->layer == 0 ? std::string()
                                                     : wxString::Format(_L("Layer %d"), int(m_caption->layer)).ToUTF8().data() +
                                                           std::string(middle_dot().ToUTF8().data()) +
                                                           wxString::Format(_L("%.1f mm"), m_caption->z).ToUTF8().data();
    const float pad_x = std::round(m.space_2 * scale);
    const float pad_y = std::round(m.space_1 * scale);
    const float gap   = std::round(m.space_half * scale);
    // The top-left corner of the canvas, which nothing else uses once
    // OrcaSlicer's overlays are hidden: the legend and the G-code listing open
    // at the top right.
    const float inset = std::round(m.space_3 * scale);
    // One line each, never wider than half the canvas, and ending before
    // OrcaSlicer's legend when it is shown: in a narrow preview the legend
    // reaches this corner.
    float right = 0.5f * float(size.get_width());
    if (const ImGuiWindow* legend = ImGui::FindWindowByName("Legend"); legend != nullptr && legend->Active)
        right = std::min(right, legend->Pos.x - inset);
    const float max_w = std::max(0.f, right - inset - 2 * pad_x);

    // The title is the bold label; it ends in an ellipsis rather than being cut.
    const bool bold = imgui.push_bold_font();
    if (ImGui::CalcTextSize(title.c_str()).x > max_w) {
        wxString cut = m_caption->title;
        const std::string ellipsis = wxString::FromUTF8("\xE2\x80\xA6").ToUTF8().data();
        while (!cut.empty() && ImGui::CalcTextSize((std::string(cut.ToUTF8().data()) + ellipsis).c_str()).x > max_w)
            cut.RemoveLast();
        title = std::string(cut.ToUTF8().data()) + ellipsis;
    }
    const ImVec2 title_size = ImGui::CalcTextSize(title.c_str());
    if (bold)
        imgui.pop_bold_font();
    const ImVec2 where_size = where.empty() ? ImVec2(0.f, 0.f) : ImGui::CalcTextSize(where.c_str());
    const float  text_w     = std::min(max_w, std::max(title_size.x, where_size.x));
    const float  w          = text_w + 2 * pad_x;
    const float  h          = 2 * pad_y + title_size.y + (where.empty() ? 0.f : gap + where_size.y);
    const float  radius     = std::round(m.radius_small * scale);

    ImGui::SetNextWindowPos(ImVec2(inset, inset));
    ImGui::SetNextWindowSize(ImVec2(w, h + std::round(m.space_1 * scale)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::Begin("##jusprin_check_print_caption", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoInputs);
    ImGui::PopStyleVar(3);
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const ImVec2 min(inset, inset);
    const ImVec2 max(inset + w, inset + h);
    // elevation.subtle, approximated: ImGui has no blur.
    draw.AddRectFilled(ImVec2(min.x, min.y + std::round(2 * scale)), ImVec2(max.x, max.y + std::round(2 * scale)),
                       im_color(p.elevation_subtle), radius);
    draw.AddRectFilled(min, max, im_color(p.surface_raised), radius);
    draw.AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), im_color(p.border_subtle), radius, 0, 1.f);
    draw.PushClipRect(min, max, true);
    const bool bold_title = imgui.push_bold_font();
    draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + pad_x, min.y + pad_y), im_color(p.text_primary), title.c_str());
    if (bold_title)
        imgui.pop_bold_font();
    if (!where.empty())
        draw.AddText(ImVec2(min.x + pad_x, min.y + pad_y + title_size.y + gap), im_color(p.text_secondary), where.c_str());
    draw.PopClipRect();
    ImGui::End();
    m_caption_rect = wxRect(int(min.x), int(min.y), int(w), int(h));
    m_caption_bold = bold_title;
}

bool CheckPrintDialog::legend_shown() const { return m_presentation && m_presentation->legend_shown(); }

void CheckPrintDialog::close_if_stale()
{
    if (IsModal() && !m_is_current())
        EndModal(wxID_CANCEL);
}

void CheckPrintDialog::on_slice_status_changed(wxCommandEvent& event)
{
    close_if_stale();
    event.Skip();
}

} // namespace Slic3r::GUI::JusPrin
