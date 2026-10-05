#include "ShellController.hpp"

#include "AgentPane.hpp"
#include "LeftPane.hpp"
#include "SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterPanel.hpp"
#include "StatusRow.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Home/HomeWebView.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Project.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

// A preset a person could pick, by the name Orca keys it.
bool preset_exists(Preset::Type type, const std::string& name)
{
    const PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return false;
    const PresetCollection& presets = type == Preset::TYPE_FILAMENT ? static_cast<const PresetCollection&>(bundle->filaments) :
                                                                      static_cast<const PresetCollection&>(bundle->printers);
    const Preset* preset = presets.find_preset(name, false);
    return preset != nullptr && preset->name == name && preset->is_visible && !preset->is_default;
}

class PaneResizeHandle final : public wxPanel
{
public:
    enum class PaneSide { Left, Right };

    // What the divider needs from the shell: the pane's width now, a new
    // width while dragging, and optionally the double click that closes it.
    struct Callbacks
    {
        std::function<int()>     width;
        std::function<void(int)> set_width;
        std::function<void()>    toggle;
    };

    PaneResizeHandle(wxWindow* parent, const ShellTheme& theme, PaneSide side,
                     const wxString& name, const wxString& tooltip, Callbacks callbacks)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition,
                  parent->FromDIP(wxSize(theme.metrics().agent_pane.resize_handle_width, -1)),
                  wxBORDER_NONE)
        , m_theme(theme)
        , m_side(side)
        , m_callbacks(std::move(callbacks))
    {
        SetName(name);
        SetToolTip(tooltip);
        SetMinSize(parent->FromDIP(wxSize(m_theme.metrics().agent_pane.resize_handle_width, -1)));
        SetCursor(wxCursor(wxCURSOR_SIZEWE));
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        Bind(wxEVT_PAINT, &PaneResizeHandle::on_paint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hovered = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = false; Refresh(); });
        Bind(wxEVT_LEFT_DOWN, &PaneResizeHandle::on_left_down, this);
        Bind(wxEVT_LEFT_UP, &PaneResizeHandle::on_left_up, this);
        if (m_callbacks.toggle)
            Bind(wxEVT_LEFT_DCLICK, &PaneResizeHandle::on_double_click, this);
        Bind(wxEVT_MOTION, &PaneResizeHandle::on_motion, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { m_dragging = false; Refresh(); });
    }

private:
    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const ShellPalette& palette = m_theme.palette(wxGetApp().dark_mode());
        dc.SetBackground(wxBrush(palette.surface_subtle));
        dc.Clear();

        const wxColour divider = m_dragging ? palette.action_primary :
                                 m_hovered  ? palette.border_strong : palette.border_subtle;
        const int line_width = FromDIP(m_theme.metrics().agent_pane.resize_handle_line_width);
        dc.SetPen(wxPen(divider, line_width));
        const int center = GetClientSize().x / 2;
        dc.DrawLine(center, 0, center, GetClientSize().y);
    }

    void on_left_down(wxMouseEvent& event)
    {
        m_drag_origin_x = ClientToScreen(event.GetPosition()).x;
        m_drag_origin_width = m_callbacks.width();
        m_dragging = true;
        CaptureMouse();
        Refresh();
    }

    void on_left_up(wxMouseEvent&)
    {
        end_drag();
    }

    void on_double_click(wxMouseEvent&)
    {
        // wx sends down/up before the double click, so a drag may be open.
        end_drag();
        m_callbacks.toggle();
    }

    void on_motion(wxMouseEvent& event)
    {
        if (!m_dragging || !HasCapture())
            return;
        const int pointer_x = ClientToScreen(event.GetPosition()).x;
        // Moving a pane's outer edge away from it grows the pane, regardless
        // of which side of the workspace it occupies.
        const int delta = pointer_x - m_drag_origin_x;
        m_callbacks.set_width(m_drag_origin_width + (m_side == PaneSide::Left ? delta : -delta));
    }

    void end_drag()
    {
        if (!m_dragging)
            return;
        m_dragging = false;
        if (HasCapture())
            ReleaseMouse();
        Refresh();
    }

    const ShellTheme& m_theme;
    PaneSide m_side;
    Callbacks m_callbacks;
    bool m_hovered{false};
    bool m_dragging{false};
    int m_drag_origin_x{0};
    int m_drag_origin_width{0};
};

std::unique_ptr<ShellController>& shell_slot()
{
    static std::unique_ptr<ShellController> shell;
    return shell;
}

} // namespace

