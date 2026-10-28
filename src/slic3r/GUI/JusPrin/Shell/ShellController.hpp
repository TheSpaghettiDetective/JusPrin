#pragma once

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/PrintIssues/IssueContext.hpp"
#include "slic3r/GUI/JusPrin/PrintIssues/IssueOverlay.hpp"
#include "slic3r/GUI/JusPrin/PrintIssues/PrintIssueMonitor.hpp"
#include "slic3r/GUI/JusPrin/PrintIssues/WorkFeed.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"

#include <boost/filesystem/path.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <functional>
#include <string>
#include <vector>
#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

class wxSizer;
class wxBoxSizer;
class wxWindow;
class Notebook;

namespace Slic3r::GUI {
class MainFrame;
class Plater;
struct SimpleEvent;
}

namespace Slic3r::GUI::JusPrin {

class AgentPane;
class ClassicViewReturn;
class LeftPane;
class ShellTheme;
class StatusRow;

namespace Home {
class HomeWebView;
}

namespace PrinterSetup {
class PrinterPanel;
}

// Installs the JusPrin production presentation inside the existing MainFrame
// layout and can restore the stock presentation exactly. The stock widget
// hierarchy stays constructed and functional: the Notebook keeps its pages and
// selection flow, only its tab strip is hidden, and the Plater sidebar is held
// hidden through Plater's sidebar-availability policy.
class ShellController : public wxEvtHandler
{
public:
    enum class ReimportDecision { OpenExisting, CreateNew, Cancel };
    ShellController();
    ~ShellController();

    ShellController(const ShellController&) = delete;
    ShellController& operator=(const ShellController&) = delete;

    // The open project's conversation as the shell leaves it, and what tells
    // that project apart from the next one OrcaSlicer opens or starts.
    struct OpenProject
    {
        std::uint64_t project_session{0};
        std::string   auxiliary_dir;
        std::string   state;
    };

    // Throws on failure after restoring any partial change; the caller may
    // then continue with the untouched stock presentation. A shell installed
    // over the project an earlier one left takes up its conversation.
    void install(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer,
                 const std::optional<OpenProject>& left = std::nullopt);
    void uninstall();
    OpenProject open_project() const;
    bool is_installed() const { return m_installed; }

    StatusRow* status_row() const { return m_status_row; }
    Home::HomeWebView* home_view() const { return m_home; }
    PrinterSetup::PrinterPanel* printer_panel() const { return m_printer_panel; }
    AgentPane* agent_pane() const { return m_agent_pane; }
    PrintIssues::WorkFeed* work_feed() const { return m_work_feed.get(); }
    bool agent_pane_collapsed() const { return m_agent_pane_collapsed; }
    LeftPane* left_pane() const { return m_left_pane; }
    // The Prepare canvas's presentation, for the integration harness to drive
    // the tool strip and the value card the way a pointer does.
    const CanvasPresentationController& prepare_canvas_presentation() const { return m_prepare_canvas_presentation; }
    Workspace::IWorkspace* workspace() const { return m_workspace.get(); }

    // What OrcaSlicer currently reports about the active plate, and its
    // pill, list and bubble on the Prepare canvas. Null while not installed.
    PrintIssues::PrintIssueMonitor* print_issues() const { return m_print_issues.get(); }
    PrintIssues::IssueOverlay*      issue_overlay() const { return m_issue_overlay.get(); }
    struct IssueSubmission
    {
        // "started", "queued", "duplicate", or why nothing was sent:
        // "expired" (OrcaSlicer no longer reports the issue), "unchecked"
        // (it has not validated the latest edit), "agent_not_set_up" (there
        // is no assistant to answer), "busy" (a reply is being written), or
        // the chat's own refusal code.
        std::string               outcome;
        PrintIssues::IssueMessage message;
    };
    // Sends the issue to the project chat as the person's own message and
    // opens the chat pane. The issue is looked up again at the moment of the
    // press, so one that has cleared or gone stale since it was drawn is not sent.
    IssueSubmission submit_issue(const std::string& issue_id, const std::string& request_sentence);
    // A Calibrate… row (printer chip, filament slot, Home printer card):
    // board 3's one popup first, since the open project closes; on Start
    // the app opens the calibration project and its chat. filament is empty
    // for a Home printer card, whose loaded spools (as it reports them) the
    // Agent offers instead.
    void start_calibration(const std::string& printer, const std::string& filament,
                           const std::vector<std::string>& spools = {});
    // The page's answer to start_calibration's question.
    void answer_calibration(const std::string& request_id, bool confirmed);
    Agent::ProjectPersistence* persistence() const { return m_persistence.get(); }
    Workspace::ProjectAutosave* autosave() const { return m_autosave.get(); }
    void set_reimport_confirmation(std::function<ReimportDecision()> confirmation)
        { m_reimport_confirmation = std::move(confirmation); }
    // How a file is opened (architecture.md, "Opening and
    // importing files"). Dropped on the Prepare canvas, a model file is added
    // to the open project; the canvas's drop target calls add_dropped_files.
    // Home's Open… and a drop on Home call open_files_as_new_project: a model
    // file (or a zip of them) opens in a new project named after it. A .3mf
    // opens as a project either way, through OrcaSlicer's load_project and
    // its "Previously opened file" question. The harness calls both.
    bool add_dropped_files(const wxArrayString& files);
    bool open_files_as_new_project(const wxArrayString& files);

