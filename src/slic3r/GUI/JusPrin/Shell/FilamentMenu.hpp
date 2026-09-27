#pragma once

// The chip's right-half menu: the filament and colour of each project slot.
//
// Everything here comes from what OrcaSlicer already keeps -- installed
// filaments, saved colours, the project's slots and which ones the plate
// uses, and the trays a connected printer reports. Nothing is remembered on
// the side.
//
// With one slot the menu opens on that slot. With several it opens on a list
// of slots, and each slot opens the same view. A slot's view holds a colour
// row and the installed filaments that fit, and stays open after a click, so
// a filament and its colour change in one visit.
//
// The views replace each other inside one popup rather than stacking popups,
// so there is one surface to dismiss and one keyboard model throughout.

#include "HeaderControls.hpp"
#include "SetupCommands.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <wx/weakref.h>

namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin {

class FilamentMenu
{
public:
    // What one visit to the menu changed: the filament of each slot whose
    // filament moved, by slot. Colour-only changes are not listed, because
    // they change nothing about how the print is prepared.
    struct Visit
    {
        std::map<std::size_t, std::string> filaments; // slot -> preset name
    };

    struct Host
    {
        Plater* plater{nullptr};
        // Called after each change, so the chip re-reads Orca at once.
        std::function<void()> changed;
        // Called when a visit that changed a filament ends.
        std::function<void(const Visit&)> visit_ended;
    };

    // Opens anchored to the chip's right half.
    //
    // Lifetime: HeaderMenu closes the popup *before* it runs a row's callback,
    // and closing destroys the popup and everything the popup owns. A raw
    // controller would therefore be freed while a queued callback still held a
    // pointer to it. The controller is shared instead, and every row callback
    // holds a reference, so it outlives the popup by exactly as long as the
    // callbacks need it.
    static void open(wxWindow* owner, const ShellTheme& theme, bool dark, Host host, HeaderButton& anchor);

    FilamentMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Host host);

private:
    using Ptr = std::shared_ptr<FilamentMenu>;

    // A view: the slot list, or one slot's filament and colour.
    struct View
    {
        std::optional<std::size_t> slot; // empty: the slot list
        bool                       from_list{false};
    };

    // Opens a fresh popup at the same anchor on `view`. Without an anchor to
    // open at, the visit ends instead.
    static void reopen(const Ptr& self, View view);
    // Closes the popup to run `task` -- a modal dialog -- and reopens it on
    // `back`, all within the same visit.
    static void step_out(const Ptr& self, View back, const std::function<void()>& task);
    static void show(const Ptr& self, View view);
    static void show_slots(const Ptr& self);
    static void show_slot(const Ptr& self, std::size_t slot, bool from_list);
    // The "Use what's loaded on the printer…" row, or nothing when the
    // printer reports no trays. Appended to `rows` with its separator.
    static void add_tray_row(const Ptr& self, std::vector<HeaderMenuItem>& rows);
    static wxWindow* build_colour_row(const Ptr& self, wxWindow* parent, View view);
    static void pick_colour(const Ptr& self, std::size_t slot, View back);
    static void end_visit(const Ptr& self);

    wxWindow*         m_owner;
    const ShellTheme& m_theme;
    bool              m_dark;
    Host              m_host;
    // Weak: a transient popup can be dismissed by the system at any time --
    // notably while a modal dialog is up -- and every step must tolerate
    // finding it gone.
    wxWeakRef<HeaderMenu> m_menu;
    // The chip half this menu hangs from, so a step that must close the popup
    // (to put a modal dialog on screen) can bring the menu back afterwards.
    wxWeakRef<HeaderButton> m_anchor;
    Visit             m_visit;
    // Set while the menu closes itself to put a dialog up; that close does
    // not end the visit.
    bool              m_stepping_out{false};
    // The colour the keyboard is on in the colour row, by value ("#RRGGBB",
    // or "+"), so it survives the row being rebuilt.
    std::string       m_colour_cursor;
};

} // namespace Slic3r::GUI::JusPrin
