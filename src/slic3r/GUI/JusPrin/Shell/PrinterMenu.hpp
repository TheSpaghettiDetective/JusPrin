#pragma once

// The chip's left-half menu: what this printer is, what it is doing, and the
// two setup values a person changes often enough to want here -- nozzle and
// plate. Everything else is a door into Orca's own settings.
//
// Five rows for one printer, by design. Listing several printers, adding one,
// and connecting a non-Bambu print host are separate work; this slice shows one
// machine and says plainly when it cannot see its state.
//
// The nozzle and plate sub-lists replace the menu's own rows instead of opening
// a second popup, so there is one surface to dismiss and one keyboard model.

#include "HeaderControls.hpp"

#include <wx/weakref.h>

namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin {

class PrinterMenu
{
public:
    // Opens the menu anchored to the chip's left half. The controller lives
    // exactly as long as the popup and deletes itself when it dismisses.
    static void open(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater, HeaderButton& anchor);

private:
    PrinterMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater);

    void show_root();
    void show_nozzles();
    void show_plates();
    // A sub-list: a back row carrying the step's name, then the choices.
    void show_sublist(const wxString& title, std::vector<HeaderMenuItem> choices);

    wxWindow*         m_owner;
    const ShellTheme& m_theme;
    bool              m_dark;
    Plater&           m_plater;
    HeaderMenu*       m_menu{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