    void apply_current_appearance();

    // Closing the Agent panel hides it and its divider; the panel keeps its
    // conversation, its loaded page, and its MCP runtime while hidden, and
    // reopening restores the width it last held this session.
    void set_agent_pane_collapsed(bool collapsed);
    // A person's own choice, remembered across the visits to Home that
    // collapse the pane on their behalf.
    void toggle_agent_pane()
    {
        m_agent_pane_user_collapsed = !m_agent_pane_collapsed;
        set_agent_pane_collapsed(m_agent_pane_user_collapsed);
    }
    bool is_agent_pane_collapsed() const { return m_agent_pane_collapsed; }
    // Mirrored left-panel treatment: the open copy lives in its own header
    // row; the collapsed copy lives at the same screen edge in Project header.
    void set_left_pane_collapsed(bool collapsed);
    void toggle_left_pane() { set_left_pane_collapsed(!m_left_pane_collapsed); }
    bool is_left_pane_collapsed() const { return m_left_pane_collapsed; }
    // JusPrin's Preferences window, over whatever is on screen; returns once
    // it is closed. `section` names where it opens (empty: Account). Its
    // Agent section is the one place the agent is set up: every surface that
    // finds none opens it there, and picks the agent up when it closes.
    void open_preferences(const std::string& section = {});
    // Opens it on the frame's next turn, for a caller that must not run a
    // modal window itself (a web view's message handler, a key handler).
    // Requests that arrive before it opens open it once.
    void request_preferences(const std::string& section = {});
    // The window shown once after JusPrin v1's setup was imported at this
    // start (V1Import.hpp); returns once it is closed. Nothing when there is
    // no import still to show.
    void open_welcome_back();

    // Orca's own printer wizard ("set it up myself") runs from a CallAfter,
    // decoupled from the add-printer request that opened it, so nothing else
    // tells Home about the printer it names. The caller asks for this refresh
    // once the wizard flow (and any naming it does) has actually finished.
    // `added` is a successful Add's saved facts, forwarded so Home can lead
    // its column with that printer and say what it assumed; every other
    // caller passes none.

    // Opens a fresh temporary task chat over the current workspace. The
    // Notebook keeps its selection, so Back restores the same Home, Prepare,
    // or Preview screen without rebuilding it.
    void open_printer_conversation(const std::string& printer_name = {}, bool connect = false);
    // Opens a filament's chat for one slot. `preset` is the slot's filament
    // preset by name; `shown` is what the header calls it.
    void open_filament_help(std::size_t slot, const std::string& preset, const std::string& shown);

private:
    // OrcaSlicer's load_files(wxArrayString) hands a person's files here
    // first (Plater::set_external_file_open_handler): files from the OS,
    // another instance, the command line, a download, and the two calls
    // above. True when it opened them in a new project, or the open stopped;
    // false leaves them to OrcaSlicer, which opens a .3mf as a project and
    // adds the rest.
    bool take_opened_files(const std::vector<boost::filesystem::path>& files);
    // OrcaSlicer puts its own drop target back after every slice; the
    // canvas's is put back on the next idle turn.
    void on_plater_idle(wxIdleEvent& event);
    void install_drop_target();
    bool m_adding_files{false};
    // File > Import > Import Zip Archive goes through load_files too, so a
    // lone zip adds unless it came through open_files_as_new_project.
    bool m_zip_opens_project{false};
    void on_frame_destroy(wxWindowDestroyEvent& event);
    // Applies what the Notebook's current page implies for the shell: Home
    // refreshes its gallery and takes the Agent panel off the screen.
    void on_page_changed();
    void on_notebook_page_changed(wxBookCtrlEvent& event);
    void on_frame_size(wxSizeEvent& event);
    void on_frame_char_hook(wxKeyEvent& event);
    // Takes Tab on the Prepare canvas, which OrcaSlicer would answer by
    // switching to Preview, and does nothing with it.
    void on_prepare_canvas_tab(SimpleEvent& event);
    // Gives the docked panel the agent the stored settings describe now.
    void reload_agent_config();
    void show_task_panel();
    void restore_task_panel();
    // The calibration a Calibrate… row asked about, until the page answers.
    struct PendingCalibration
    {
        std::string request_id, printer, filament, project;
        std::vector<std::string> spools;
        bool        needs_agent{false}; // asked with "Set up the agent…" in place of Start
    };
    std::optional<PendingCalibration> m_pending_calibration;
    // The width the pane may hold right now: at least its own minimum, and no
    // more than what the frame can spare beside a usable workspace.
    int  agent_pane_width_within(int width) const;
    void request_agent_pane_width(int width);
    void apply_agent_pane_width();
    int  left_pane_width_within(int width) const;
    void request_left_pane_width(int width);
    void apply_left_pane_width();