ShellController::ShellController()
{
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (m_autosave) {
            m_autosave->tick();
            const std::string head = m_autosave->current_version();
            const auto document_revision = m_agent_pane ?
                m_agent_pane->web_view().host().persistence().document().doc_revision() : 0;
            if (!head.empty() && m_autosave->state() == Workspace::ProjectAutosave::State::Saved &&
                (head != m_timeline_version_head || document_revision != m_timeline_document_revision)) {
                if (m_agent_pane && m_agent_pane->web_view().host().checkpoint_active_chat()) {
                    m_timeline_version_head = m_autosave->current_version();
                    m_timeline_document_revision = m_agent_pane->web_view().host().persistence().document().doc_revision();
                    m_agent_pane->web_view().host().refresh_page_state();
                }
            }
            const std::string key = std::to_string(int(m_autosave->state())) + m_autosave->error();
            if (key != m_autosave_status_key && m_status_row != nullptr) {
                m_autosave_status_key = key;
                m_status_row->refresh();
                if (m_autosave->state() == Workspace::ProjectAutosave::State::Saved &&
                    m_home != nullptr && m_home->IsShownOnScreen())
                    m_home->refresh();
            }
        }
        if (m_status_row != nullptr && m_frame != nullptr) {
            const wxString title = m_status_row->project_summary().BeforeFirst('\n');
            if (m_frame->GetTitle() != title) {
                m_frame->SetTitle(title);
                m_frame->update_title_colour_after_set_title();
            }
        }
        if (!m_agent_pane) return;
        auto& host = m_agent_pane->web_view().host();
        host.pump_stream();
        host.pump_tools();
        host.pump_setup();
    }, m_runtime_timer.GetId());
}

ShellController::~ShellController()
{
    m_runtime_timer.Stop();
    if (m_installed)
        uninstall();
}

