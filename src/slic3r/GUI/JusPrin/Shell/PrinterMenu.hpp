#pragma once

// The chip's left-half menu: what this printer is, what it is doing, its
// nozzle (shown, not changed), and the plate, the one setup value a person
// changes often enough to want here. Everything else is a door into Orca's
// own settings.
//
// The selected printer stays at the top. Other saved printers follow its
// nozzle and plate rows, with the same live status Home can report. Connecting
// a non-Bambu print host remains part of printer setup.
//
// The plate sub-list replaces the menu's own rows instead of opening a second
// popup, so there is one surface to dismiss and one keyboard model.

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
    static void show_plates(const Ptr& self);
    // A sub-list: a back row carrying the step's name, then the choices.
    static void show_sublist(const Ptr& self, const wxString& title, std::vector<HeaderMenuItem> choices);

    Plater& m_plater;
    wxWeakRef<wxWindow> m_owner;
    ShellTheme m_theme;
    bool m_dark{false};
    // Weak: see FilamentMenu -- a transient popup may vanish under us.
    wxWeakRef<HeaderMenu> m_menu;
};

} // namespace Slic3r::GUI::JusPrin
