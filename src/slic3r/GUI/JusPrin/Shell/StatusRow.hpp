#pragma once

#include "ShellTheme.hpp"

#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"

#include <wx/panel.h>

class wxStaticText;
class wxBookCtrlEvent;
class wxWindowDestroyEvent;
class Button;
class Notebook;
class StaticBox;

namespace Slic3r::GUI {
class Plater;
}

namespace Slic3r::GUI::JusPrin {

// Compact top row of the JusPrin shell: project identity, a pointer to the
// configured printer/material state, and the Slice / Check print flow. It
// renders authoritative Orca state and drives the same event paths as the
// stock controls; it owns no project state of its own.
class StatusRow : public wxPanel
{
public:
    StatusRow(wxWindow* parent, const ShellTheme& theme, Plater& plater, Notebook& tabpanel);
    ~StatusRow() override;

    void apply_appearance(bool dark);
    void refresh();

    // The same dispatch the stock slice entry points use: request Orca's
    // plate slice and present the result in the Preview canvas.
    void request_slice();
    void request_check_print();
    void request_prepare();

private:
    void update_mode_button();
    void on_tab_changed(wxBookCtrlEvent& event);
    void on_tabpanel_destroyed(wxWindowDestroyEvent& event);

    const ShellTheme& m_theme;
    Plater&           m_plater;
    Notebook&         m_tabpanel;

    wxStaticText* m_project_name{nullptr};
    wxStaticText* m_dirty_marker{nullptr};
    StaticBox*    m_setup_chip{nullptr};
    wxStaticText* m_setup_summary{nullptr};
    Button*       m_slice_button{nullptr};
    Button*       m_mode_button{nullptr};
    Button*       m_print_button{nullptr};

    ProjectStateSubscription m_project_state_subscription;
    bool                     m_dark{false};
    bool                     m_tabpanel_alive{true};
};

} // namespace Slic3r::GUI::JusPrin
