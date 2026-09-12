#pragma once

#include "ShellTheme.hpp"
#include "PrimaryPrintAction.hpp"

#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"

#include <wx/panel.h>
#include <wx/weakref.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

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
class PrinterSpoolChip;

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

    // Chip entry points, shared by the controls and the native harness, so the
    // two-click swap can be driven without a pointer.
    void open_printer_menu();
    void open_spool_menu();
    // Applies a remembered spool by ID exactly as picking its row would,
    // including the recency stamp and the chat line. Returns false when the ID
    // is not a spool of the current printer.
    bool select_spool(const std::string& spool_id);
    // The spools the chip would list right now, most recently used first.
    std::vector<Workspace::Spool> listed_spools();
    // Remembers a spool for the current printer, as the spool menu's
    // "Use this spool" step does, without selecting it.
    Workspace::Spool remember_spool(const std::string& filament_preset, const std::string& colour,
                                    const std::string& name);
    // What the chip's two halves currently read, for harness assertions.
    wxString printer_text() const;
    wxString spool_text() const;
    wxString project_summary() const;

    // The spool the project currently corresponds to, seeding one for a
    // printer that has none so the chip never shows an empty right half.
    std::optional<Workspace::Spool> current_spool();

    // Where the after-swap chat line goes. Supplied by ShellController once
    // the Agent pane exists, so the header does not depend on the Agent host.
    void set_note_sink(std::function<void(const wxString&)> sink) { m_note_sink = std::move(sink); }

    // The Agent-panel toggle, supplied by ShellController for the same reason
    // as the note sink: the header drives the panel without depending on it.
    // Its button stays hidden until a toggle is supplied, so a header built
    // without a panel shows no control for one.
    void set_agent_pane_toggle(std::function<void()> toggle);
    void set_agent_pane_collapsed(bool collapsed);

private:
    void on_spool_selected(const Workspace::Spool& spool);
    void refresh_chip();
    void layout_header();
    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t> print_target_identity() const;
    wxString action_label(PrintAction action, bool primary = false) const;
    void on_slice_status_changed(wxCommandEvent& event);
    void on_tab_changed(wxBookCtrlEvent& event);
    void on_tabpanel_destroyed(wxWindowDestroyEvent& event);

    const ShellTheme&          m_theme;
    Plater&                    m_plater;
    Notebook&                  m_tabpanel;
    Agent::ProjectPersistence& m_persistence;

    HeaderButton*     m_home_button{nullptr};
    PrinterSpoolChip* m_chip{nullptr};
    HeaderButton* m_slice_button{nullptr};
    HeaderButton* m_menu_button{nullptr};
    HeaderButton* m_overflow_button{nullptr};
    HeaderButton* m_agent_toggle{nullptr};

    // Remembered spools are machine facts, so they live beside the
    // application data rather than in the project archive.
    std::unique_ptr<Workspace::SpoolStore> m_spools;
    std::function<void(const wxString&)>   m_note_sink;
    std::function<void()>                  m_agent_pane_toggle;

    ProjectStateSubscription m_project_state_subscription;
    bool                     m_dark{false};
    bool                     m_tabpanel_alive{true};
};

} // namespace Slic3r::GUI::JusPrin
