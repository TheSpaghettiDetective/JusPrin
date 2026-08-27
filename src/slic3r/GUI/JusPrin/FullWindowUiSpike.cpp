#include "FullWindowUiSpike.hpp"

#include "../GLToolbar.hpp"
#include "../Plater.hpp"
#include "GLCanvas3DWrapper.hpp"
#include "Workspace/OrcaWorkspaceAdapter.hpp"

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>

namespace Slic3r::GUI::JusPrin {

namespace {

using Workspace::CommandResult;
using Workspace::OrcaWorkspaceAdapter;
using Workspace::WorkspaceChangeReasons;
using Workspace::WorkspaceSnapshot;
using Workspace::WorkspaceSubscription;

const wxColour shell_background(246, 246, 243);
const wxColour border_colour(221, 221, 216);
const wxColour text_primary(34, 34, 32);
const wxColour text_secondary(102, 102, 98);
const wxColour accent_colour(25, 113, 194);

std::size_t object_count(const WorkspaceSnapshot& snapshot)
{
    return std::accumulate(snapshot.plates.begin(), snapshot.plates.end(), std::size_t{0},
                           [](std::size_t count, const Workspace::WorkspacePlate& plate) {
                               return count + plate.objects.size();
                           });
}

class FullWindowUiSpike final : public wxPanel
{
public:
    FullWindowUiSpike(wxWindow* parent, Plater& plater)
        : wxPanel(parent, wxID_ANY)
        , m_plater(plater)
        , m_workspace(std::make_unique<OrcaWorkspaceAdapter>(plater))
        , m_prepare_canvas(*plater.get_view3D_canvas3D())
        , m_preview_canvas(*plater.get_preview_canvas3D())
    {
        SetName("JusPrin Full Window UI Spike");
        SetBackgroundColour(shell_background);

        if (const char* path = std::getenv("JUSPRIN_FULL_WINDOW_UI_SPIKE_LOG"))
            m_log.open(path, std::ios::out | std::ios::trunc);

        build_layout();

        m_plater.Reparent(m_center_host);
        m_plater.enable_sidebar(false);
        m_center_sizer->Add(&m_plater, 1, wxEXPAND);
        m_plater.Show();

        m_subscription = m_workspace->subscribe([this](const Workspace::WorkspaceChanged& change) {
            refresh_workspace("callback", change.reasons);
        });
        refresh_workspace("initial", WorkspaceChangeReasons::None);
        log("shell installed in existing MainFrame");
    }

    ~FullWindowUiSpike() override
    {
        m_subscription.reset();
    }

private:
    wxButton* add_nav_button(wxWindow* parent, wxSizer* sizer, const wxString& label, bool enabled = true)
    {
        auto* button = new wxButton(parent, wxID_ANY, label, wxDefaultPosition,
                                    wxSize(parent->FromDIP(96), parent->FromDIP(34)), wxBORDER_NONE);
        button->SetBackgroundColour(*wxWHITE);
        button->SetForegroundColour(enabled ? text_primary : text_secondary);
        button->Enable(enabled);
        sizer->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(6));
        return button;
    }

    void build_layout()
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* top = new wxPanel(this, wxID_ANY);
        top->SetName("JusPrin Top Navigation");
        top->SetBackgroundColour(*wxWHITE);
        top->SetMinSize(wxSize(-1, FromDIP(54)));

        auto* top_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* brand = new wxStaticText(top, wxID_ANY, "JUSPRIN");
        wxFont brand_font = brand->GetFont();
        brand_font.SetWeight(wxFONTWEIGHT_BOLD);
        brand_font.SetPointSize(brand_font.GetPointSize() + 2);
        brand->SetFont(brand_font);
        brand->SetForegroundColour(text_primary);
        top_sizer->Add(brand, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(18));

        add_nav_button(top, top_sizer, "Home", false);
        auto* prepare = add_nav_button(top, top_sizer, "ORCA");
        top_sizer->AddStretchSpacer();
        auto* undo = add_nav_button(top, top_sizer, "Undo");
        auto* redo = add_nav_button(top, top_sizer, "Redo");
        auto* slice = add_nav_button(top, top_sizer, "Slice");
        auto* check_print = add_nav_button(top, top_sizer, "Check Print");
        add_nav_button(top, top_sizer, "Print", false);
        auto* printer = new wxStaticText(top, wxID_ANY, "X1 Carbon  •  Ready");
        printer->SetForegroundColour(text_secondary);
        top_sizer->Add(printer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(18));
        top->SetSizer(top_sizer);
        root->Add(top, 0, wxEXPAND);