void ShellController::install(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer)
{
    if (m_installed)
        throw std::runtime_error("the JusPrin shell is already installed");

    Plater* plater = frame.plater();
    if (plater == nullptr)
        throw std::runtime_error("the Plater is not constructed");
    if (tabpanel.FindPage(plater) != MainFrame::tp3DEditor)
        throw std::runtime_error("the Prepare page is not where the shell expects it");
    if (main_sizer.GetItem(&tabpanel) == nullptr)
        throw std::runtime_error("the tab panel is not in the main layout");

    // The design tokens were loaded once, at startup, by the brand palette.
    // Checked before touching any layout so a broken resource bundle leaves
    // the stock presentation untouched.
    m_theme = brand_theme();
    if (m_theme == nullptr)
        throw std::runtime_error("the JusPrin design tokens did not load; the brand palette logged why");

    m_frame     = &frame;
    m_saved_frame_title = frame.GetTitle();
    m_tabpanel  = &tabpanel;
    m_main_sizer = &main_sizer;
    m_plater    = plater;

    // Saved before any mutation so a partial-install rollback restores the
    // real prior state, not a default.
    m_saved_collapse_toolbar_enabled = plater->get_collapse_toolbar().is_enabled();
    m_saved_auto_preview_after_slice = plater->auto_preview_after_slice();
    m_saved_show_config_wizard_on_startup = wxGetApp().show_config_wizard_on_startup;

    try {
        m_workspace = std::make_unique<Workspace::OrcaWorkspaceAdapter>(*plater);

        // Conversation state, stored inside the project's
        // auxiliary directory and mirrored to a per-project local recovery
        // store under the application data dir.
        Agent::ProjectPersistence::Config persistence_config;
        persistence_config.recovery_root = (boost::filesystem::path(data_dir()) / "jusprin" / "recovery").string();
        persistence_config.managed_root = (boost::filesystem::path(data_dir()) / "jusprin" / "projects").string();
        m_persistence = std::make_unique<Agent::ProjectPersistence>(*m_workspace, std::move(persistence_config));

        Agent::AgentRuntime agent = Agent::load_agent_runtime(wxGetApp().app_config);

        m_status_row = new StatusRow(&frame, *m_theme, *plater, tabpanel, *m_persistence);
        m_agent_pane = new AgentPane(&frame, *m_theme, *m_workspace, *m_persistence, agent.availability,
                                     std::move(agent.service), std::move(agent.setup),
                                     (boost::filesystem::path(data_dir()) / "jusprin" / "mcp.json").string());
        m_agent_pane_preferred_width = frame.FromDIP(m_theme->metrics().agent_pane.min_width);
        m_agent_resize_handle = new PaneResizeHandle(
            &frame, *m_theme, PaneResizeHandle::PaneSide::Right,
            _L("Resize Agent panel"), _L("Drag to resize the Agent panel"),
            PaneResizeHandle::Callbacks{
                [this] { return m_agent_pane == nullptr ? 0 : m_agent_pane->GetSize().x; },
                [this](int width) { request_agent_pane_width(width); },
                [this] { toggle_agent_pane(); }});
        m_status_row->set_agent_pane_toggle([this] { toggle_agent_pane(); });
        m_status_row->set_agent_pane_collapsed(m_agent_pane_collapsed);

        // The header posts its after-change line into the thread. Wiring it
        // here rather than giving StatusRow the AgentHost keeps the header free
        // of any Agent dependency, and the weak reference means a torn-down
        // pane simply stops accepting notes.
        m_status_row->set_note_sink([pane = wxWeakRef<AgentPane>(m_agent_pane)](const wxString& text, const wxString& swatch) {
            if (pane) pane->web_view().host().post_note(text.ToUTF8().data(), swatch.ToUTF8().data());
        });
        // A file the person opened or imported: the project conversation's
        // Agent speaks about it, or its card lists what OrcaSlicer said.
        m_workspace->set_load_report_listener([pane = wxWeakRef<AgentPane>(m_agent_pane)](const Workspace::LoadReport& report) {
            if (pane) pane->web_view().host().on_file_loaded(report);
        });

        // Adopt the currently open project once the host has registered its
        // listeners, so the initial document reaches the pane too.
        m_persistence->attach();
        m_autosave = std::make_unique<Workspace::ProjectAutosave>(
            *plater, *m_persistence, *m_workspace,
            std::filesystem::path(data_dir()) / "jusprin" / "projects");
        auto& project_host = m_agent_pane->web_view().host();
        project_host.set_chat_checkpoint_callbacks(
            [this] { return m_autosave->pin_current(); },
            [this] {
                return m_autosave->state() == Workspace::ProjectAutosave::State::Saved
                    ? m_autosave->current_version() : std::string();
            },
            [this](const std::string& id) {
                const auto versions = m_autosave->history();
                return std::any_of(versions.begin(), versions.end(), [&id](const auto& version) {
                    return version.id == id;
                });
            },
            [this] { return m_autosave->save_now(); },
            [this](const std::string& version, const nlohmann::json& planning, const std::string& chat) {
                return m_autosave->restore_chat(version, planning, chat);
            });
        project_host.checkpoint_active_chat();
        project_host.set_restore_points_provider([this] {
            nlohmann::json points = nlohmann::json::array();
            if (!m_autosave)
                return points;
            // Keep the first model saved at a boundary: later model-only saves
            // with the same change sequence must not move its restore target.
            std::map<std::uint64_t, std::string> by_change;
            const auto versions = m_autosave->history();
            const auto current = m_autosave->current_version();
            const auto head = std::find_if(versions.begin(), versions.end(), [&current](const auto& version) {
                return version.id == current;
            });
            const auto changes = m_persistence->document().changes();
            const std::uint64_t latest_change_seq = changes.empty() ? 0 : changes.back().seq;
            // The head needs no revert action while it still describes the
            // latest change; once a newer edit is logged it becomes useful.
            const std::uint64_t current_change_seq = head != versions.end() && head->change_seq == latest_change_seq
                ? head->change_seq : 0;
            for (const auto& version : versions)
                if (version.change_seq != 0 && version.change_seq != current_change_seq)
                    by_change.emplace(version.change_seq, version.id);
            for (const auto& [seq, id] : by_change)
                points.push_back({{"changeSeq", seq}, {"versionId", id}});
            return points;
        });
        project_host.set_page_message_handler([this](const std::string& type, const nlohmann::json& payload) {
            if (type != Agent::Protocol::kShellAction || !payload.is_object())
                return false;
            const std::string action = payload.value("action", std::string());
            if (action == "collapse_agent_pane") {
                // Hiding a wxWebView from inside its own script-message
                // callback can stall its platform message pump. Collapse on
                // the next native turn, after this handler has returned.
                m_frame->CallAfter([this] {
                    if (m_installed && !m_agent_pane_collapsed)
                        toggle_agent_pane();
                });
                return true;
            }
            if (action != "revert_to_here")
                return false;
            const std::string id = payload.value("versionId", std::string());
            const auto versions = m_autosave->history();
            const auto found = std::find_if(versions.begin(), versions.end(), [&id](const auto& version) {
                return version.id == id && version.change_seq != 0;
            });
            if (found == versions.end() || !m_autosave->restore(id)) {
                wxMessageBox(_L("This saved version couldn't be restored. Try again from Version history."),
                             _L("Revert failed"), wxOK | wxICON_ERROR, m_frame);
            }
            m_agent_pane->web_view().host().refresh_page_state();
            return true;
        });
        m_agent_pane->web_view().host().set_turn_boundary_callback([this] {
            m_agent_pane->web_view().host().checkpoint_active_chat();
        });
        Slic3r::set_backup_suspended(true);
        m_status_row->set_autosave(m_autosave.get());
        plater->set_before_project_release([this] {
            if (!m_autosave || !m_autosave->save_now())
                return false;
            m_autosave->ensure_current_preview();
            return true;
        });
        plater->set_external_project_open_handler([this](const Plater::fs_path& path) {
#ifdef _WIN32
            const std::filesystem::path source(path.wstring());
#else
            const std::filesystem::path source(path.string());
#endif
            if (!m_autosave)
                return false;
            const auto matches = m_autosave->projects_for_source(source);
            if (matches.empty())
                return false;
            const bool current = matches.front() == m_persistence->document().project_id();
            const ReimportDecision decision = m_reimport_confirmation ? m_reimport_confirmation() : [this, &matches, current] {
                const wxString message = current ?
                    _L("This file is already open as a project. Continue working in it, or create a new project from this file.") :
                    matches.size() > 1 ?
                    _L("You've opened this file before. Open the most recently updated project or create another project from this file.") :
                    _L("You've opened this file before. Open the existing project to continue where you left off, "
                       "or create a new project from this file.");
                wxMessageDialog dialog(m_frame,
                    message, _L("Previously opened file"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
                dialog.SetYesNoLabels(current ? _L("Continue current project") :
                                      matches.size() > 1 ? _L("Open most recent project") : _L("Open existing project"),
                                      _L("Create new project"));
                const int answer = dialog.ShowModal();
                return answer == wxID_YES ? ReimportDecision::OpenExisting :
                       answer == wxID_NO ? ReimportDecision::CreateNew : ReimportDecision::Cancel;
            }();
            if (decision == ReimportDecision::CreateNew)
                return false;
            if (decision == ReimportDecision::OpenExisting) {
                if (!m_autosave->open_managed_project(matches.front()))
                    wxMessageBox(_L("The project couldn't be opened. Try again."), _L("Couldn't open project"),
                                 wxOK | wxICON_ERROR, m_frame);
                else
                    m_frame->select_tab(size_t(MainFrame::tp3DEditor));
            }
            return true;
        });
        m_agent_pane->web_view().host().tools().set_version_callbacks(
            [this](const Agent::ToolActivity&) {
                const std::string before = m_autosave->pin_current();
                if (before.empty())
                    throw std::runtime_error("Couldn't save the project. The action was not run.");
                return before;
            },
            [this](const Agent::ToolActivity& activity, const std::string& before) {
                const char* outcome = activity.state == Agent::ToolState::Succeeded ? "succeeded" :
                                      activity.state == Agent::ToolState::Cancelled ? "cancelled" : "failed";
                m_autosave->record_agent_operation(activity.action_id, activity.tool, outcome, before);
            });
        m_agent_pane->web_view().host().tools().set_document_boundary_callback(
            [this](const Agent::ToolActivity&) {
                if (!m_autosave->save_now())
                    throw std::runtime_error("The action ran, but its changes couldn't be saved.");
            });

        // One shell-owned pump continues throughout WebView page reloads.
        // The page handshake gates delivery, not native MCP execution.
        m_runtime_timer.Start(33);

        // Home is a fork-owned panel beside the Notebook, not one of its pages.
        // The Notebook keeps every page it had, so upstream keeps the meaning
        // of its indices: the dialog parents that ask for page 0, the
        // predicates that key off GetSelection(), and any future cast of a
        // page all still find the window they expect. The shell owns only
        // which of the two is on screen for tpHome.
        m_home = new Home::HomeWebView(&frame, *m_theme, frame);
        m_home->backend().set_autosave(m_autosave.get());
        m_home->Hide();

        // One temporary task chat serves printer and filament help from any
        // screen. Its in-memory runtime is rebuilt for every opening.
        m_printer_panel = new PrinterSetup::PrinterPanel(
            &frame, *m_theme, GUI_App::dark_mode(), *m_workspace, *plater,
            PrinterSetup::PrinterPanel::Callbacks{
                [this] { restore_task_panel(); },
                [this](const std::string& added) { m_home->refresh(added); },
                [this] { mark_agent_config_possibly_changed(); },
                [](std::size_t slot) { SetupCommands::open_filament_settings(slot); }});
        m_home->backend().set_conversation_opener(
            [this](const std::string& printer_name, bool connect) { open_printer_conversation(printer_name, connect); });

        main_sizer.Detach(&tabpanel);
        m_center_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_project_sizer = new wxBoxSizer(wxVERTICAL);
        m_workspace_sizer = new wxBoxSizer(wxVERTICAL);
        m_workspace_sizer->Add(&tabpanel, 1, wxEXPAND);
        m_workspace_sizer->Add(m_home, 1, wxEXPAND);
        m_left_pane = new LeftPane(&frame, *m_theme, *m_workspace);
        m_left_pane_preferred_width = frame.FromDIP(m_theme->metrics().left_pane.width);
        m_left_resize_handle = new PaneResizeHandle(
            &frame, *m_theme, PaneResizeHandle::PaneSide::Left,
            _L("Resize Plates and Project panel"), _L("Drag to resize the Plates and Project panel"),
            PaneResizeHandle::Callbacks{
                [this] { return m_left_pane == nullptr ? 0 : m_left_pane->GetSize().x; },
                [this](int width) { request_left_pane_width(width); },
                [this] { toggle_left_pane(); }});
        m_left_pane->set_pane_toggle([this] { toggle_left_pane(); });
        m_status_row->set_left_pane_toggle([this] { toggle_left_pane(); });
        m_status_row->set_left_pane_collapsed(m_left_pane_collapsed);
        m_left_pane->set_project_sources({m_persistence.get(), m_autosave.get(),
            [this] {
                // OrcaSlicer's project-information editor lives on its Project page.
                ProjectPanel* project = m_frame->m_project;
                if (project == nullptr)
                    throw std::runtime_error("the Project page is unavailable");
                const int page = m_tabpanel->FindPage(project);
                if (page == wxNOT_FOUND)
                    throw std::runtime_error("the Project page is missing from the Notebook");
                m_frame->select_tab(size_t(page));
                project->show_info_editor(true);
            }});
        m_project_sizer->Add(m_status_row, 0, wxEXPAND);
        m_project_sizer->Add(m_workspace_sizer, 1, wxEXPAND);
        m_center_sizer->Add(m_left_pane, 0, wxEXPAND);
        m_center_sizer->Add(m_left_resize_handle, 0, wxEXPAND);
        m_center_sizer->Add(m_project_sizer, 1, wxEXPAND);
        m_center_sizer->Add(m_agent_resize_handle, 0, wxEXPAND);
        m_center_sizer->Add(m_agent_pane, 0, wxEXPAND);
        m_center_sizer->Add(m_printer_panel, 1, wxEXPAND);
        m_printer_panel->Hide();
        main_sizer.Add(m_center_sizer, 1, wxEXPAND);

        tabpanel.GetBtnsListCtrl()->Hide();
        plater->set_sidebar_available(false);
        plater->set_auto_preview_after_slice(false);

        // The collapse toolbar belongs to the Plater and is shared by the
        // Prepare and Preview canvases, so this controller is its single
        // owner for the shell's lifetime.
        plater->get_collapse_toolbar().set_enabled(false);

        // Hide the whole legacy canvas-overlay layer on the Prepare canvas
        // (main toolbar, gizmo picker, plate controls, navigator, canvas menu)
        // and the per-plate corner icons; the fork's own UI owns the canvas.
        // The active gizmo stays interactive so shell controls can drive it.
        // Add-model remains available through File > Import and drag-drop.
        if (GLCanvas3D* prepare_canvas = plater->get_view3D_canvas3D()) {
            m_prepare_canvas_presentation.attach(*prepare_canvas);
        }

        // The frame may outlive a runtime detach, so use a tracked event sink
        // and disconnect it when uninstalling the controller.
        frame.Bind(wxEVT_DESTROY, &ShellController::on_frame_destroy, this);
        frame.Bind(wxEVT_SIZE, &ShellController::on_frame_size, this);

        // A member function rather than a lambda so uninstall() can take it
        // off again: the Notebook outlives this controller.
        tabpanel.Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ShellController::on_notebook_page_changed, this);

        m_installed = true;
        on_page_changed();
        apply_current_appearance();
        m_status_row->refresh();
        frame.Layout();
        apply_left_pane_width();
        apply_agent_pane_width();
        // Home owns first-run printer setup; keep Orca's wizard available on demand.
        // Shell installation runs during MainFrame construction, before the
        // deferred startup wizard check. Keep this before that check, not in run_wizard.
        wxGetApp().show_config_wizard_on_startup = false;
    } catch (...) {
        m_installed = true; // let uninstall() undo whatever was applied
        uninstall();
        throw;
    }
}

void ShellController::on_frame_destroy(wxWindowDestroyEvent& event)
{
    if (event.GetWindow() == m_frame) {
        m_runtime_timer.Stop();
        m_installed = false;
        wxGetApp().show_config_wizard_on_startup = m_saved_show_config_wizard_on_startup;
        m_prepare_canvas_presentation.abandon();
        // This controller lives in a static slot and outlives every window,
        // so whatever it owns that unbinds from the Plater has to go now,
        // while the frame's children still exist: wx sends this event from
        // the top-level window's destructor, before DestroyChildren. Left in
        // place, the slot's destructor ran ~OrcaWorkspaceAdapter against a
        // deleted Plater after main had returned -- an access violation at
        // process exit in every run that did not detach the shell by hand.
        // Same order as uninstall(): the pane and status row first, since
        // their destructors talk to persistence and the workspace, then
        // those two. The remaining children die with the frame.
        if (m_left_pane != nullptr) {
            m_left_pane->Destroy();
            m_left_pane = nullptr;
        }
        if (m_agent_pane != nullptr) {
            m_agent_pane->web_view().host().tools().set_version_callbacks({}, {});
            m_agent_pane->Destroy();
            m_agent_pane = nullptr;
        }
        // The printer panel holds a host of its own over the same workspace,
        // so it goes with the pane rather than with Home's children later.
        if (m_printer_panel != nullptr) {
            m_printer_panel->Destroy();
            m_printer_panel = nullptr;
        }
        if (m_status_row != nullptr) {
            m_status_row->Destroy();
            m_status_row = nullptr;
        }
        if (m_home != nullptr) {
            m_home->set_live(false);
            m_home->backend().set_autosave(nullptr);
        }
        m_plater->set_before_project_release({});
        m_plater->set_external_project_open_handler({});
        m_autosave.reset();
        m_persistence.reset();
        m_workspace.reset();
    }
    event.Skip();
}

void ShellController::on_frame_size(wxSizeEvent& event)
{
    event.Skip();
    if (m_installed) {
        apply_left_pane_width();
        apply_agent_pane_width();
    }
}

int ShellController::agent_pane_width_within(int width) const
{
    if (m_theme == nullptr || m_frame == nullptr)
        return width;
    const AgentPaneMetrics& metrics = m_theme->metrics().agent_pane;
    const int min_width = m_frame->FromDIP(metrics.min_width);
    const int workspace_min_width = m_frame->FromDIP(metrics.workspace_min_width);
    const int handle_width = m_frame->FromDIP(metrics.resize_handle_width);
    // The Plates / Project pane is also beside the canvas, so the Agent pane
    // may not take what the canvas needs while it is on screen.
    const int left_width = m_left_pane != nullptr && m_left_pane->IsShown() ? m_left_pane->GetSize().x : 0;
    const int left_handle_width = m_left_resize_handle != nullptr && m_left_resize_handle->IsShown() ? handle_width : 0;
    const int max_width = std::max(min_width, m_frame->GetClientSize().x - workspace_min_width - handle_width -
                                             left_width - left_handle_width);
    return std::clamp(width, min_width, max_width);
}

void ShellController::request_agent_pane_width(int width)
{
    // Remember what the pane can actually take, not what the pointer asked
    // for. A drag that runs past the edge of a small window would otherwise
    // record a width the pane never had, and the pane would claim it -- at the
    // workspace's expense -- the next time the window grew enough to allow it.
    // A width the window merely cannot afford right now is a different case:
    // apply_agent_pane_width() narrows the pane without touching the
    // preference, so growing the window restores the width the person chose.
    m_agent_pane_preferred_width = agent_pane_width_within(width);
    apply_agent_pane_width();
}

void ShellController::apply_agent_pane_width()
{
    if (m_theme == nullptr || m_frame == nullptr || m_center_sizer == nullptr || m_agent_pane == nullptr)
        return;
    if (m_agent_pane_collapsed)
        return;
    const int width = agent_pane_width_within(m_agent_pane_preferred_width);
    m_center_sizer->SetItemMinSize(m_agent_pane, width, -1);
    // Lay out from the frame, not from m_center_sizer. A sizer lays itself out
    // against the dimension it was last given, which during a frame resize is
    // still the previous client width; the frame reads the current one from the
    // window. Laying out the sizer directly leaves the workspace and the
    // divider sized for the old width while the pane takes the new one, so they
    // overlap, and the next resize swaps which of them is stale.
    m_frame->Layout();
}

int ShellController::left_pane_width_within(int width) const
{
    if (m_theme == nullptr || m_frame == nullptr)
        return width;
    const int min_width = m_frame->FromDIP(m_theme->metrics().left_pane.min_width);
    const AgentPaneMetrics& agent = m_theme->metrics().agent_pane;
    const int workspace_min_width = m_frame->FromDIP(agent.workspace_min_width);
    const int handle_width = m_frame->FromDIP(agent.resize_handle_width);
    const int agent_width = m_agent_pane != nullptr && m_agent_pane->IsShown() ? m_agent_pane->GetSize().x : 0;
    const int agent_handle_width = m_agent_resize_handle != nullptr && m_agent_resize_handle->IsShown() ? handle_width : 0;
    const int max_width = std::max(min_width, m_frame->GetClientSize().x - workspace_min_width - handle_width -
                                             agent_width - agent_handle_width);
    return std::clamp(width, min_width, max_width);
}

void ShellController::request_left_pane_width(int width)
{
    m_left_pane_preferred_width = left_pane_width_within(width);
    apply_left_pane_width();
    // Both panes share the same workspace reserve. Re-evaluate the opposite
    // edge after a drag so neither preference can cover the center column.
    apply_agent_pane_width();
}

void ShellController::apply_left_pane_width()
{
    if (m_theme == nullptr || m_frame == nullptr || m_center_sizer == nullptr || m_left_pane == nullptr ||
        !m_left_pane->IsShown())
        return;
    const int width = left_pane_width_within(m_left_pane_preferred_width);
    m_center_sizer->SetItemMinSize(m_left_pane, width, -1);
    m_frame->Layout();
}

void ShellController::set_left_pane_collapsed(bool collapsed)
{
    if (m_left_pane == nullptr || m_left_resize_handle == nullptr || m_left_pane_collapsed == collapsed)
        return;
    m_left_pane_collapsed = collapsed;
    const bool available = !m_task_open && m_tabpanel != nullptr &&
                           m_tabpanel->GetSelection() == MainFrame::tp3DEditor;
    m_left_pane->Show(available && !collapsed);
    m_left_resize_handle->Show(available && !collapsed);
    if (m_status_row != nullptr)
        m_status_row->set_left_pane_collapsed(collapsed);
    if (!collapsed)
        apply_left_pane_width();
    m_frame->Layout();
    // The Agent preference may have expanded into space freed by the left
    // pane, or need constraining when it returns.
    apply_agent_pane_width();
}

void ShellController::set_agent_pane_collapsed(bool collapsed)
{
    if (m_agent_pane == nullptr || m_agent_resize_handle == nullptr || m_agent_pane_collapsed == collapsed)
        return;
    m_agent_pane_collapsed = collapsed;
    // Hidden, never destroyed: the conversation, the loaded page, and the MCP
    // runtime all outlive a collapse. A hidden sizer item takes no space, so
    // the workspace grows into the whole width with no second width policy.
    m_agent_pane->Show(!collapsed);
    m_agent_resize_handle->Show(!collapsed);
    if (m_status_row != nullptr)
        m_status_row->set_agent_pane_collapsed(collapsed);
    // Expanding re-applies the width policy, which lays out on its way; a
    // collapsed pane has no width to apply, so it lays out here.
    apply_left_pane_width();
    if (collapsed)
        m_frame->Layout();
    else
        apply_agent_pane_width();
}

void ShellController::open_agent_setup()
{
    m_agent_pane_user_collapsed = false;
    if (m_tabpanel->GetSelection() == MainFrame::tpHome)
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
    set_agent_pane_collapsed(false);
    m_agent_pane->web_view().host().request_setup();
}

void ShellController::uninstall()
{
    m_runtime_timer.Stop();
    if (!m_installed)
        return;
    m_installed = false;
    m_left_pane_collapsed = true;
    m_agent_pane_collapsed = false;
    m_frame->Unbind(wxEVT_DESTROY, &ShellController::on_frame_destroy, this);
    m_frame->Unbind(wxEVT_SIZE, &ShellController::on_frame_size, this);
    m_tabpanel->Unbind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ShellController::on_notebook_page_changed, this);

    m_prepare_canvas_presentation.detach();
    m_plater->get_collapse_toolbar().set_enabled(m_saved_collapse_toolbar_enabled);
    m_plater->set_sidebar_available(true);
    m_plater->set_auto_preview_after_slice(m_saved_auto_preview_after_slice);
    wxGetApp().show_config_wizard_on_startup = m_saved_show_config_wizard_on_startup;
    // A printer connection may have installed a provider the slicing profile
    // does not select; hand the choice back to Orca's profile-driven policy.
    wxGetApp().switch_printer_agent();
    m_tabpanel->GetBtnsListCtrl()->Show();

    if (m_center_sizer != nullptr) {
        if (m_workspace_sizer != nullptr) {
            m_workspace_sizer->Detach(m_tabpanel);
            if (m_home != nullptr)
                m_workspace_sizer->Detach(m_home);
        }
        if (m_project_sizer != nullptr) {
            if (m_status_row != nullptr)
                m_project_sizer->Detach(m_status_row);
            if (m_workspace_sizer != nullptr)
                m_project_sizer->Detach(m_workspace_sizer);
        }
        if (m_left_pane != nullptr)
            m_center_sizer->Detach(m_left_pane);
        if (m_left_resize_handle != nullptr)
            m_center_sizer->Detach(m_left_resize_handle);
        if (m_project_sizer != nullptr)
            m_center_sizer->Detach(m_project_sizer);
        if (m_agent_resize_handle != nullptr)
            m_center_sizer->Detach(m_agent_resize_handle);
        if (m_agent_pane != nullptr)
            m_center_sizer->Detach(m_agent_pane);
        if (m_printer_panel != nullptr)
            m_center_sizer->Detach(m_printer_panel);
        m_main_sizer->Detach(m_center_sizer);
        delete m_workspace_sizer;
        delete m_project_sizer;
        delete m_center_sizer;
        m_center_sizer = nullptr;
        m_project_sizer = nullptr;
        m_workspace_sizer = nullptr;
    }
    if (m_main_sizer->GetItem(m_tabpanel) == nullptr)
        m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxTOP, 0);

    if (m_left_pane != nullptr) {
        m_left_pane->Destroy();
        m_left_pane = nullptr;
    }
    if (m_status_row != nullptr) {
        m_status_row->Destroy();
        m_status_row = nullptr;
    }
    if (m_left_resize_handle != nullptr) {
        m_left_resize_handle->Destroy();
        m_left_resize_handle = nullptr;
    }
    if (m_agent_resize_handle != nullptr) {
        m_agent_resize_handle->Destroy();
        m_agent_resize_handle = nullptr;
    }
    if (m_agent_pane != nullptr) {
        m_agent_pane->web_view().host().tools().set_version_callbacks({}, {});
        m_agent_pane->Destroy();
        m_agent_pane = nullptr;
    }
    if (m_printer_panel != nullptr) {
        m_printer_panel->Destroy();
        m_printer_panel = nullptr;
    }
    if (m_home != nullptr) {
        m_home->Destroy();
        m_home = nullptr;
    }
    // The Notebook is hidden only while Home is up, so the stock presentation
    // gets it back shown.
    m_tabpanel->Show();
    // After the pane (and with it the Agent host) is gone.
    m_plater->set_before_project_release({});
    m_plater->set_external_project_open_handler({});
    m_autosave.reset();
    if (m_plater->get_project_name().empty())
        m_frame->SetTitle(m_saved_frame_title);
    else
        m_plater->update_title_dirty_status();
    Slic3r::set_backup_suspended(false);
    m_persistence.reset();
    m_workspace.reset();

    m_frame->Layout();
}

