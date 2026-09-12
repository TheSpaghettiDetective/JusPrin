#pragma once

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"

#include <memory>
#include <wx/event.h>
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
class ShellTheme;
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

    // Closing the Agent panel hides it and its divider; the panel keeps its
    // conversation, its loaded page, and its MCP runtime while hidden, and
    // reopening restores the width it last held this session.
    void set_agent_pane_collapsed(bool collapsed);
    void toggle_agent_pane() { set_agent_pane_collapsed(!m_agent_pane_collapsed); }
    bool is_agent_pane_collapsed() const { return m_agent_pane_collapsed; }

private:
    void on_frame_destroy(wxWindowDestroyEvent& event);
    void on_frame_size(wxSizeEvent& event);
    // The width the pane may hold right now: at least its own minimum, and no
    // more than what the frame can spare beside a usable workspace.
    int  agent_pane_width_within(int width) const;
    void request_agent_pane_width(int width);
    void apply_agent_pane_width();

    const ShellTheme* m_theme{nullptr};
    wxTimer m_runtime_timer{this};

    MainFrame* m_frame{nullptr};
    Notebook*  m_tabpanel{nullptr};
    wxSizer*   m_main_sizer{nullptr};
    Plater*    m_plater{nullptr};

    StatusRow* m_status_row{nullptr};
    AgentPane* m_agent_pane{nullptr};
    wxWindow* m_agent_resize_handle{nullptr};
    wxBoxSizer* m_center_sizer{nullptr};
    int m_agent_pane_preferred_width{0};
    bool m_agent_pane_collapsed{false};

    // The one workspace projection consumed by the Agent bridge. It must be
    // constructed before the AgentPane and outlive it.
    std::unique_ptr<Workspace::OrcaWorkspaceAdapter> m_workspace;
    // Project-owned conversation state; constructed
    // after the workspace and before the pane, destroyed in reverse.
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;

    bool m_installed{false};
    bool m_saved_collapse_toolbar_enabled{false};
    bool m_saved_auto_preview_after_slice{true};
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
