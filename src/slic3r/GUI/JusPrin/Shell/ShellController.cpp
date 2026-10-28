#include "ShellController.hpp"

#include "AgentPane.hpp"
#include "ClassicView.hpp"
#include "GcodeHistoryRecorder.hpp"
#include "slic3r/GUI/JusPrin/Updates/UpdateCheck.hpp"
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
#include "slic3r/GUI/JusPrin/Preferences/PreferencesWindow.hpp"
#include "slic3r/GUI/JusPrin/V1Import/WelcomeBackWindow.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Canvas/EmptyPlateHint.hpp"
#include "slic3r/GUI/JusPrin/Home/OrcaHomeBackend.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/JusPrin/Workspace/DialogListeners.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Project.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/glcanvas.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/timer.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <utility>
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

// How long a switch to classic view waits before trying again while a web
// view is installing its script message handler (detach_shell).
constexpr int kDetachRetryMs = 50;

// What attach_shell received from the frame being built, kept so the shell
// can be installed again after a switch to classic view.
struct AttachPoint
{
    MainFrame* frame{nullptr};
    Notebook*  tabpanel{nullptr};
    wxSizer*   main_sizer{nullptr};
};

AttachPoint& attach_point()
{
    static AttachPoint point;
    return point;
}

// The way back while the shell is off; empty whenever the shell is installed.
std::unique_ptr<ClassicViewReturn>& classic_view_slot()
{
    static std::unique_ptr<ClassicViewReturn> classic;
    return classic;
}

// What the shell left open when it switched to classic view, for the shell
// that comes back; kept until one installs.
std::optional<ShellController::OpenProject>& left_project()
{
    static std::optional<ShellController::OpenProject> left;
    return left;
}

// Builds a controller over the attach point and installs it. On failure the
// stock presentation is untouched and the logged reason is returned.
std::optional<std::string> install_shell()
{
    const AttachPoint& point = attach_point();
    auto controller = std::make_unique<ShellController>();
    try {
        controller->install(*point.frame, *point.tabpanel, *point.main_sizer, left_project());
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "JusPrin shell could not be installed, keeping the standard "
                                    "presentation: " << error.what();
        return std::string(error.what());
    }
    shell_slot() = std::move(controller);
    left_project().reset();
    return std::nullopt;
}

void install_classic_view()
{
    // Going back installs the shell, which needs these same tokens: without
    // them a way back could only ever fail.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "JusPrin design tokens not loaded, so classic view offers no way back to JusPrin";
        return;
    }
    const AttachPoint& point = attach_point();
    auto classic = std::make_unique<ClassicViewReturn>(*point.frame, *point.tabpanel, *point.main_sizer, *theme,
                                                       [] { reattach_shell(); });
    try {
        classic->install();
    } catch (const std::runtime_error& error) {
        // Only the View-menu lookup throws once the banner is up; the banner
        // stays and is the way back on its own.
        BOOST_LOG_TRIVIAL(error) << "JusPrin's View-menu row was not added; the classic view banner remains: "
                                 << error.what();
    }
    classic_view_slot() = std::move(classic);
}

// The Prepare canvas's drop target, in place of OrcaSlicer's PlaterDropTarget,
// which it follows except for where the files go: they are the shell's to
// add (ShellController::add_dropped_files). OrcaSlicer's one exception, a
// lone SVG embossed where it lands, is not kept: its gizmo is hidden in the
// shell, so the SVG loads as an object like any other file. Without a shell
// to hand them to, the files take OrcaSlicer's own path.
class PrepareDropTarget final : public wxFileDropTarget
{
public:
    PrepareDropTarget(MainFrame& frame, Plater& plater, std::function<bool(const wxArrayString&)> add)
        : m_frame(frame), m_plater(plater), m_add(std::move(add))
    {
        SetDefaultAction(wxDragCopy);
    }

    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override
    {
#ifdef _WIN32
        MSWUpdateDragImageOnLeave(); // hides the system icon
#endif
        m_frame.Raise();
        m_frame.select_tab(size_t(MainFrame::tp3DEditor));
        m_plater.select_view_3D("3D");
        const bool loaded = m_add ? m_add(files) : m_plater.load_files(files);
        m_frame.update_title();
        return loaded;
    }

private:
    MainFrame&                                 m_frame;
    Plater&                                    m_plater;
    std::function<bool(const wxArrayString&)> m_add;
};

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
        if (m_left_pane != nullptr && m_plater != nullptr && m_plater->is_background_process_slicing())
            m_left_pane->refresh_slicing_progress();
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

