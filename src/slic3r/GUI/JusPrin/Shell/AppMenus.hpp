#pragma once

#include <wx/event.h>
#include <wx/string.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

class wxKeyEvent;
class wxMenu;
class wxMenuEvent;
class wxMenuItem;
class wxUpdateUIEvent;

namespace Slic3r::GUI {
class MainFrame;
}

namespace Slic3r::GUI::JusPrin {

class ShellController;

// The app's menus while the shell is installed: OrcaSlicer's own, rearranged
// in place, with JusPrin's commands added and a Print and a Printer menu after
// View. The same menus show on every screen, Home included; a command that
// cannot run there is greyed out, never hidden. An OrcaSlicer item is taken
// out only when it can never run in JusPrin. On the Mac they are the menu bar;
// on Windows and Linux, OrcaSlicer's File button and its title-bar drop-down.
//
// OrcaSlicer's items stay in the wxMenu that made them, since their handlers
// are bound on it, and an item already where it belongs is not touched: the
// Mac hides Quit inside File, and View holds a radio group. Uninstall puts
// every menu back as it was.
class AppMenus : public wxEvtHandler
{
public:
    AppMenus(MainFrame& frame, ShellController& shell);
    ~AppMenus();

    AppMenus(const AppMenus&)            = delete;
    AppMenus& operator=(const AppMenus&) = delete;

    // Throws std::runtime_error when one of OrcaSlicer's items is not where
    // its title says, with whatever was already changed left for uninstall()
    // to put back. A broken precondition is a std::logic_error.
    void install();
    void uninstall();

    // The menu shortcuts JusPrin answers itself: Cmd/Ctrl+N, O, S, Shift+S
    // and R. Whether the command ran or not, true means the key is spent: on
    // Home, Cmd+S must not fall through to OrcaSlicer's Save.
    bool run_shortcut(const wxKeyEvent& event);

    // For the integration harness.
    wxMenu* file_menu() const { return m_file; }
    wxMenu* view_menu() const { return m_view; }
    wxMenu* help_menu() const { return m_help; }
    wxMenu* print_menu() const { return m_print; }
    wxMenu* printer_menu() const { return m_printer; }
    wxMenu* open_recent_menu() const { return m_recent; }

private:
    struct Command
    {
        std::function<void()>     run;
        std::function<bool()>     enabled; // empty: always
        std::function<bool()>     checked; // a check item's state
        std::function<wxString()> label;   // a label that follows state
    };
    // A stock menu as OrcaSlicer built it, to put back.
    struct Arranged
    {
        wxMenu*                  menu;
        std::vector<wxMenuItem*> items;
    };

    wxMenuItem* command(wxMenu* menu, const wxString& label, Command command, bool check = false);
    wxMenuItem* separator(wxMenu* menu);
    wxMenuItem* submenu(wxMenu* parent, wxMenu* menu, const wxString& label);
    void        arrange(wxMenu* menu, const std::vector<wxMenuItem*>& items);
    bool        run(int id);

    void build_file(wxMenu* menu);
    void build_view(wxMenu* menu);
    void build_help(wxMenu* menu);
    wxMenu* build_print();
    wxMenu* build_printer();
    void    fill_open_recent();

    void on_char_hook(wxKeyEvent& event);
    void on_command(wxCommandEvent& event);
    void on_update_ui(wxUpdateUIEvent& event);
    void on_file_menu_open(wxMenuEvent& event);
    void on_recent(wxCommandEvent& event);

    // What the commands ask.
    bool home() const;
    bool project_shown() const;

    MainFrame&       m_frame;
    ShellController& m_shell;
    bool             m_installed{false};

    std::map<int, Command>               m_commands;
    std::vector<std::pair<wxMenu*, int>> m_bound;
    std::vector<Arranged>                m_arranged;
    // JusPrin's items placed in OrcaSlicer's menus; deleted once those menus
    // are put back. Items in JusPrin's own menus go with their menu.
    std::vector<wxMenuItem*> m_placed;
    // The Mac menu bar's Print and Printer menus.
    std::vector<wxMenu*> m_bar_menus;
    wxMenuItem*          m_relabeled{nullptr};
    wxString             m_relabeled_from;

    wxMenu*     m_file{nullptr};
    wxMenu*     m_view{nullptr};
    wxMenu*     m_help{nullptr};
    wxMenu*     m_print{nullptr};
    wxMenu*     m_printer{nullptr};
    wxMenu*     m_recent{nullptr};
    wxMenuItem* m_recent_item{nullptr};
    int         m_new_project{0}, m_open{0}, m_save{0}, m_export_project{0}, m_slice{0};
    std::map<int, std::string> m_recent_projects; // item id -> project id
};

} // namespace Slic3r::GUI::JusPrin
