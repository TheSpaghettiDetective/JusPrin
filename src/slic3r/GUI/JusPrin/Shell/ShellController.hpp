#pragma once

#include "ShellTheme.hpp"

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"

#include <memory>
#include <wx/event.h>
#include <wx/timer.h>

class wxSizer;
class wxBoxSizer;
class Notebook;

namespace Slic3r::GUI {
class MainFrame;
class Plater;
}

namespace Slic3r::GUI::JusPrin {

class AgentPane;
class StatusRow;

// Installs the JusPrin production presentation inside the existing MainFrame
// layout and can restore the stock presentation exactly. The stock widget
// hierarchy stays constructed and functional: the Notebook keeps its pages and
// selection flow, only its tab strip is hidden, and the Plater sidebar is held
// hidden through Plater's sidebar-availability policy.
class ShellController : public wxEvtHandler
{
public:
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
    AgentPane* agent_pane() const { return m_agent_pane; }
    Workspace::IWorkspace* workspace() const { return m_workspace.get(); }
    Agent::ProjectPersistence* persistence() const { return m_persistence.get(); }

    void apply_current_appearance();

private:
    void on_frame_destroy(wxWindowDestroyEvent& event);

    ShellTheme m_theme;
    wxTimer m_runtime_timer{this};

    MainFrame* m_frame{nullptr};
    Notebook*  m_tabpanel{nullptr};
    wxSizer*   m_main_sizer{nullptr};
    Plater*    m_plater{nullptr};

    StatusRow* m_status_row{nullptr};
    AgentPane* m_agent_pane{nullptr};
    wxBoxSizer* m_center_sizer{nullptr};

    // The one workspace projection consumed by the Agent bridge. It must be
    // constructed before the AgentPane and outlive it.
    std::unique_ptr<Workspace::OrcaWorkspaceAdapter> m_workspace;
    // Project-owned conversation state and revision checkpoints; constructed
    // after the workspace and before the pane, destroyed in reverse.
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;

    bool m_installed{false};
    bool m_saved_collapse_toolbar_enabled{false};
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