void ShellController::install(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer,
                              const std::optional<OpenProject>& left)
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

        // Conversation state, stored beside each project's local versions
        // under the application data dir.
        Agent::ProjectPersistence::Config persistence_config;
        persistence_config.managed_root = (boost::filesystem::path(data_dir()) / "jusprin" / "projects").string();
        m_persistence = std::make_unique<Agent::ProjectPersistence>(*m_workspace, std::move(persistence_config));

        Agent::AgentRuntime agent = Agent::load_agent_runtime(wxGetApp().app_config);

        m_status_row = new StatusRow(&frame, *m_theme, *plater, tabpanel, *m_workspace, *m_persistence);
        m_agent_pane = new AgentPane(&frame, *m_theme, *m_workspace, *m_persistence, agent.availability,
                                     std::move(agent.service), std::move(agent.setup),
                                     (boost::filesystem::path(data_dir()) / "jusprin" / "mcp.json").string());
        m_agent_pane->web_view().enable_chat_model_choice(wxGetApp().app_config);
        m_agent_pane_preferred_width = frame.FromDIP(m_theme->metrics().agent_pane.width);
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
        m_status_row->set_plan_recorded([pane = wxWeakRef<AgentPane>(m_agent_pane)] {
            return pane && !pane->web_view().host().persistence().document().plan().headline.empty();
        });
        // A file the person opened or imported: the project conversation's
        // Agent speaks about it, or its card lists what OrcaSlicer said.
        m_workspace->set_load_report_listener([pane = wxWeakRef<AgentPane>(m_agent_pane)](const Workspace::LoadReport& report) {
            if (pane) pane->web_view().host().on_file_loaded(report);
        });

        // Adopt the currently open project once the host has registered its
        // listeners, so the initial document reaches the pane too. Its
        // conversation is kept under the managed project's id, which the
        // project itself does not carry, so a shell coming back over the
        // project an earlier one left takes up that one's state. A project
        // OrcaSlicer replaced or reset meanwhile has a new session or a new
        // auxiliary directory, and starts its own.
        if (left && left->project_session == plater->project_state_session() &&
            left->auxiliary_dir == m_workspace->auxiliary_data_dir())
            m_persistence->adopt_managed_state(left->state);
        else
            m_persistence->attach();
        m_autosave = std::make_unique<Workspace::ProjectAutosave>(
            *plater, *m_persistence, *m_workspace,
            std::filesystem::path(data_dir()) / "jusprin" / "projects");
        m_gcode_history = std::make_unique<GcodeHistoryRecorder>(
            *plater, *m_persistence, *m_workspace, std::filesystem::path(data_dir()) / "jusprin" / "projects",
            [this] { return gcode_records_by_project(); });
        // A print that starts or finishes while Home is up changes its cards
        // and, when it is open, the Print history screen.
        m_gcode_history->set_changed_listener([this] {
            if (m_home != nullptr && m_home_shown)
                m_home->refresh();
        });
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
            [this](const std::string& version, const std::string& chat) {
                return m_autosave->restore_chat(version, chat);
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
            if (action == "open_preferences") {
                request_preferences(payload.value("section", std::string()));
                return true;
            }
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
            if (action == "open_print_history") {
                open_print_history(payload.value("hash", std::string()));
                return true;
            }
            if (action == "confirm_answer") {
                // After this handler returns, as for collapsing: the answer
                // may switch tabs and start a turn.
                m_frame->CallAfter([this, id = payload.value("requestId", std::string()),
                                    confirmed = payload.value("confirmed", false)] {
                    if (m_installed)
                        answer_calibration(id, confirmed);
                });
                return true;
            }
            if (action == "compute_setup_estimates") {
                const auto& document = m_persistence->document();
                const auto snapshot = m_workspace->snapshot();
                const bool active_chat = document.viewed_conversation_id() == document.active_conversation_id();
                const bool same_project = payload.value("sessionId", std::string()) == std::to_string(snapshot.session.value());
                const bool same_plate = snapshot.active_plate &&
                    payload.value("plateId", std::string()) == std::to_string(snapshot.active_plate->value());
                if (!active_chat || !same_project || !same_plate) {
                    wxMessageBox(_L("The setup changed. Review the current plate and try again."),
                                 _L("Could not compute estimates"), wxOK | wxICON_WARNING, m_frame);
                    return true;
                }
                const auto result = m_workspace->start_slice(snapshot.active_plate, false);
                if (!result.succeeded())
                    wxMessageBox(wxString::FromUTF8(result.message), _L("Could not compute estimates"),
                                 wxOK | wxICON_ERROR, m_frame);
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
        // A model turn saves the project once, here, after its last edit: the
        // chat checkpoint pins the model as it is now. Edits between the
        // turn's first and this point make no version of their own.
        m_agent_pane->web_view().host().set_turn_boundary_callback([this] {
            m_agent_pane->web_view().host().checkpoint_active_chat();
            m_turn_before_version.clear();
        });
        m_autosave->set_capture_hold([this] { return m_agent_pane && m_agent_pane->web_view().host().busy(); });
        Slic3r::set_backup_suspended(true);
        m_status_row->set_autosave(m_autosave.get());
        plater->set_before_project_release([this] {
            // A calibration generator's own new_project: the person's project
            // is already saved, and the empty one it replaces is not theirs.
            if (m_workspace && m_workspace->generating_calibration())
                return true;
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
            // A tool's own open settles this in the conversation instead.
            if (!m_autosave || m_workspace->tool_load_active())
                return false;
            // A model file from File > Recent files opens as it does from the
            // OS: in a new project, without the question below.
            if (!boost::algorithm::iends_with(path.string(), ".3mf"))
                return take_opened_files({path});
            const auto found = m_autosave->projects_for_source(source);
            if (!found) {
                // Whether the file was opened before cannot be told, and a
                // project made from it could not be saved either. The open
                // stops, the current project stays, and opening the file
                // again works once the projects folder can be read.
                MessageDialog dialog(m_frame,
                                     _L("Your saved projects couldn't be read, so JusPrin can't check whether you've opened this "
                                        "file before. The file wasn't opened. Try again once JusPrin can read its projects folder."),
                                     _L("Couldn't open file"), wxOK | wxICON_WARNING);
                Workspace::ScopedDialogListener theirs([&dialog](wxWindow& window, const Workspace::DialogFacts&) {
                    return &window == &dialog ? std::optional<int>(Workspace::kShowToPerson) : std::nullopt;
                });
                dialog.ShowModal();
                return true;
            }
            const auto& matches = *found;
            if (matches.empty())
                return false;
            const bool current = matches.front().current;
            // A file that changed since is most likely a new export: a new
            // project is the default then; otherwise the saved one.
            const bool changed = matches.front().changed.value_or(false);
            const ReimportDecision decision = m_reimport_confirmation ? m_reimport_confirmation() : [this, &matches, current, changed] {
                const wxString message =
                    current && changed ? _L("This file has changed since this project was made from it. Create a new project from the "
                                            "updated file, or continue the current project, which keeps the earlier version.") :
                    current ? _L("This file is already open as a project. Continue working in it, or create a new project from this file.") :
                    changed ? _L("This file has changed since you last opened it. Create a new project from the updated file, or "
                                 "open the existing project, which keeps the earlier version.") :
                    matches.size() > 1 ?
                    _L("You've opened this file before. Open the most recently updated project or create another project from this file.") :
                    _L("You've opened this file before. Open the existing project to continue where you left off, "
                       "or create a new project from this file.");
                // OrcaSlicer's own dialog, so it looks like every other
                // question the app asks. Its Yes is the highlighted button,
                // so Yes is the choice suggested here.
                MessageDialog dialog(m_frame, message, _L("Previously opened file"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
                const wxString keep  = current ? _L("Continue current project") :
                                       matches.size() > 1 ? _L("Open most recent project") : _L("Open existing project");
                const wxString fresh = _L("Create new project");
                dialog.SetButtonLabel(wxID_YES, changed ? fresh : keep);
                dialog.SetButtonLabel(wxID_NO, changed ? keep : fresh);
                // A person's open answers OrcaSlicer's dialogs on its own
                // (FileLoads.hpp); this question is theirs.
                Workspace::ScopedDialogListener theirs([&dialog](wxWindow& window, const Workspace::DialogFacts&) {
                    return &window == &dialog ? std::optional<int>(Workspace::kShowToPerson) : std::nullopt;
                });
                const int answer = dialog.ShowModal();
                if (answer != wxID_YES && answer != wxID_NO)
                    return ReimportDecision::Cancel;
                return (answer == wxID_YES) != changed ? ReimportDecision::OpenExisting : ReimportDecision::CreateNew;
            }();
            if (decision == ReimportDecision::CreateNew)
                return false;
            if (decision == ReimportDecision::OpenExisting) {
                if (!m_autosave->open_managed_project(matches.front().id))
                    MessageDialog(m_frame, _L("The project couldn't be opened. Try again."), _L("Couldn't open project"),
                                  wxOK | wxICON_ERROR).ShowModal();
                else
                    m_frame->select_tab(size_t(MainFrame::tp3DEditor));
            }
            return true;
        });
        plater->set_external_file_open_handler([this](const std::vector<Plater::fs_path>& files) {
            return take_opened_files(files);
        });
        install_drop_target();
        plater->Bind(wxEVT_IDLE, &ShellController::on_plater_idle, this);
        // Within one model turn the first edit pins the version before the
        // turn and the later edits reuse it; the turn boundary saves the
        // result. An edit outside a turn (an external MCP client's) still
        // pins before and saves after, as before.
        const auto in_model_turn = [this] { return m_agent_pane && m_agent_pane->web_view().host().busy(); };
        const auto version_before = [this, in_model_turn](const Agent::ToolActivity&) {
            if (in_model_turn() && !m_turn_before_version.empty())
                return m_turn_before_version;
            const std::string before = m_autosave ? m_autosave->pin_current() : std::string();
            if (before.empty())
                throw std::runtime_error("Couldn't save the project. The action was not run.");
            if (in_model_turn())
                m_turn_before_version = before;
            return before;
        };
        const auto version_after = [this, in_model_turn](const Agent::ToolActivity& activity, const std::string& before) {
            const char* outcome = activity.state == Agent::ToolState::Succeeded ? "succeeded" :
                                  activity.state == Agent::ToolState::Cancelled ? "cancelled" : "failed";
            if (m_autosave)
                m_autosave->record_agent_operation(activity.action_id, activity.tool, outcome, before, !in_model_turn());
        };
        m_agent_pane->web_view().host().tools().set_version_callbacks(version_before, version_after);
        m_agent_pane->web_view().host().tools().set_earlier_projects(
            [this](const std::string& path) {
                using Coordinator = Agent::ToolExecutionCoordinator;
                if (!m_autosave)
                    return Coordinator::EarlierLookup{};
                const auto found = m_autosave->projects_for_source(std::filesystem::u8path(path));
                if (!found) {
                    Coordinator::EarlierLookup unreadable;
                    unreadable.unreadable = true;
                    return unreadable;
                }
                if (found->empty())
                    return Coordinator::EarlierLookup{};
                const auto& match = found->front();
                return Coordinator::EarlierLookup{Coordinator::EarlierProject{match.id, match.name, match.current, match.changed}};
            },
            [this](const std::string& id) { return m_autosave && m_autosave->open_managed_project(id); },
            [this](const std::string& path) {
                if (m_autosave)
                    m_autosave->record_opened_from(std::filesystem::u8path(path));
            });
        m_agent_pane->web_view().host().tools().set_print_dialog_opener(
            [this](const Agent::ToolExecutionCoordinator::PrintDialogRequest& request) {
                return open_print_dialog(request);
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
                [](std::size_t slot) { SetupCommands::open_filament_settings(slot); }, version_before, version_after,
                // The project's own counter, which its agent pane takes ids from.
                [this] { return m_persistence->document().allocate_action_id(); }});
        m_home->backend().set_conversation_opener(
            [this](const std::string& printer_name, bool connect) { open_printer_conversation(printer_name, connect); });
        m_home->backend().set_project_opening([this] { m_agent_pane->web_view().host().set_project_opened_from("Home"); });
        // The card names its printer and no filament; the assistant sets the
        // printer up before making the test.
        m_home->backend().set_calibration_starter(
            [this](const std::string& printer_name, const std::vector<std::string>& spools) {
                start_calibration(printer_name, {}, spools);
            });
        m_home->host().set_confirmation_listener([this](const std::string& id, bool confirmed) {
            m_frame->CallAfter([this, id, confirmed] {
                if (m_installed)
                    answer_calibration(id, confirmed);
            });
        });

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
        // OrcaSlicer's File > Import: its file dialog, then the file's objects
        // added to the open project.
        m_left_pane->set_add_model([this] { m_plater->add_model(); });
        m_status_row->set_left_pane_toggle([this] { toggle_left_pane(); });
        m_status_row->set_left_pane_collapsed(m_left_pane_collapsed);
        // The overflow menu's project rows are the Project tab's. Its two
        // views open in the pane, which shows only on Prepare; the export is
        // the tab's own and needs no pane on screen.
        m_status_row->set_project_rows([this](ProjectMenuRow row) {
            if (row == ProjectMenuRow::Export) {
                m_left_pane->export_project();
                return;
            }
            m_frame->select_tab(size_t(MainFrame::tp3DEditor));
            if (is_left_pane_collapsed())
                toggle_left_pane();
            if (row == ProjectMenuRow::VersionHistory)
                m_left_pane->open_project_view(ProjectView::Versions);
            else
                m_left_pane->show_project();
        });
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
            },
            {},
            [this] { open_print_history(); },
            [] { return live_prints(); }});
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
            // Tab on the canvas is OrcaSlicer's switch to Preview, which would
            // change the page under the JusPrin header. Bound after Plater's
            // own handler, so this one runs first; not skipping it stops the
            // switch. Tab on the Preview canvas still goes back to Prepare.
            prepare_canvas->get_wxglcanvas()->Bind(EVT_GLCANVAS_TAB, &ShellController::on_prepare_canvas_tab, this);
            // Print issues: read from OrcaSlicer by the monitor and shown by
            // the overlay on this canvas. OrcaSlicer's own notices are one of
            // the legacy overlays the attach above hides; the notification
            // manager keeps running underneath, and the detach that restores
            // the overlays brings the notices straight back.
            m_print_issues = std::make_unique<PrintIssues::PrintIssueMonitor>(*plater);
            // The workspace records why a slice did not succeed in the words
            // the monitor reads from OrcaSlicer, at the moment it happens.
            m_workspace->set_slice_failure_reader([this]() -> std::optional<Workspace::SliceFailure> {
                return m_print_issues ? m_print_issues->slice_failure() : std::nullopt;
            });
            // The workspace summarizes a slice's warnings in the words the
            // monitor reads from OrcaSlicer, with their setting and object.
            // Read now: the slice's end reaches the workspace before the
            // notification manager's next turn has the monitor look again,
            // and a slice_start result is built at that moment. Its callbacks
            // (the overlay's redraw, the work entries) never read the workspace.
            m_workspace->set_slice_warnings_reader(
                [this](std::uint64_t plate) -> std::optional<std::vector<Workspace::SliceWarningFinding>> {
                    if (m_print_issues == nullptr)
                        return std::nullopt;
                    m_print_issues->refresh_now();
                    if (m_print_issues->snapshot().plate != plate)
                        return std::nullopt;
                    return PrintIssues::slice_warnings_of(m_print_issues->snapshot());
                });
            PrintIssues::IssueOverlay::Actions actions;
            actions.highlight = [this](const PrintIssues::PrintIssue& issue) {
                if (m_workspace == nullptr || m_print_issues == nullptr)
                    return;
                // The command's own result is not needed: an already selected
                // copy is the outcome wanted, and a copy that has just gone
                // takes its issue with it on the next read.
                const Workspace::ProjectSessionId session(m_print_issues->snapshot().session);
                if (issue.instance != 0)
                    m_workspace->select_copy(Workspace::InstanceId(session, issue.instance));
                else if (issue.object != 0)
                    m_workspace->select_object(Workspace::ObjectId(session, issue.object));
            };
            actions.submit = [this](const std::string& id, const std::string& request) {
                const std::string outcome = submit_issue(id, request).outcome;
                // "duplicate": the same question about the same evidence is
                // already in the chat.
                return outcome == "started" || outcome == "queued" || outcome == "duplicate";
            };
            actions.chat_busy = [this] { return m_agent_pane != nullptr && m_agent_pane->web_view().host().busy(); };
            actions.agent_ready = [this] {
                return m_agent_pane != nullptr && m_agent_pane->web_view().host().availability() == Agent::AgentAvailability::Ready;
            };
            // The one place the agent is set up. Opened from a later turn:
            // the press arrives inside the canvas's own frame.
            actions.set_up_agent = [this] { request_preferences("agent"); };
            m_issue_overlay = std::make_unique<PrintIssues::IssueOverlay>(*prepare_canvas, *m_print_issues, std::move(actions));
            m_prepare_canvas_presentation.set_extra_overlay([this, prepare_canvas] {
                if (m_plater->model().objects.empty())
                    render_empty_plate_hint(*prepare_canvas);
                if (m_issue_overlay)
                    m_issue_overlay->render();
            });
            // Work that runs outside the chat is kept in the chat: an entry
            // at the place it started, with its progress and its Cancel. The
            // panel opens when work starts, or the entry would run unseen.
            PrintIssues::WorkFeed::Sink sink;
            sink.begin = [this](const std::string& kind, const std::string& title, bool cancellable) {
                if (m_agent_pane == nullptr)
                    return std::string();
                set_agent_pane_collapsed(false);
                return m_agent_pane->web_view().host().begin_work(kind, title, cancellable);
            };
            sink.update = [this](const std::string& id, int current, int total, const std::string& detail) {
                if (m_agent_pane != nullptr)
                    m_agent_pane->web_view().host().update_work(id, current, total, detail);
            };
            sink.finish = [this](const std::string& id, const std::string& state, const std::string& detail) {
                if (m_agent_pane != nullptr)
                    m_agent_pane->web_view().host().finish_work(id, state, detail);
            };
            m_work_feed = std::make_unique<PrintIssues::WorkFeed>(*plater, std::move(sink));
            m_print_issues->set_work_callback([this] {
                if (m_work_feed && m_print_issues)
                    m_work_feed->sync(m_print_issues->snapshot());
            });
            if (m_agent_pane != nullptr)
                m_agent_pane->web_view().host().set_work_cancel_handler([this](const std::string& id) {
                    if (m_work_feed)
                        m_work_feed->cancel(id);
                });
        }

        // The frame may outlive a runtime detach, so use a tracked event sink
        // and disconnect it when uninstalling the controller.
        frame.Bind(wxEVT_DESTROY, &ShellController::on_frame_destroy, this);
        frame.Bind(wxEVT_SIZE, &ShellController::on_frame_size, this);
        frame.Bind(wxEVT_CHAR_HOOK, &ShellController::on_frame_char_hook, this);
        // Every way into Preferences OrcaSlicer has -- its menu items, Ctrl+P,
        // the macOS app menu's Cmd+, -- opens JusPrin's window. A request for
        // one of OrcaSlicer's own tabs or options still opens its dialog.
        // The hook asks for whichever shell is installed when it runs: a
        // language reload installs the next shell before this one is gone,
        // so this one must not own the hook, nor clear it on the way out.
        wxGetApp().set_preferences_handler([](size_t tab, const std::string& option) {
            ShellController* shell = installed_shell();
            if (shell == nullptr || !shell->is_installed() || !Preferences::answers_open_request(tab, option))
                return false;
            shell->open_preferences();
            return true;
        });

        // A member function rather than a lambda so uninstall() can take it
        // off again: the Notebook outlives this controller.
        tabpanel.Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ShellController::on_notebook_page_changed, this);

        show_orca_calibration_menu(false);
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
        // A JusPrin v1 setup imported at this start is shown once the window
        // is on screen, OrcaSlicer has finished starting up, and none of the
        // dialogs OrcaSlicer opens at start-up is still waiting for an answer
        // (one opened on top of it would run inside its modal loop).
        if (const auto report = V1Import::read_report(std::filesystem::u8path(data_dir())); report && report->pending) {
            m_welcome_wait = std::make_unique<wxTimer>();
            m_welcome_wait->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                if (!wxGetApp().post_initialized() || !m_frame->IsShownOnScreen())
                    return;
                for (wxWindow* window : wxTopLevelWindows)
                    if (const auto* dialog = dynamic_cast<wxDialog*>(window); dialog != nullptr && dialog->IsModal())
                        return;
                m_welcome_wait->Stop();
                m_frame->CallAfter([this] { open_welcome_back(); });
            });
            m_welcome_wait->Start(250);
        }
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
        m_welcome_wait.reset();
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
            m_agent_pane->web_view().host().tools().set_earlier_projects({}, {}, {});
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
            m_home->backend().set_project_opening({});
        }
        m_plater->set_before_project_release({});
        m_plater->set_external_project_open_handler({});
        m_plater->set_external_file_open_handler({});
        m_plater->Unbind(wxEVT_IDLE, &ShellController::on_plater_idle, this);
        m_gcode_history.reset();
        m_autosave.reset();
        m_persistence.reset();
        m_work_feed.reset();
        m_issue_overlay.reset();
        m_print_issues.reset();
        m_workspace.reset();
    }
    event.Skip();
}

