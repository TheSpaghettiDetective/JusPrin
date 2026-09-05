#pragma once

#include "ShellTheme.hpp"
#include "PrimaryPrintAction.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceReview.hpp"

#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"

#include <wx/panel.h>
#include <memory>

class wxBookCtrlEvent;
class wxWindowDestroyEvent;
class Notebook;

namespace Slic3r::GUI {
class Plater;
}

namespace Slic3r::GUI::JusPrin::Agent {
class ProjectPersistence;
}

namespace Slic3r::GUI::JusPrin {
class HeaderButton;

// Home navigation, a centered setup selector, and right-aligned print actions.
// Project identity and physical-print count live in the overflow menu. It
// renders authoritative Orca state and drives the same event paths as the
// stock controls; it owns no project state of its own.
class StatusRow : public wxPanel
{
public:
    StatusRow(wxWindow*                  parent,
              const ShellTheme&          theme,
              Plater&                    plater,
              Notebook&                  tabpanel,
              Agent::ProjectPersistence& persistence,
              std::shared_ptr<Workspace::SliceReviews> reviews);
    ~StatusRow() override;

    void apply_appearance(bool dark);
    void refresh();

    // Native behavior entry points, shared by the controls and native harness.
    void request_slice(bool all = false);
    void request_check_print();
    void request_prepare();
    void request_action(PrintAction action);
    PrintActionState action_state() const;
    Workspace::SliceIdentity slice_identity() const;
    void request_home();
    void show_action_menu();
    void show_setup_menu();
    void show_overflow_menu();
    wxString project_summary() const;

private:
    void layout_header();
    wxString action_label(PrintAction action, bool primary = false) const;
    void on_slice_status_changed(wxCommandEvent& event);
    void on_tab_changed(wxBookCtrlEvent& event);
    void on_tabpanel_destroyed(wxWindowDestroyEvent& event);

    const ShellTheme&          m_theme;
    Plater&                    m_plater;
    Notebook&                  m_tabpanel;
    Agent::ProjectPersistence& m_persistence;
    std::shared_ptr<Workspace::SliceReviews> m_reviews;

    HeaderButton* m_home_button{nullptr};
    HeaderButton* m_setup_chip{nullptr};
    HeaderButton* m_slice_button{nullptr};
    HeaderButton* m_menu_button{nullptr};
    HeaderButton* m_overflow_button{nullptr};

    ProjectStateSubscription m_project_state_subscription;
    bool                     m_dark{false};
    bool                     m_tabpanel_alive{true};
};

} // namespace Slic3r::GUI::JusPrin