void ShellController::apply_current_appearance()
{
    const bool dark = wxGetApp().dark_mode();
    if (m_status_row != nullptr)
        m_status_row->apply_appearance(dark);
    if (m_left_pane != nullptr)
        m_left_pane->apply_appearance(dark);
    if (m_agent_pane != nullptr)
        m_agent_pane->apply_appearance(dark);
    if (m_home != nullptr)
        m_home->apply_appearance(dark);
    if (m_printer_panel != nullptr)
        m_printer_panel->apply_appearance(dark);
    if (m_agent_resize_handle != nullptr)
        m_agent_resize_handle->Refresh();
    if (m_left_resize_handle != nullptr)
        m_left_resize_handle->Refresh();
}

// Home is a screen before a project, not a workspace: the Agent pilots a
// project, so its panel has nothing to act on here and the design does not
// show it. It is collapsed while Home is up and restored to whatever the
// person had chosen on the way out.
//
// The gallery is a view of state that changes while another screen is in front
// -- a project saved, a print started -- so it is refreshed on the way in
// rather than kept live behind the canvas. While it is on screen its printer
// cards stay live (HomeWebView::set_live).
void ShellController::on_notebook_page_changed(wxBookCtrlEvent& event)
{
    on_page_changed();
    event.Skip();
}