bool ShellController::take_opened_files(const std::vector<boost::filesystem::path>& files)
{
    // A drop on the canvas adds; a tool's own load settles its file in the
    // conversation.
    if (m_adding_files || !m_autosave || m_workspace->tool_load_active())
        return false;
    const auto has_extension = [](const boost::filesystem::path& file, const char* extension) {
        return boost::algorithm::iends_with(file.string(), extension);
    };
    // A .3mf opens as a project through OrcaSlicer's load_project, which asks
    // the "Previously opened file" question; files beside it are added to it.
    if (std::any_of(files.begin(), files.end(), [&](const auto& file) { return has_extension(file, ".3mf"); }))
        return false;
    const auto model = std::find_if(files.begin(), files.end(), [&](const auto& file) { return !has_extension(file, ".zip"); });
    if (model == files.end() && !m_zip_opens_project)
        return false;

    const boost::filesystem::path& named = model != files.end() ? *model : files.front();
    BOOST_LOG_TRIVIAL(info) << "JusPrin opens " << files.size() << " file(s) in a new project: " << named.string();
    if (!m_workspace->release_project(std::string(from_path(named.stem()).ToUTF8()))) {
        MessageDialog dialog(m_frame, _L("The open project couldn't be saved, so the file wasn't opened. Try again."),
                             _L("Couldn't open file"), wxOK | wxICON_ERROR);
        Workspace::ScopedDialogListener theirs([&dialog](wxWindow& window, const Workspace::DialogFacts&) {
            return &window == &dialog ? std::optional<int>(Workspace::kShowToPerson) : std::nullopt;
        });
        dialog.ShowModal();
        return true;
    }
    // OrcaSlicer's own load into the new project, as a drop on the canvas
    // adds: the same snapshot, zip and multi-file handling.
    wxArrayString again;
    for (const auto& file : files)
        again.Add(from_path(file));
    if (add_dropped_files(again)) {
        // OrcaSlicer's File > Recent files lists each model a project was
        // started from, whatever its recent_models preference says, and
        // opening one there opens it as this did. A file dropped on Home is
        // a temporary copy, not the person's file, so it is left out.
        const std::filesystem::path dropped = Home::dropped_files_folder().lexically_normal();
        for (const auto& file : files) {
#ifdef _WIN32
            const std::filesystem::path path = std::filesystem::path(file.wstring()).lexically_normal();
#else
            const std::filesystem::path path = std::filesystem::path(file.string()).lexically_normal();
#endif
            const bool from_home_drop =
                std::mismatch(dropped.begin(), dropped.end(), path.begin(), path.end()).first == dropped.end();
            if (!has_extension(file, ".zip") && !from_home_drop)
                m_frame->add_to_recent_projects(from_path(file));
        }
    }
    m_frame->select_tab(size_t(MainFrame::tp3DEditor));
    return true;
}

