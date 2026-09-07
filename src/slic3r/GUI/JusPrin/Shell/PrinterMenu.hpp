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

#include <memory>

namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin {

class PrinterMenu
{
public:
    // Opens the menu anchored to the chip's left half.
    //
    // Lifetime: HeaderMenu closes the popup *before* running a row's callback,
    // so a raw controller would be freed while a queued callback still pointed
    // at it. The controller is shared and every callback holds a reference.
    static void open(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater, HeaderButton& anchor);

    PrinterMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater);

private:
    using Ptr = std::shared_ptr<PrinterMenu>;

    static void show_root(const Ptr& self);
    static void show_nozzles(const Ptr& self);
    static void show_plates(const Ptr& self);
    // A sub-list: a back row carrying the step's name, then the choices.
    static void show_sublist(const Ptr& self, const wxString& title, std::vector<HeaderMenuItem> choices);

    wxWindow*         m_owner;
    const ShellTheme& m_theme;
    bool              m_dark;
    Plater&           m_plater;
    // Weak: see SpoolMenu -- a transient popup may vanish under us.
    wxWeakRef<HeaderMenu> m_menu;
};

} // namespace Slic3r::GUI::JusPrin