void ShellController::open_printer_conversation(const std::string& printer_name, bool connect)
{
    if (m_task_open || m_printer_panel == nullptr || m_tabpanel == nullptr)
        return;
    // Every caller names a preset it has just read, so a miss means it went
    // between the menu and the click: say so as Orca does, never open a
    // conversation about another printer.
    if (!printer_name.empty() && !preset_exists(Preset::TYPE_PRINTER, printer_name)) {
        show_error(m_frame, wxString::Format(_L("The printer preset \"%s\" was not found."), from_u8(printer_name)));
        return;
    }
    m_printer_panel->open(connect ? PrinterSetup::ConversationMode::Connect : printer_name.empty() ? PrinterSetup::ConversationMode::Add :
                                                 PrinterSetup::ConversationMode::Change,
                          printer_name);
    show_task_panel();
}

void ShellController::open_filament_help(std::size_t slot, const std::string& preset, const std::string& shown)
{
    if (m_task_open || m_printer_panel == nullptr || m_tabpanel == nullptr)
        return;
    if (!preset_exists(Preset::TYPE_FILAMENT, preset)) {
        show_error(m_frame, wxString::Format(_L("The filament preset \"%s\" was not found."), from_u8(preset)));
        return;
    }
    m_printer_panel->open_filament(slot, preset, shown,
        m_tabpanel->GetSelection() == MainFrame::tpPreview ? _L("Back to Preview") : _L("Back to Prepare"));
    show_task_panel();
}