bool ShellController::add_dropped_files(const wxArrayString& files)
{
    const bool was_adding = std::exchange(m_adding_files, true);
    const bool loaded     = m_plater->load_files(files);
    m_adding_files        = was_adding;
    return loaded;
}

bool ShellController::open_files_as_new_project(const wxArrayString& files)
{
    const bool was_set  = std::exchange(m_zip_opens_project, true);
    const bool loaded   = m_plater->load_files(files);
    m_zip_opens_project = was_set;
    return loaded;
}

void ShellController::install_drop_target()
{
    m_plater->SetDropTarget(new PrepareDropTarget(*m_frame, *m_plater, [this](const wxArrayString& files) {
        BOOST_LOG_TRIVIAL(info) << "JusPrin adds " << files.size() << " dropped file(s) to the open project";
        return add_dropped_files(files);
    }));
}

void ShellController::on_plater_idle(wxIdleEvent& event)
{
    event.Skip();
    if (m_installed && dynamic_cast<PrepareDropTarget*>(m_plater->GetDropTarget()) == nullptr)
        install_drop_target();
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

ShellController::IssueSubmission ShellController::submit_issue(const std::string& issue_id, const std::string& request_sentence)
{
    using namespace PrintIssues;
    IssueSubmission result;
    if (m_print_issues == nullptr || m_workspace == nullptr || m_agent_pane == nullptr || m_persistence == nullptr) {
        result.outcome = "unavailable";
        return result;
    }
    // What OrcaSlicer says at the press, not what the bubble was drawn from.
    m_print_issues->refresh_now();
    const PrintIssueSnapshot& issues = m_print_issues->snapshot();
    const PrintIssue*         issue  = issues.find(issue_id);
    if (issue == nullptr) {
        result.outcome = "expired";
        return result;
    }
    if (!issues.checked || issue->stale) {
        result.outcome = "unchecked";
        return result;
    }
    Agent::AgentHost& host = m_agent_pane->web_view().host();
    // With nobody to answer, a question in the chat would only fail there.
    if (host.availability() != Agent::AgentAvailability::Ready) {
        result.outcome = "agent_not_set_up";
        return result;
    }
    if (host.busy()) {
        result.outcome = "busy";
        return result;
    }

    const Workspace::WorkspaceSnapshot workspace = m_workspace->snapshot();
    IssueMessageRequest request;
    request.request_sentence   = request_sentence;
    request.workspace_session  = workspace.session.value();
    request.workspace_revision = workspace.revision;
    request.conversation_id    = m_persistence->document().active_conversation_id();
    result.message = make_issue_message(*issue, issues, request);

    // The reply has to be somewhere the person can see it.
    if (m_agent_pane_collapsed) {
        m_agent_pane_user_collapsed = false;
        set_agent_pane_collapsed(false);
    }
    std::string refusal;
    switch (host.submit_issue_message(result.message.client_message_id, result.message.text, result.message.context_json, &refusal)) {
    case Agent::AgentHost::SubmitResult::Started:   result.outcome = "started"; break;
    case Agent::AgentHost::SubmitResult::Queued:    result.outcome = "queued"; break;
    case Agent::AgentHost::SubmitResult::Duplicate: result.outcome = "duplicate"; break;
    case Agent::AgentHost::SubmitResult::Refused:   result.outcome = refusal; break;
    }
    return result;
}

void ShellController::start_calibration(const std::string& printer, const std::string& filament,
                                        const std::vector<std::string>& spools)
{
    Agent::AgentHost& host = m_agent_pane->web_view().host();
    // A menu row has no sentence of the Agent's before it, so board 3's one
    // popup says the two facts: the open project is saved and closed, and
    // the test's settings are temporary. It names the filament and printer,
    // never a test: the test is picked in the calibration project. The page
    // the person is looking at asks: Home for its card, the project chat for
    // the header's rows. Nothing changes until they choose Start. With no
    // Agent set up (decision 18) the same dialog says the test needs one, and
    // its primary action opens Preferences › Agent instead of Start.
    const bool        needs_agent = host.availability() != Agent::AgentAvailability::Ready;
    const bool        home    = m_tabpanel->GetSelection() == MainFrame::tpHome;
    const std::string project = home ? std::string() : m_workspace->snapshot().setup.project_name;
    const wxString    target  = filament.empty() ? from_u8(printer) :
                                                   wxString::Format(_L("%1$s on %2$s"), from_u8(filament), from_u8(printer));
    const wxString closes = project.empty() ? wxString() : wxString::Format(_L("%s will be saved and closed. "), from_u8(project));
    const wxString opens  = filament.empty() ?
        wxString::Format(_L("A new calibration project opens for %s, and the agent picks the spool and the test with you there."), target) :
        wxString::Format(_L("A new calibration project opens for %s, and the agent picks the test with you there."), target);
    const wxString unset = needs_agent ? " " + _L("The agent isn't set up yet; the test needs it to choose the range and read the result.")
                                       : wxString();
    const std::string body = into_u8(closes + opens + " " + _L("Settings the test changes are temporary and are not saved to your filament.") +
                                     unset);
    const std::string id = "calibrate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    m_pending_calibration = PendingCalibration{id, printer, filament, home ? std::string("Home") : project, spools, needs_agent};
    const std::string title   = into_u8(wxString::Format(_L("Calibrate %s?"), target));
    const std::string confirm = into_u8(needs_agent ? _L("Set up the agent…") : _L("Start"));
    const std::string cancel  = into_u8(_L("Cancel"));
    if (home) {
        m_home->host().ask_confirmation({id, title, body, confirm, cancel});
        return;
    }
    // The project chat, not the printer's: the test needs the project's tools.
    restore_task_panel();
    m_agent_pane_user_collapsed = false;
    set_agent_pane_collapsed(false);
    host.ask_confirmation({id, title, body, confirm, cancel});
}

void ShellController::answer_calibration(const std::string& request_id, bool confirmed)
{
    // An answer to a question a newer one replaced asks for nothing.
    if (!m_pending_calibration || m_pending_calibration->request_id != request_id)
        return;
    const PendingCalibration pending = std::move(*m_pending_calibration);
    m_pending_calibration.reset();
    if (!confirmed)
        return;
    // Preferences › Agent over the screen the row was pressed on. Closed with
    // an agent, the same question comes back with Start; closed without one,
    // the screen is as it was. Nothing is saved, closed or started meanwhile.
    if (pending.needs_agent) {
        open_preferences("agent");
        if (m_agent_pane->web_view().host().availability() == Agent::AgentAvailability::Ready)
            start_calibration(pending.printer, pending.filament, pending.spools);
        return;
    }
    restore_task_panel();
    if (m_tabpanel->GetSelection() == MainFrame::tpHome)
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
    m_agent_pane_user_collapsed = false;
    set_agent_pane_collapsed(false);
    // The app, not the Agent, opens the calibration project: the open one is
    // saved to its version history and released through the project_open
    // path, and the new one is named for what it calibrates.
    Workspace::ProjectOpenRequest open;
    open.new_project = true;
    open.name        = Agent::calibration_project_name(pending.filament, pending.printer);
    Workspace::LoadReport report;
    if (!m_workspace->open_project(open, report).succeeded()) {
        show_error(m_frame, _L("The calibration project could not be opened, so your project stays open."));
        return;
    }
    // A Home card's printer is the one the test prints on; there is no open
    // project to have it already.
    if (pending.filament.empty() && !SetupCommands::select_named_printer(*m_plater, pending.printer))
        show_error(m_frame, wxString::Format(_L("The printer \"%s\" could not be selected."), from_u8(pending.printer)));
    m_agent_pane->web_view().host().open_calibration_chat(
        {pending.project, pending.filament, pending.printer, {}, pending.spools}, /*start=*/true);
}

void ShellController::reload_agent_config()
{
    Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
    m_agent_pane->web_view().host().set_agent(std::move(runtime.service), runtime.availability);
}

namespace {

// Keys, the account and the agent's settings live in their own section; a
// change there is the one thing a JusPrin window over the app changes that
// the docked panel has to hear about.
std::map<std::string, std::string> agent_settings()
{
    const AppConfig& config = *wxGetApp().app_config;
    return config.has_section("jusprin_agent") ? config.get_section("jusprin_agent") : std::map<std::string, std::string>{};
}

} // namespace

void ShellController::open_welcome_back()
{
    const std::filesystem::path           data   = std::filesystem::u8path(data_dir());
    const std::optional<V1Import::Report> report = V1Import::read_report(data);
    if (!m_installed || m_preferences_open || !report || !report->pending)
        return;
    const auto agent_before = agent_settings();
    // One JusPrin window over the app at a time.
    m_preferences_open = true;
    {
        V1Import::WelcomeBackWindow window(m_frame, *m_theme, *m_workspace, *report);
        window.ShowModal();
    }
    m_preferences_open = false;
    V1Import::mark_shown(data);
    // The account check there stores what the server said about the account,
    // and a new sign-in replaces it; either way the docked agent rebuilds.
    if (agent_settings() != agent_before)
        reload_agent_config();
}

void ShellController::open_preferences(const std::string& section)
{
    if (!m_installed || m_preferences_open)
        return;
    const auto agent_before = agent_settings();

    m_preferences_open = true;
    Preferences::AfterClose after;
    {
        Preferences::PreferencesWindow window(m_frame, *m_theme, *m_workspace, m_agent_pane->web_view().host(),
                                              Preferences::OpenRequest{section});
        window.ShowModal();
        after = window.after_close();
    }
    m_preferences_open = false;

    // Reloading cancels whatever the docked agent is doing, so only when the
    // window changed its settings. An open printer or filament chat runs on
    // an agent of its own, read when it opened; it looks again too, so a chat
    // that was waiting for an agent continues where it was.
    if (agent_settings() != agent_before) {
        reload_agent_config();
        if (m_task_open && m_printer_panel != nullptr)
            m_printer_panel->reload_agent_config();
    }
    apply_current_appearance();
    // What OrcaSlicer does after its own dialog: the 3D view settings are
    // read as it draws, so a redraw shows them.
    if (m_plater != nullptr)
        m_plater->set_current_canvas_as_dirty();
    Preferences::run_after_close(after);
}

void ShellController::request_preferences(const std::string& section)
{
    if (m_preferences_requested)
        return;
    m_preferences_requested = true;
    m_frame->CallAfter([this, section] {
        m_preferences_requested = false;
        open_preferences(section);
    });
}

void ShellController::on_prepare_canvas_tab(SimpleEvent&)
{
}

void ShellController::on_frame_char_hook(wxKeyEvent& event)
{
    // Ctrl+, on Windows and Linux; the macOS app menu already gives Cmd+, to
    // Preferences.
#ifndef __APPLE__
    if (event.GetModifiers() == wxMOD_CONTROL && event.GetKeyCode() == ',') {
        request_preferences();
        return;
    }
#endif
    event.Skip();
}

// OrcaSlicer's own Calibration menu stays out of sight while the shell is
// installed: a calibration starts from the printer and filament menus or the
// chat, which make the test through the assistant. Uninstall puts it back.
// The title bar's button on Windows and Linux; a menu bar menu on macOS.
void ShellController::show_orca_calibration_menu(bool show)
{
#ifdef __APPLE__
    wxMenuBar* bar = m_frame->GetMenuBar();
    if (!show && m_orca_calibration_menu == nullptr) {
        m_orca_calibration_menu_at = bar->FindMenu(_L("Calibration"));
        if (m_orca_calibration_menu_at == wxNOT_FOUND)
            throw std::runtime_error("OrcaSlicer's Calibration menu is not on the menu bar");
        m_orca_calibration_menu = bar->Remove(m_orca_calibration_menu_at);
    } else if (show && m_orca_calibration_menu != nullptr) {
        bar->Insert(m_orca_calibration_menu_at, m_orca_calibration_menu, wxString::Format("&%s", _L("Calibration")));
        m_orca_calibration_menu = nullptr;
    }
#else
    m_frame->topbar()->ShowCalibrationButton(show);
#endif
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
    m_frame->Unbind(wxEVT_CHAR_HOOK, &ShellController::on_frame_char_hook, this);
    m_tabpanel->Unbind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ShellController::on_notebook_page_changed, this);

    m_prepare_canvas_presentation.detach();
    if (GLCanvas3D* prepare_canvas = m_plater->get_view3D_canvas3D())
        prepare_canvas->get_wxglcanvas()->Unbind(EVT_GLCANVAS_TAB, &ShellController::on_prepare_canvas_tab, this);
    m_plater->get_collapse_toolbar().set_enabled(m_saved_collapse_toolbar_enabled);
    m_plater->set_sidebar_available(true);
    m_plater->set_auto_preview_after_slice(m_saved_auto_preview_after_slice);
    wxGetApp().show_config_wizard_on_startup = m_saved_show_config_wizard_on_startup;
    show_orca_calibration_menu(true);
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
        m_agent_pane->web_view().host().tools().set_earlier_projects({}, {}, {});
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
    m_plater->set_external_file_open_handler({});
    m_plater->Unbind(wxEVT_IDLE, &ShellController::on_plater_idle, this);
    // A drop now takes OrcaSlicer's own path again; its own target comes
    // back with the next slice.
    m_plater->SetDropTarget(new PrepareDropTarget(*m_frame, *m_plater, {}));
    m_gcode_history.reset();
    m_autosave.reset();
    if (m_plater->get_project_name().empty())
        m_frame->SetTitle(m_saved_frame_title);
    else
        m_plater->update_title_dirty_status();
    Slic3r::set_backup_suspended(false);
    m_persistence.reset();
    m_work_feed.reset();
    m_issue_overlay.reset();
    m_print_issues.reset();
    m_workspace.reset();

    m_frame->Layout();
}

