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

    // Opens anchored to the chip's right half. The controller lives exactly as
    // long as the popup and deletes itself when it dismisses.
    static void open(wxWindow* owner, const ShellTheme& theme, bool dark, Host host, HeaderButton& anchor);

private:
    SpoolMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Host host);

    void show_spools();
    void show_row_menu(const Workspace::Spool& spool);
    void show_other_spool();
    void show_new_spool(const SetupCommands::FilamentInfo& preset);

    // Applies a remembered spool to the project, then reports it.
    void select(const Workspace::Spool& spool);

    wxWindow*         m_owner;
    const ShellTheme& m_theme;
    bool              m_dark;
    Host              m_host;
    HeaderMenu*       m_menu{nullptr};

    // "Other spool…" step.
    wxString    m_search;
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