void ShellController::show_task_panel()
{
    // Keep the stock page constructed and selected. Hiding its sizer item
    // gives the task chat the workspace without changing Orca's tab indices.
    m_task_open = true;
    m_task_origin_tab = m_tabpanel->GetSelection();
    m_home->set_live(false);
    m_workspace_sizer->Show(false);
    m_left_pane->Hide();
    m_left_resize_handle->Hide();
    m_status_row->Hide();
    m_agent_pane->Hide();
    m_agent_resize_handle->Hide();
    m_printer_panel->Show();
    m_frame->Layout();
}

void ShellController::restore_task_panel()
{
    if (!m_task_open)
        return;
    m_task_open = false;
    m_task_origin_tab = -1;
    m_printer_panel->Hide();
    m_workspace_sizer->Show(true);
    m_agent_pane->Show(!m_agent_pane_collapsed);
    m_agent_resize_handle->Show(!m_agent_pane_collapsed);
    on_page_changed();
    apply_agent_pane_width();
}

void ShellController::on_page_changed()
{
    if (!m_installed || m_home == nullptr || m_tabpanel == nullptr)
        return;
    if (m_task_open) {
        if (m_tabpanel->GetSelection() != m_task_origin_tab)
            m_printer_panel->close();
        return;
    }
    const bool home = m_tabpanel->GetSelection() == MainFrame::tpHome;
    if (home)
        m_home->refresh();
    m_home->set_live(home);
    // A throwaway setup webview elsewhere (the Add a printer dialog) may have
    // written a working agent config since the docked pane last checked; push
    // it in now, the same "refresh on the way in" pattern as Home's gallery
    // above, rather than reloading the page or polling continuously.
    if (m_agent_config_possibly_changed) {
        m_agent_config_possibly_changed = false;
        Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
        m_agent_pane->web_view().host().set_agent(std::move(runtime.service), runtime.availability);
    }
    // Home and the Notebook share the workspace slot, so exactly one is shown.
    // The Notebook is only hidden while Home is the selection: post_init
    // selects the Prepare tab to map the GL canvas before it initialises
    // OpenGL, and this handler runs on that selection, so the Notebook is
    // shown at the moment the canvas has to be on screen.
    // The header belongs to the workspace: its controls act on the open
    // project, so Home, which has no project in front, shows no header.
    m_home->Show(home);
    m_tabpanel->Show(!home);
    m_status_row->Show(!home);
    // The pane lists the project being prepared; Preview draws its own plate
    // picker and Home has no project in front.
    const bool left_pane_available = !home && m_tabpanel->GetSelection() == MainFrame::tp3DEditor;
    if (m_status_row != nullptr)
        m_status_row->set_left_pane_available(left_pane_available);
    const bool show_left_pane = left_pane_available && !m_left_pane_collapsed;
    m_left_pane->Show(show_left_pane);
    m_left_resize_handle->Show(show_left_pane);
    m_frame->Layout();
    apply_left_pane_width();
    set_agent_pane_collapsed(home ? true : m_agent_pane_user_collapsed);
}

void attach_shell(MainFrame& frame, Notebook* tabpanel, wxSizer* main_sizer)
{
    if (tabpanel == nullptr || main_sizer == nullptr)
        return;
    if (!wxGetApp().is_editor())
        return;
    if (wxGetApp().app_config != nullptr && wxGetApp().app_config->get("jusprin_shell") == "0")
        return;

    auto controller = std::make_unique<ShellController>();
    try {
        controller->install(frame, *tabpanel, *main_sizer);
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "JusPrin shell could not be installed, keeping the standard "
                                    "presentation: " << error.what();
        return;
    }
    shell_slot() = std::move(controller);
}

ShellController* installed_shell()
{
    return shell_slot().get();
}

void detach_shell()
{
    if (shell_slot() != nullptr) {
        shell_slot()->uninstall();
        shell_slot().reset();
    }
}

} // namespace Slic3r::GUI::JusPrin
