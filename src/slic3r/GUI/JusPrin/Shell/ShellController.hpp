#pragma once

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"

#include <memory>
#include <functional>
#include <string>
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
}

namespace Slic3r::GUI::JusPrin {

class AgentPane;
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

    // Throws on failure after restoring any partial change; the caller may
    // then continue with the untouched stock presentation.
    void install(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer);
    void uninstall();
    bool is_installed() const { return m_installed; }

    StatusRow* status_row() const { return m_status_row; }
    Home::HomeWebView* home_view() const { return m_home; }
    PrinterSetup::PrinterPanel* printer_panel() const { return m_printer_panel; }
    AgentPane* agent_pane() const { return m_agent_pane; }
    LeftPane* left_pane() const { return m_left_pane; }
    // The Prepare canvas's presentation, for the integration harness to drive
    // the tool strip and the value card the way a pointer does.
    const CanvasPresentationController& prepare_canvas_presentation() const { return m_prepare_canvas_presentation; }
    Workspace::IWorkspace* workspace() const { return m_workspace.get(); }
    Agent::ProjectPersistence* persistence() const { return m_persistence.get(); }
    Workspace::ProjectAutosave* autosave() const { return m_autosave.get(); }
    void set_reimport_confirmation(std::function<ReimportDecision()> confirmation)
        { m_reimport_confirmation = std::move(confirmation); }

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
    // Takes the person to the Agent panel's own setup flow from anywhere in
    // the shell: leaves Home for the workspace, opens the panel as their
    // choice, and asks the page to open setup.
    void open_agent_setup();

    // A throwaway AgentWebView embedded elsewhere (the Add a printer dialog's
    // own setup flow) just wrote a working agent config that the docked
    // pane's own AgentHost instance has no way to learn about on its own.
    // on_page_changed() re-derives and pushes fresh availability into the
    // docked pane the next time it would become relevant again, the same way
    // it already refreshes Home's gallery on return.
    void mark_agent_config_possibly_changed() { m_agent_config_possibly_changed = true; }

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
    void on_frame_destroy(wxWindowDestroyEvent& event);
    // Applies what the Notebook's current page implies for the shell: Home
    // refreshes its gallery and takes the Agent panel off the screen.
    void on_page_changed();
    void on_notebook_page_changed(wxBookCtrlEvent& event);
    void on_frame_size(wxSizeEvent& event);
    void show_task_panel();
    void restore_task_panel();
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
    // Set by mark_agent_config_possibly_changed(), consumed by on_page_changed().
    bool m_agent_config_possibly_changed{false};
    bool m_task_open{false};
    int  m_task_origin_tab{-1};

    // The one workspace projection consumed by the Agent bridge. It must be
    // constructed before the AgentPane and outlive it.
    std::unique_ptr<Workspace::OrcaWorkspaceAdapter> m_workspace;
    // Project-owned conversation state; constructed
    // after the workspace and before the pane, destroyed in reverse.
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    std::unique_ptr<Workspace::ProjectAutosave> m_autosave;
    std::string m_autosave_status_key;
    std::string m_timeline_version_head;
    wxString m_saved_frame_title;

    bool m_installed{false};
    bool m_saved_collapse_toolbar_enabled{false};
    bool m_saved_auto_preview_after_slice{true};
    bool m_saved_show_config_wizard_on_startup{true};
    CanvasPresentationController m_prepare_canvas_presentation;
};

// The one MainFrame attachment point. Decides whether the shell should be
// installed for this session, installs it, and falls back to the untouched
// stock presentation when installation fails. Never throws.
void attach_shell(MainFrame& frame, Notebook* tabpanel, wxSizer* main_sizer);

// The controller installed by attach_shell, if any (used by tests and later
// phases; returns nullptr in stock mode).
ShellController* installed_shell();

// Removes the shell installed by attach_shell and restores the stock
// presentation. Safe to call when no shell is installed.
void detach_shell();

} // namespace Slic3r::GUI::JusPrin
