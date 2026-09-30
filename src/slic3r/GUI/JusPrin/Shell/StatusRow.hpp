#pragma once

#include "FilamentMenu.hpp"
#include "ShellTheme.hpp"
#include "PrimaryPrintAction.hpp"

#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"

#include <wx/panel.h>
#include <wx/weakref.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

class wxWindowDestroyEvent;
class Notebook;

namespace Slic3r::GUI {
class Plater;
}

namespace Slic3r::GUI::JusPrin::Agent {
class ProjectPersistence;
}

namespace Slic3r::GUI::JusPrin::Workspace {
class ProjectAutosave;
}

namespace Slic3r::GUI::JusPrin {
class HeaderButton;
class PrinterFilamentChip;

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
              Agent::ProjectPersistence& persistence);
    ~StatusRow() override;

    void apply_appearance(bool dark);
    void refresh();

    // Native behavior entry points, shared by the controls and native harness.
    void request_slice(bool all = false);
    void request_prepare();
    void request_action(PrintAction action);
    PrintActionState action_state() const;
    void request_home();
    // Closes the Agent panel if it is open and opens it if it is closed.
    // Does nothing until ShellController has supplied the toggle.
    void toggle_agent_pane();
    void show_action_menu();
    void show_overflow_menu();
    void show_version_history(std::size_t offset = 0);
    void show_recent_projects(std::size_t offset = 0);

    // Chip entry points, shared by the controls and the native harness, so a
    // filament change can be driven without a pointer.
    void open_printer_menu();
    void open_filament_menu();
    // What ends a filament menu visit: the chip re-reads Orca, and a visit
    // that changed a filament leaves one line in the thread.
    void on_filament_visit(const FilamentMenu::Visit& visit);
    wxString project_summary() const;
    void set_autosave(Workspace::ProjectAutosave* autosave) { m_autosave = autosave; refresh(); }
    void set_restore_confirmation(std::function<bool()> confirmation)
        { m_restore_confirmation = std::move(confirmation); }

    // Where the after-change chat line goes, with the colour that leads it
    // ("#RRGGBB", or empty). Supplied by ShellController once the Agent pane
    // exists, so the header does not depend on the Agent host.
    void set_note_sink(std::function<void(const wxString&, const wxString&)> sink) { m_note_sink = std::move(sink); }

    // The Agent-panel toggle, supplied by ShellController for the same reason
    // as the note sink: the header drives the panel without depending on it.
    // Its button stays hidden until a toggle is supplied, so a header built
    // without a panel shows no control for one.
    void set_agent_pane_toggle(std::function<void()> toggle);
    void set_agent_pane_collapsed(bool collapsed);

private:
    void refresh_chip();
    void layout_header();
    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t> print_target_identity() const;
    wxString action_label(PrintAction action, bool primary = false) const;
    void on_slice_status_changed(wxCommandEvent& event);
    void on_tabpanel_destroyed(wxWindowDestroyEvent& event);

    const ShellTheme&          m_theme;
    Plater&                    m_plater;
    Notebook&                  m_tabpanel;
    Agent::ProjectPersistence& m_persistence;
    Workspace::ProjectAutosave* m_autosave{nullptr};

    HeaderButton*        m_home_button{nullptr};
    PrinterFilamentChip* m_chip{nullptr};
    HeaderButton* m_slice_button{nullptr};
    HeaderButton* m_menu_button{nullptr};
    HeaderButton* m_overflow_button{nullptr};
    HeaderButton* m_agent_toggle{nullptr};

    std::function<void(const wxString&, const wxString&)> m_note_sink;
    std::function<bool()> m_restore_confirmation;
    std::function<void()>                                 m_agent_pane_toggle;

    ProjectStateSubscription m_project_state_subscription;
    bool                     m_dark{false};
    bool                     m_tabpanel_alive{true};
};

} // namespace Slic3r::GUI::JusPrin
