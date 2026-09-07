#pragma once

// The chip's right-half menu: the spools this printer has, one click each.
//
// The whole point of the design is that swapping to a spool you already own
// takes two clicks -- open the half, pick the row -- so the remembered list is
// the first thing in the menu and never scrolls or truncates. Everything that
// is not "pick a spool I already have" is one step further away: the ⋯ menu on
// a row for editing it, and "Other spool…" for anything not in the list.
//
// The three steps (list, other-spool search, new-spool details) replace each
// other inside one popup rather than stacking popups, so there is one surface
// to dismiss and one keyboard model throughout.

#include "HeaderControls.hpp"
#include "SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"

#include <functional>
#include <memory>

#include <wx/weakref.h>

class wxTextCtrl;

namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin {

class SpoolMenu
{
public:
    struct Host
    {
        Workspace::SpoolStore* store{nullptr};
        Plater*                plater{nullptr};
        // Called after the project has actually been switched to this spool.
        std::function<void(const Workspace::Spool&)> selected;
        // Called after the store changed without a swap (rename, recolour,
        // remove), so the chip can re-read it.
        std::function<void()> store_changed;
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

    SpoolMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Host host);

private:
    using Ptr = std::shared_ptr<SpoolMenu>;

    // Each takes the shared handle so the callbacks they install keep the
    // controller alive after the popup has gone.
    static void reopen(const Ptr& self);
    static void show_spools(const Ptr& self);
    static void show_row_menu(const Ptr& self, const Workspace::Spool& spool);
    static void show_other_spool(const Ptr& self);
    static void show_new_spool(const Ptr& self, const SetupCommands::FilamentInfo& preset);

    // Applies a remembered spool to the project, then reports it.
    static void select(const Ptr& self, const Workspace::Spool& spool);

    wxWindow*         m_owner;
    const ShellTheme& m_theme;
    bool              m_dark;
    Host              m_host;
    // Weak: a transient popup can be dismissed by the system at any time --
    // notably while one of the row dialogs below is modal -- and every step
    // must tolerate finding it gone.
    wxWeakRef<HeaderMenu> m_menu;
    // The chip half this menu hangs from, so a step that must close the popup
    // (to put a modal dialog on screen) can bring the menu back afterwards.
    wxWeakRef<HeaderButton> m_anchor;

    // "Other spool…" step. The free-text query and the generic filter are
    // independent: the filter is a fact about the preset, the query is what
    // the person typed, and narrowing by one must never fake the other.
    wxString    m_search;
    bool        m_generic_only{false};
    wxTextCtrl* m_search_field{nullptr};

    // "New spool" step.
    std::string m_new_preset;
    wxString    m_new_material;
    wxString    m_new_vendor;
    wxColour    m_new_colour;
    wxString    m_new_name;
    bool        m_new_name_edited{false};
    wxTextCtrl* m_name_field{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