        auto* divider = new wxPanel(this, wxID_ANY);
        divider->SetBackgroundColour(border_colour);
        divider->SetMinSize(wxSize(-1, 1));
        root->Add(divider, 0, wxEXPAND);

        auto* body = new wxBoxSizer(wxHORIZONTAL);
        auto* objects_rail = new wxPanel(this, wxID_ANY);
        objects_rail->SetName("JusPrin Objects Rail");
        objects_rail->SetBackgroundColour(wxColour(250, 250, 248));
        objects_rail->SetMinSize(wxSize(FromDIP(66), -1));
        auto* rail_sizer = new wxBoxSizer(wxVERTICAL);
        m_objects_label = new wxStaticText(objects_rail, wxID_ANY, "OBJECTS\n0", wxDefaultPosition,
                                           wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        m_objects_label->SetForegroundColour(text_secondary);
        rail_sizer->Add(m_objects_label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(20));
        rail_sizer->AddStretchSpacer();
        auto* move = new wxButton(objects_rail, wxID_ANY, "Move", wxDefaultPosition,
                                  wxSize(FromDIP(56), FromDIP(34)), wxBORDER_NONE);
        move->SetName("JusPrin Move");
        move->SetBackgroundColour(*wxWHITE);
        move->SetForegroundColour(accent_colour);
        rail_sizer->Add(move, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(18));
        auto* rotate = new wxButton(objects_rail, wxID_ANY, "Rotate", wxDefaultPosition,
                                    wxSize(FromDIP(56), FromDIP(34)), wxBORDER_NONE);
        rotate->SetName("JusPrin Rotate");
        rotate->SetBackgroundColour(*wxWHITE);
        rotate->SetForegroundColour(accent_colour);
        rail_sizer->Add(rotate, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(12));
        objects_rail->SetSizer(rail_sizer);
        body->Add(objects_rail, 0, wxEXPAND);

        m_center_host = new wxPanel(this, wxID_ANY);
        m_center_host->SetName("JusPrin Viewport Host");
        m_center_host->SetBackgroundColour(shell_background);
        m_center_sizer = new wxBoxSizer(wxVERTICAL);
        m_center_host->SetSizer(m_center_sizer);
        body->Add(m_center_host, 1, wxEXPAND);

        auto* right = new wxPanel(this, wxID_ANY);
        right->SetName("JusPrin Agent Pane");
        right->SetBackgroundColour(*wxWHITE);
        right->SetMinSize(wxSize(FromDIP(330), -1));
        auto* right_sizer = new wxBoxSizer(wxVERTICAL);

        auto* print_header = new wxStaticText(right, wxID_ANY, "First print                         +");
        wxFont header_font = print_header->GetFont();
        header_font.SetWeight(wxFONTWEIGHT_BOLD);
        print_header->SetFont(header_font);
        print_header->SetForegroundColour(text_primary);
        right_sizer->Add(print_header, 0, wxEXPAND | wxALL, FromDIP(18));

        auto* setup_card = new wxPanel(right, wxID_ANY);
        setup_card->SetBackgroundColour(shell_background);
        auto* setup_sizer = new wxBoxSizer(wxVERTICAL);
        auto* setup_title = new wxStaticText(setup_card, wxID_ANY, "CURRENT SETUP");
        setup_title->SetForegroundColour(text_secondary);
        setup_sizer->Add(setup_title, 0, wxALL, FromDIP(12));
        auto* setup_value = new wxStaticText(setup_card, wxID_ANY,
                                             "Bambu Lab X1 Carbon\n0.4 mm nozzle  •  Generic PLA");
        setup_value->SetForegroundColour(text_primary);
        setup_sizer->Add(setup_value, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        setup_card->SetSizer(setup_sizer);
        right_sizer->Add(setup_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

        auto* transcript = new wxTextCtrl(right, wxID_ANY,
            "You\nHelp me prepare this model for a reliable first print.\n\n"
            "JusPrin\nYour model is ready. Select it to inspect placement, then Slice to check the toolpath.",
            wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        transcript->SetName("JusPrin Static Transcript");
        transcript->SetBackgroundColour(*wxWHITE);
        transcript->SetForegroundColour(text_primary);
        right_sizer->Add(transcript, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

        m_status = new wxStaticText(right, wxID_ANY, "Project ready");
        m_status->SetForegroundColour(text_secondary);
        right_sizer->Add(m_status, 0, wxEXPAND | wxALL, FromDIP(14));

        auto* input = new wxTextCtrl(right, wxID_ANY, "", wxDefaultPosition, wxSize(-1, FromDIP(42)),
                                     wxTE_PROCESS_ENTER);
        input->SetName("JusPrin Chat Input");
        input->SetHint("Ask JusPrin about this print...");
        right_sizer->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
        right->SetSizer(right_sizer);
        body->Add(right, 0, wxEXPAND);

        root->Add(body, 1, wxEXPAND);
        SetSizer(root);

        prepare->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_plater.select_view_3D("3D");
            refresh_workspace("prepare", WorkspaceChangeReasons::None);
            log("command prepare");
        });
        slice->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_plater.exit_gizmo();
            m_plater.update(true, true);
            wxPostEvent(&m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
            CallAfter([this] { refresh_workspace("slice", WorkspaceChangeReasons::None); });
            log("command slice_plate posted");
        });
        check_print->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_plater.select_view_3D("Preview", false);
            refresh_workspace("preview", WorkspaceChangeReasons::None);
            log("command preview");
        });
        move->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const bool opened = m_prepare_canvas.activate_move_gizmo();
            refresh_workspace("move", WorkspaceChangeReasons::None);
            log(std::string("command move ") + (opened ? "active" : "unavailable"));
        });
        rotate->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const bool opened = m_prepare_canvas.activate_rotate_gizmo();
            refresh_workspace("rotate", WorkspaceChangeReasons::None);
            log(std::string("command rotate ") + (opened ? "active" : "unavailable"));
        });
        undo->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { log_command("undo", m_workspace->undo()); });
        redo->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { log_command("redo", m_workspace->redo()); });
    }

    void refresh_workspace(const char* source, WorkspaceChangeReasons reasons)
    {
        const WorkspaceSnapshot snapshot = m_workspace->snapshot();
        const std::size_t count = object_count(snapshot);
        m_objects_label->SetLabel(wxString::Format("OBJECTS\n%zu", count));

        wxString status = wxString::Format("%zu object%s", count, count == 1 ? "" : "s");
        if (!snapshot.selected_objects.empty())
            status += wxString::Format("  •  selected %llu",
                                       static_cast<unsigned long long>(snapshot.selected_objects.front().value()));
        status += m_plater.is_preview_shown() ? "  •  Preview" : "  •  Prepare";
        m_status->SetLabel(status);

        log(std::string("snapshot source=") + source +
            " revision=" + std::to_string(snapshot.revision) +
            " reasons=" + std::to_string(static_cast<unsigned int>(reasons)) +
            " objects=" + std::to_string(count) +
            " selected=" + (snapshot.selected_objects.empty() ? std::string("none") :
                std::to_string(snapshot.selected_objects.front().value())) +
            " view=" + (m_plater.is_preview_shown() ? "Preview" : "Prepare"));
    }

    void log_command(const char* name, const CommandResult& result)
    {
        log(std::string("command ") + name + (result.succeeded() ? " success" : " failure: ") + result.message);
        refresh_workspace(name, WorkspaceChangeReasons::None);
    }

    void log(const std::string& message)
    {
        if (m_log)
            m_log << message << '\n' << std::flush;
    }

    Plater& m_plater;
    std::unique_ptr<OrcaWorkspaceAdapter> m_workspace;
    GLCanvas3DWrapper m_prepare_canvas;
    GLCanvas3DWrapper m_preview_canvas;
    WorkspaceSubscription m_subscription;
    wxPanel* m_center_host{nullptr};
    wxSizer* m_center_sizer{nullptr};
    wxStaticText* m_objects_label{nullptr};
    wxStaticText* m_status{nullptr};
    std::ofstream m_log;
};

} // namespace

wxWindow* create_full_window_ui_spike(wxWindow* parent, Plater& plater)
{
    return new FullWindowUiSpike(parent, plater);
}

} // namespace Slic3r::GUI::JusPrin