ShellController::OpenProject ShellController::open_project() const
{
    return {m_plater->project_state_session(), m_workspace->auxiliary_data_dir(), m_persistence->document().dump()};
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

void ShellController::open_print_history(const std::string& focus_hash)
{
    m_frame->CallAfter([this, focus_hash] {
        if (!m_installed || m_task_open || m_tabpanel->GetSelection() == MainFrame::tpHome)
            return;
        const Agent::ProjectStateDocument& document = m_persistence->document();
        if (!document.has_identity())
            throw std::logic_error("a Print history was asked for a project that has no identity");
        m_history_return_tab = m_tabpanel->GetSelection();
        m_home_screen        = nlohmann::json{{"screen", "history"}, {"projectId", document.project_id()}, {"back", "project"}};
        if (!focus_hash.empty())
            m_home_screen["focus"] = focus_hash;
        m_tabpanel->SetSelection(MainFrame::tpHome);
    });
}

void ShellController::leave_print_history()
{
    m_frame->CallAfter([this] {
        if (!m_installed || m_history_return_tab < 0)
            return;
        m_tabpanel->SetSelection(std::exchange(m_history_return_tab, -1));
    });
}

Agent::ToolExecutionCoordinator::PrintDialogOutcome
ShellController::open_print_dialog(const Agent::ToolExecutionCoordinator::PrintDialogRequest& request)
{
    using Outcome  = Agent::ToolExecutionCoordinator::PrintDialogOutcome;
    PresetBundle& presets = *wxGetApp().preset_bundle;
    const std::string dialog = presets.use_bbl_network() ? "select_machine" : "print_host";
    const Agent::ProjectStateDocument& document = m_persistence->document();

    if (!request.gcode.empty()) {
        std::vector<Agent::GcodeRecord> matches;
        for (const Agent::GcodeRecord& record : document.gcode_records())
            if (record.hash.rfind(request.gcode, 0) == 0)
                matches.push_back(record);
        if (matches.empty())
            return Outcome{"not_found", "This project's print history has no G-code " + request.gcode + ".", {}};
        if (matches.size() > 1)
            return Outcome{"invalid_arguments", "More than one G-code in the print history starts with " + request.gcode +
                                                    "; give more of its digits.", {}};
        switch (m_gcode_history->print_again_state(document.project_id(), matches.front())) {
        case GcodeHistoryRecorder::PrintAgain::Ready: break;
        case GcodeHistoryRecorder::PrintAgain::NotKept:
            return Outcome{"refused", "The project no longer keeps this G-code's file, so it can't be printed again as it was. "
                                      "Slice the plate again and print that.", {}};
        case GcodeHistoryRecorder::PrintAgain::NotForThisPrinter:
            return Outcome{"refused", "This G-code's file is not kept in the form the selected printer takes: a Bambu printer "
                                      "takes the package an earlier send to a Bambu printer left, and only some print hosts take "
                                      "a kept copy. Slice the plate again and print that.", {}};
        case GcodeHistoryRecorder::PrintAgain::NoPrinter:
            return Outcome{"no_printer", "No printer connection is set up for the selected printer.", {}};
        case GcodeHistoryRecorder::PrintAgain::Changed:
            return Outcome{"refused", "The file kept for this G-code changed on disk after it was kept, so JusPrin will not "
                                      "send it. Slice the plate again and print that.", {}};
        }
        print_again(document.project_id(), matches.front().hash);
        return Outcome{{}, {}, dialog};
    }

    // A plate's slice, the way the header's Print sends it.
    const auto* host = presets.printers.get_edited_preset().config.option<ConfigOptionString>("print_host");
    if (!presets.use_bbl_network() && (host == nullptr || host->value.empty()))
        return Outcome{"no_printer", "No printer connection is set up for the selected printer. The sliced file can be "
                                     "exported instead.", {}};
    m_frame->CallAfter([this, plate = *request.plate] {
        if (!m_installed)
            return;
        // The plate can go between the call and this turn.
        if (m_workspace->snapshot().active_plate != plate && !m_workspace->select_plate(plate).succeeded()) {
            MessageDialog(m_frame, _L("The plate to print is no longer in the project."), _L("Couldn't open the print dialog"),
                          wxOK | wxICON_INFORMATION)
                .ShowModal();
            return;
        }
        SimpleEvent print(EVT_GLTOOLBAR_PRINT_PLATE);
        m_plater->GetEventHandler()->ProcessEvent(print);
    });
    return Outcome{{}, {}, dialog};
}

std::vector<std::pair<std::string, std::vector<Agent::GcodeRecord>>> ShellController::gcode_records_by_project() const
{
    std::vector<std::pair<std::string, std::vector<Agent::GcodeRecord>>> records;
    // A projects folder that cannot be read has no records to read.
    if (const auto projects = m_autosave->projects())
        for (const auto& project : *projects)
            if (project.gcodes)
                records.emplace_back(project.id, *project.gcodes);
    return records;
}

void ShellController::print_again(const std::string& project_id, const std::string& hash)
{
    m_frame->CallAfter([this, project_id, hash] {
        if (!m_installed)
            return;
        // The page drew its button from a state that can move before the
        // click lands (another printer selected, a copy pruned): it is read
        // again, and a page that is behind is brought up to date instead.
        Agent::GcodeRecord record;
        for (const auto& [id, records] : gcode_records_by_project())
            for (const Agent::GcodeRecord& candidate : records)
                if (id == project_id && candidate.hash == hash)
                    record = candidate;
        if (record.hash.empty() ||
            m_gcode_history->print_again_state(project_id, record) != GcodeHistoryRecorder::PrintAgain::Ready) {
            m_home->refresh();
            return;
        }
        m_gcode_history->print_again(project_id, hash);
    });
}

void ShellController::open_in_chat(const std::string& project_id, const std::string& conversation_id, const std::string& item_id)
{
    m_frame->CallAfter([this, project_id, conversation_id, item_id] {
        if (!m_installed)
            return;
        const Agent::ProjectStateDocument& document = m_persistence->document();
        if (!document.has_identity() || document.project_id() != project_id) {
            if (!m_autosave->open_managed_project(project_id)) {
                MessageDialog(m_frame, _L("The project couldn't be opened. Try again."), _L("Couldn't open project"),
                              wxOK | wxICON_ERROR)
                    .ShowModal();
                return;
            }
        }
        m_history_return_tab = -1;
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        if (m_agent_pane_collapsed)
            toggle_agent_pane();
        // A chat can be deleted after a G-code went out from it.
        if (!m_agent_pane->web_view().host().reveal_item(conversation_id, item_id))
            MessageDialog(m_frame, _L("The chat this G-code went out from is no longer in the project."), _L("Chat not found"),
                          wxOK | wxICON_INFORMATION)
                .ShowModal();
    });
}

void ShellController::open_filament_help(std::size_t slot, const std::string& preset, const std::string& shown)
{
    if (m_task_open || m_printer_panel == nullptr || m_tabpanel == nullptr)
        return;
    if (!preset_exists(Preset::TYPE_FILAMENT, preset)) {
        show_error(m_frame, wxString::Format(_L("The filament preset \"%s\" was not found."), from_u8(preset)));
        return;
    }
    // Tab no longer reaches Preview from the shell, but OrcaSlicer's Ctrl+R
    // (slice and show the preview) still does.
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
    // Coming onto the screen, Home shows its gallery, or the Print history
    // the project asked for on the way.
    if (home && !m_home_shown)
        m_home->show_screen(std::exchange(m_home_screen, nlohmann::json{{"screen", "home"}}));
    m_home_shown = home;
    m_home->set_live(home);
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
    // Before the shell checks below, so the classic presentation gets this
    // too: JusPrin checks for its own new versions, and OrcaSlicer's check,
    // which asks OrcaSlicer's server about OrcaSlicer releases, never runs.
    // Its startup call is left to JusPrin's own schedule; Help > Check for
    // Updates checks at once and, with the shell on, shows Preferences >
    // Updates, where the result appears.
    if (wxGetApp().is_editor()) {
        Updates::update_check().attach(frame);
        wxGetApp().app_update_check = [](int by_user) {
            if (by_user == 0)
                return;
            Updates::update_check().check_now();
            if (ShellController* shell = installed_shell())
                shell->open_preferences("updates");
        };
        // Built-in printer and filament profiles change only with a JusPrin
        // release, so OrcaSlicer's profile server is never asked about them,
        // nor Bambu's server for its printer configuration bundle. All three
        // of OrcaSlicer's checks (profiles at startup and on switching to
        // another maker's printer, the bundle at startup) read this preference.
        if (wxGetApp().app_config != nullptr)
            wxGetApp().app_config->set_bool("sync_system_preset", false);
        // A file opened from the OS lands in the running JusPrin, and a .3mf
        // opens as a project without OrcaSlicer's "open as project or import
        // geometry?" question (architecture.md, "Opening and importing
        // files"). OrcaSlicer reads single_instance at startup before this
        // runs, so a second launch takes it from the value stored here; a
        // file opened while JusPrin runs reads it when the open request comes in.
        if (wxGetApp().app_config != nullptr) {
            wxGetApp().app_config->set_bool("single_instance", true);
            wxGetApp().app_config->set(SETTING_PROJECT_LOAD_BEHAVIOUR, OPTION_PROJECT_LOAD_BEHAVIOUR_LOAD_ALL);
            // A project keeps the printer it was saved for, also when a
            // different Bambu printer is connected: OrcaSlicer's stored "No"
            // to its "sync the printer and switch the preset?" question, so
            // it is never asked, on a file open or on reopening a saved
            // project from Home.
            wxGetApp().app_config->set_bool("sync_after_load_file_show_flag", false);
        }
    }
    if (tabpanel == nullptr || main_sizer == nullptr)
        return;
    if (!wxGetApp().is_editor())
        return;
    // A language change builds the next frame while the previous one, which
    // may be in classic view, is still alive; its way back goes now.
    classic_view_slot().reset();
    left_project().reset();
    attach_point() = {&frame, tabpanel, main_sizer};
    if (wxGetApp().app_config != nullptr && wxGetApp().app_config->get("jusprin_shell") == "0") {
        install_classic_view();
        return;
    }
    if (install_shell())
        install_classic_view();
}

ShellController* installed_shell()
{
    return shell_slot().get();
}

void detach_shell()
{
    // On macOS a new web view installs its script message handler from a
    // CallAfter (WebView::CreateWebView), and wx waits for WebKit's reply to
    // that install inside a nested event loop, with the app's
    // is_adding_script_handler() flag set until it returns. A switch chosen
    // just after the shell came back can run inside that wait, and the
    // uninstall would free the shell's web views under the install. The
    // switch waits for the install to end instead, as GUI_App::run_wizard
    // and PrinterPanel do.
    if (shell_slot() != nullptr && wxGetApp().is_adding_script_handler()) {
        auto* retry = new wxTimer();
        retry->Bind(wxEVT_TIMER, [retry](wxTimerEvent&) {
            // A frame destroyed meanwhile has left the shell uninstalled.
            if (shell_slot() != nullptr && shell_slot()->is_installed())
                detach_shell();
            // Last: this handler goes with the timer.
            delete retry;
        });
        retry->StartOnce(kDetachRetryMs);
        return;
    }
    if (shell_slot() != nullptr) {
        left_project() = shell_slot()->open_project();
        shell_slot()->uninstall();
        shell_slot().reset();
        install_classic_view();
    }
}

void reattach_shell()
{
    std::unique_ptr<ClassicViewReturn>& classic = classic_view_slot();
    if (classic == nullptr)
        throw std::logic_error("the JusPrin view was asked back while it is not in classic view");
    classic.reset();
    if (const std::optional<std::string> failure = install_shell()) {
        install_classic_view();
        // A person pressed a button; unlike a failed start, this one is told.
        MessageDialog(attach_point().frame,
                      _L("JusPrin's view couldn't be opened.") + "\n" + wxString::FromUTF8(*failure),
                      wxEmptyString, wxOK | wxICON_ERROR).ShowModal();
    }
}

ClassicViewReturn* installed_classic_view()
{
    return classic_view_slot().get();
}

} // namespace Slic3r::GUI::JusPrin