    const ShellTheme* m_theme{nullptr};
    wxTimer m_runtime_timer{this};

    MainFrame* m_frame{nullptr};
    Notebook*  m_tabpanel{nullptr};
    wxSizer*   m_main_sizer{nullptr};
    Plater*    m_plater{nullptr};
    std::function<ReimportDecision()> m_reimport_confirmation;

    StatusRow* m_status_row{nullptr};
    AgentPane* m_agent_pane{nullptr};
    // The Agent page owns the open-pane toggle in its existing header. The
    // project header owns the collapsed copy; only one is visible at a time.
    // The Plates / Project pane, left of the workspace; shown on Prepare only.
    LeftPane*  m_left_pane{nullptr};
    // Shown in the Notebook's slot while the Notebook's selection is tpHome.
    Home::HomeWebView* m_home{nullptr};
    // Fresh in-memory printer or filament task chat, shown over the workspace.
    PrinterSetup::PrinterPanel* m_printer_panel{nullptr};
    wxWindow* m_left_resize_handle{nullptr};
    wxWindow* m_agent_resize_handle{nullptr};
    wxBoxSizer* m_center_sizer{nullptr};
    // The project header and canvas share the center column. The Plates /
    // Project and Agent panes are independent full-height columns beside it.
    wxBoxSizer* m_project_sizer{nullptr};
    // Home and the Notebook share this slot: exactly one is shown.
    wxBoxSizer* m_workspace_sizer{nullptr};
    int m_left_pane_preferred_width{0};
    int m_agent_pane_preferred_width{0};
    bool m_left_pane_collapsed{true};
    bool m_agent_pane_collapsed{false};
    // What the person last asked for, as opposed to what Home imposes.
    bool m_agent_pane_user_collapsed{false};
    bool m_preferences_open{false};
    bool m_preferences_requested{false};
    // Waits for the main window to be up before the welcome-back window.
    std::unique_ptr<wxTimer> m_welcome_wait;
    bool m_task_open{false};
    int  m_task_origin_tab{-1};

    // The one workspace projection consumed by the Agent bridge. It must be
    // constructed before the AgentPane and outlive it.
    std::unique_ptr<Workspace::OrcaWorkspaceAdapter> m_workspace;
    // Project-owned conversation state; constructed
    // after the workspace and before the pane, destroyed in reverse.
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    std::unique_ptr<Workspace::ProjectAutosave> m_autosave;
    // The version pinned before a model turn's first edit; every later edit
    // of the turn links to it, and the turn's end clears it.
    std::string m_turn_before_version;
    std::string m_autosave_status_key;
    std::string m_timeline_version_head;
    std::uint64_t m_timeline_document_revision{0};
    wxString m_saved_frame_title;
    // OrcaSlicer's own Calibration menu, off the macOS menu bar while the
    // shell is installed, and where it stood.
    wxMenu* m_orca_calibration_menu{nullptr};
    int     m_orca_calibration_menu_at{wxNOT_FOUND};
    void show_orca_calibration_menu(bool show);

    bool m_installed{false};
    bool m_saved_collapse_toolbar_enabled{false};
    bool m_saved_auto_preview_after_slice{true};
    bool m_saved_show_config_wizard_on_startup{true};
    CanvasPresentationController m_prepare_canvas_presentation;
    std::unique_ptr<PrintIssues::PrintIssueMonitor> m_print_issues;
    std::unique_ptr<PrintIssues::IssueOverlay>      m_issue_overlay;
    std::unique_ptr<PrintIssues::WorkFeed>          m_work_feed;
};

// The one MainFrame attachment point. Decides whether the shell should be
// installed for this session, installs it, and falls back to the untouched
// stock presentation, with its way back to JusPrin, when installation fails.
// Throws only on a broken invariant.
void attach_shell(MainFrame& frame, Notebook* tabpanel, wxSizer* main_sizer);

// The controller installed by attach_shell, if any (used by tests and later
// phases; returns nullptr in stock mode).
ShellController* installed_shell();

// Removes the shell installed by attach_shell and restores the stock
// presentation with its way back to JusPrin. Safe to call when no shell is
// installed.
void detach_shell();

// From classic view, removes the way back and installs the shell again over
// the frame attach_shell last received. When that fails, the way back
// returns and the person is told why.
void reattach_shell();

// The way back while the shell is off, if any (used by tests; nullptr while
// the shell is installed).
ClassicViewReturn* installed_classic_view();

} // namespace Slic3r::GUI::JusPrin
