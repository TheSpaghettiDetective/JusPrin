#include "AppMenus.hpp"

#include "AboutWindow.hpp"
#include "SetupCommands.hpp"
#include "ShellController.hpp"
#include "StatusRow.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Home/HomeWebView.hpp"
#include "slic3r/GUI/JusPrin/Home/OrcaHomeBackend.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/BBLTopbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Notebook.hpp"

#include <wx/menu.h>
#include <wx/utils.h>

#include <algorithm>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

constexpr std::size_t kRecentProjects = 10;

// MainFrame's separator before a key OrcaSlicer shows but does not bind as a
// macOS key equivalent, which would take the key from typing.
#ifdef __APPLE__
const wxString kShownKey = " - ";
#else
const wxString kShownKey = "\t";
#endif

// One of OrcaSlicer's items, found by the localized label MainFrame gave it,
// as the shell finds its Calibration menu.
wxMenuItem* stock_item(wxMenu* menu, const wxString& label)
{
    const wxString text = wxStripMenuCodes(label);
    for (wxMenuItem* item : menu->GetMenuItems())
        if (!item->IsSeparator() && item->GetItemLabelText() == text)
            return item;
    throw std::runtime_error("OrcaSlicer's menu item \"" + std::string(text.ToUTF8()) + "\" is not where JusPrin looks for it");
}

wxMenu* stock_submenu(wxMenu* menu, const wxString& label)
{
    wxMenu* submenu = stock_item(menu, label)->GetSubMenu();
    if (submenu == nullptr)
        throw std::runtime_error("OrcaSlicer's \"" + std::string(label.ToUTF8()) + "\" item has no submenu");
    return submenu;
}

std::vector<wxMenuItem*> items_of(wxMenu* menu)
{
    std::vector<wxMenuItem*> items;
    for (wxMenuItem* item : menu->GetMenuItems())
        items.push_back(item);
    return items;
}

// A menu's items less `removed`, without a separator left at either end or
// next to another one.
std::vector<wxMenuItem*> without(wxMenu* menu, const std::vector<wxMenuItem*>& removed)
{
    std::vector<wxMenuItem*> kept;
    for (wxMenuItem* item : menu->GetMenuItems()) {
        if (std::find(removed.begin(), removed.end(), item) != removed.end())
            continue;
        if (item->IsSeparator() && (kept.empty() || kept.back()->IsSeparator()))
            continue;
        kept.push_back(item);
    }
    while (!kept.empty() && kept.back()->IsSeparator())
        kept.pop_back();
    return kept;
}

bool holds(wxMenu* menu, const wxMenuItem* item)
{
    for (const wxMenuItem* held : menu->GetMenuItems())
        if (held == item)
            return true;
    return false;
}

#ifndef __APPLE__
wxMenu* top_submenu(MainFrame& frame, const wxString& label)
{
    const wxString title = wxStripMenuCodes(label);
    for (wxMenuItem* item : frame.topbar()->GetTopMenu()->GetMenuItems())
        if (item->IsSubMenu() && item->GetItemLabelText() == title)
            return item->GetSubMenu();
    throw std::runtime_error("OrcaSlicer's " + std::string(title.ToUTF8()) + " menu is not in the title bar menu");
}
#else
wxMenu* bar_menu(MainFrame& frame, const wxString& title)
{
    const int at = frame.GetMenuBar()->FindMenu(title);
    if (at == wxNOT_FOUND)
        throw std::runtime_error("OrcaSlicer's " + std::string(title.ToUTF8()) + " menu is not on the menu bar");
    return frame.GetMenuBar()->GetMenu(at);
}
#endif

} // namespace

AppMenus::AppMenus(MainFrame& frame, ShellController& shell) : m_frame(frame), m_shell(shell) {}

AppMenus::~AppMenus() { uninstall(); }

void AppMenus::install()
{
    if (m_installed)
        throw std::logic_error("the app menus are already installed");
    m_installed = true;
#ifdef __APPLE__
    wxMenuBar* bar = m_frame.GetMenuBar();
    m_file = bar_menu(m_frame, _L("File"));
    m_view = bar_menu(m_frame, _L("View"));
    m_help = bar_menu(m_frame, _L("Help"));
#else
    m_file = m_frame.topbar()->GetFileMenu();
    if (m_file == nullptr)
        throw std::runtime_error("OrcaSlicer's title bar has no File menu");
    m_view = top_submenu(m_frame, _L("View"));
    m_help = top_submenu(m_frame, _L("Help"));
#endif
    build_file(m_file);
    build_view(m_view);
    build_help(m_help);
#ifdef __APPLE__
    open_about_from(bar->OSXGetAppleMenu());
#else
    open_about_from(m_help);
#endif

#ifndef __APPLE__
    wxMenu* top = m_frame.topbar()->GetTopMenu();
    // Sync Presets pulls presets from OrcaSlicer's cloud, which JusPrin
    // accounts are not. Speed Dial is OrcaSlicer's palette of its own
    // commands. Plugins opens OrcaSlicer's Python plugin manager; a plugin's
    // page is a tab, and the shell hides the tab strip. On the Mac both are
    // File items, which build_file leaves out.
    const std::vector<wxMenuItem*> top_items = without(
        top, {stock_item(top, _L("Sync Presets")), stock_item(top, _L("Open Speed Dial")), stock_item(top, _L("Plugins"))});
#endif
    // Print and Printer follow View, as an app's own menus do.
    wxMenu* print   = build_print();
    wxMenu* printer = build_printer();
#ifdef __APPLE__
    const int view_at = bar->FindMenu(_L("View"));
    bar->Insert(view_at + 1, print, _L("Print"));
    m_bar_menus.push_back(print);
    bar->Insert(view_at + 2, printer, _L("Printer"));
    m_bar_menus.push_back(printer);
#else
    std::vector<wxMenuItem*> items;
    for (wxMenuItem* item : top_items) {
        items.push_back(item);
        if (item->GetSubMenu() == m_view) {
            items.push_back(submenu(top, print, _L("Print")));
            items.push_back(submenu(top, printer, _L("Printer")));
        }
    }
    arrange(top, items);
#endif
    m_print   = print;
    m_printer = printer;

    m_file->Bind(wxEVT_MENU_OPEN, &AppMenus::on_file_menu_open, this);
    fill_open_recent();
    // MainFrame's own key hook takes these shortcuts for OrcaSlicer's
    // commands, and the handler bound last runs first. The shell installs
    // during MainFrame's constructor, before that hook is bound, so this
    // binds once the constructor is done.
    CallAfter([this] {
        if (m_installed)
            m_frame.Bind(wxEVT_CHAR_HOOK, &AppMenus::on_char_hook, this);
    });
}

void AppMenus::uninstall()
{
    if (!m_installed)
        return;
    m_installed = false;
    m_frame.Unbind(wxEVT_CHAR_HOOK, &AppMenus::on_char_hook, this);
    if (m_file != nullptr)
        m_file->Unbind(wxEVT_MENU_OPEN, &AppMenus::on_file_menu_open, this);
    if (m_recent != nullptr)
        for (const auto& [id, project] : m_recent_projects)
            m_recent->Unbind(wxEVT_MENU, &AppMenus::on_recent, this, id);
    m_recent_projects.clear();
    for (const auto& [menu, id] : m_bound) {
        menu->Unbind(wxEVT_MENU, &AppMenus::on_command, this, id);
        m_frame.Unbind(wxEVT_UPDATE_UI, &AppMenus::on_update_ui, this, id);
    }
    m_bound.clear();
    m_commands.clear();

    // Last change first, so each menu goes back to the items it had.
    for (auto it = m_arranged.rbegin(); it != m_arranged.rend(); ++it)
        arrange(it->menu, it->items);
    m_arranged.clear();
    for (wxMenuItem* item : m_placed)
        delete item;
    m_placed.clear();
#ifdef __APPLE__
    wxMenuBar* bar = m_frame.GetMenuBar();
    for (wxMenu* menu : m_bar_menus)
        for (size_t at = 0; at < bar->GetMenuCount(); ++at)
            if (bar->GetMenu(at) == menu) {
                delete bar->Remove(at);
                break;
            }
#endif
    m_bar_menus.clear();
    if (m_relabeled != nullptr)
        m_relabeled->SetItemLabel(m_relabeled_from);
    m_relabeled = nullptr;
    m_file = m_view = m_help = m_print = m_printer = m_recent = nullptr;
    m_recent_item = nullptr;
}

bool AppMenus::run_shortcut(const wxKeyEvent& event)
{
    int id = 0;
    const int key = event.GetKeyCode();
    if (event.GetModifiers() == wxMOD_CMD) {
        if (key == 'N')
            id = m_new_project;
        else if (key == 'O')
            id = m_open;
        else if (key == 'S')
            id = m_save;
        else if (key == 'R')
            id = m_slice;
    } else if (event.GetModifiers() == (wxMOD_CMD | wxMOD_SHIFT) && key == 'S') {
        id = m_export_project;
    }
    if (id == 0)
        return false;
    run(id);
    return true;
}

// -- Building ---------------------------------------------------------------

wxMenuItem* AppMenus::command(wxMenu* menu, const wxString& label, Command command, bool check)
{
    auto* item = new wxMenuItem(menu, wxID_ANY, label, wxEmptyString, check ? wxITEM_CHECK : wxITEM_NORMAL);
    const int id = item->GetId();
    // On the menu, as OrcaSlicer binds its own items off Windows; the frame
    // answers the update events, which wx sends to the window.
    menu->Bind(wxEVT_MENU, &AppMenus::on_command, this, id);
    m_frame.Bind(wxEVT_UPDATE_UI, &AppMenus::on_update_ui, this, id);
    m_bound.emplace_back(menu, id);
    m_commands.emplace(id, std::move(command));
    return item;
}

wxMenuItem* AppMenus::separator(wxMenu* menu)
{
    return new wxMenuItem(menu, wxID_SEPARATOR);
}

wxMenuItem* AppMenus::submenu(wxMenu* parent, wxMenu* menu, const wxString& label)
{
    return new wxMenuItem(parent, wxID_ANY, label, wxEmptyString, wxITEM_NORMAL, menu);
}

// Makes `menu` hold `items`, in order. An item of the menu not among them is
// taken out and kept; one not yet in it is put in. An item already in its
// place is left alone. Every new item of a stock menu is recorded, to be
// deleted once uninstall has taken it out again.
void AppMenus::arrange(wxMenu* menu, const std::vector<wxMenuItem*>& items)
{
    if (m_installed) {
        m_arranged.push_back({menu, items_of(menu)});
        for (wxMenuItem* item : items)
            if (!holds(menu, item))
                m_placed.push_back(item);
    }
    for (wxMenuItem* item : items_of(menu))
        if (std::find(items.begin(), items.end(), item) == items.end())
            menu->Remove(item);
    for (size_t at = 0; at < items.size(); ++at) {
        if (at < menu->GetMenuItemCount() && menu->FindItemByPosition(at) == items[at])
            continue;
        if (holds(menu, items[at]))
            menu->Remove(items[at]);
        menu->Insert(at, items[at]);
    }
}

void AppMenus::build_file(wxMenu* menu)
{
    wxMenuItem* import_item = stock_item(menu, _L("Import"));
    wxMenuItem* export_item = stock_item(menu, _L("Export"));
    wxMenu*     exports     = export_item->GetSubMenu();
#ifdef __APPLE__
    wxMenuItem* quit        = menu->FindChildItem(wxID_EXIT);
#else
    // A Shortcut::Quit item, whose id is not wxID_EXIT, off macOS.
    wxMenuItem* quit        = stock_item(menu, _L("Quit"));
#endif
    if (exports == nullptr || quit == nullptr)
        throw std::runtime_error("OrcaSlicer's File menu has no Export submenu or no Quit item");
#ifdef __APPLE__
    wxMenuItem* preset_bundle = stock_item(menu, _L("Preset Bundle"));
#endif

    // JusPrin keeps every project in its own store and saves as it goes, so
    // File opens and saves that store's projects. OrcaSlicer's Open Project,
    // Recent files and Save Project read and write the 3MF at its own path;
    // a 3MF leaves the store only through Export.
    wxMenuItem* new_project = command(menu, _L("New project") + "\tCtrl+N", {[this] {
        m_shell.home_view()->backend().new_project();
    }});
    m_new_project = new_project->GetId();
    wxMenuItem* open = command(menu, _L("Open…") + "\tCtrl+O", {[this] {
        m_shell.home_view()->backend().open_file();
    }});
    m_open = open->GetId();
    m_recent      = new wxMenu();
    m_recent_item = submenu(menu, m_recent, _L("Open recent"));
    // Enabled from the list fill_open_recent last read.
    m_frame.Bind(wxEVT_UPDATE_UI, &AppMenus::on_update_ui, this, m_recent_item->GetId());
    m_bound.emplace_back(menu, m_recent_item->GetId());
    m_commands.emplace(m_recent_item->GetId(), Command{{}, [this] { return !m_recent_projects.empty(); }});

    // Saves now and pins that version, so it is kept, and Version history
    // lists it.
    wxMenuItem* save = command(menu, _L("Save") + "\tCtrl+S", {[this] {
        m_shell.autosave()->pin_current();
        m_shell.status_row()->refresh();
    }, [this] { return !home() && m_shell.autosave() != nullptr; }});
    m_save = save->GetId();
    wxMenuItem* versions = command(menu, _L("Version history…"), {[this] {
        m_shell.open_project_row(ProjectMenuRow::VersionHistory);
    }, [this] { return project_shown(); }});

    wxMenuItem* project     = command(exports, _L("Project 3MF…") + "\tCtrl+Shift+S", {[this] {
        m_shell.open_project_row(ProjectMenuRow::Export);
    }, [this] { return !home() && m_shell.autosave() != nullptr; }});
    m_export_project = project->GetId();
    std::vector<wxMenuItem*> export_items{project, separator(exports)};
    for (wxMenuItem* item : exports->GetMenuItems())
        export_items.push_back(item);
    arrange(exports, export_items);

    std::vector<wxMenuItem*> items{new_project, open, m_recent_item, separator(menu), save, versions, separator(menu),
                                   import_item, export_item, separator(menu), quit};
#ifdef __APPLE__
    // The Mac keeps Quit in the app menu and hides it here; Preset Bundle
    // follows it because the app menu takes no other items.
    items.push_back(preset_bundle);
#endif
    arrange(menu, items);
#ifdef _WIN32
    // Windows names it Exit.
    m_relabeled      = quit;
    m_relabeled_from = quit->GetItemLabel();
    quit->SetItemLabel(_L("Exit"));
#endif
}

void AppMenus::build_view(wxMenu* menu)
{
    // The G-code window and the sidebar's layout belong to OrcaSlicer's
    // sidebar, which JusPrin does not show.
    const std::vector<wxMenuItem*> stock = without(menu, {stock_item(menu, _L("Show &G-code Window") + kShownKey + "C"),
                                                          stock_item(menu, _L("Reset Window Layout"))});
    std::vector<wxMenuItem*> items;
    items.push_back(command(menu, _L("Home"), {[this] { m_shell.status_row()->request_home(); }, [this] { return !home(); }}));

    // On the Mac the label says what choosing it does; Windows asks for a
    // check mark instead of a label that changes.
#ifdef __APPLE__
    constexpr bool check = false;
#else
    constexpr bool check = true;
#endif
    items.push_back(command(menu, _L("Plates and project panel"), {
        [this] { m_shell.toggle_left_pane(); },
        [this] { return !m_shell.task_open() && m_frame.m_tabpanel->GetSelectedPageName() == TAB_ID_PREPARE; },
        check ? [this] { return !m_shell.is_left_pane_collapsed(); } : std::function<bool()>{},
        check ? std::function<wxString()>{} : [this] {
            return m_shell.is_left_pane_collapsed() ? _L("Show plates and project panel") : _L("Hide plates and project panel");
        }}, check));
    items.push_back(command(menu, _L("AI panel"), {
        [this] { m_shell.toggle_agent_pane(); },
        [this] { return project_shown() && m_shell.agent_pane() != nullptr; },
        check ? [this] { return !m_shell.is_agent_pane_collapsed(); } : std::function<bool()>{},
        check ? std::function<wxString()>{} : [this] {
            return m_shell.is_agent_pane_collapsed() ? _L("Show AI panel") : _L("Hide AI panel");
        }}, check));
    items.push_back(separator(menu));

    items.insert(items.end(), stock.begin(), stock.end());

    items.push_back(separator(menu));
    items.push_back(command(menu, _L("Switch to classic view…"), {[this] {
        // On the frame's next turn, so this handler returns before uninstall
        // takes the menus apart.
        m_frame.CallAfter([] { detach_shell(); });
    }}));
    arrange(menu, items);
}

void AppMenus::build_help(wxMenu* menu)
{
    // Setup Wizard is OrcaSlicer's first run, which Home replaces; its tips
    // are about OrcaSlicer's screens.
    const std::vector<wxMenuItem*> stock = without(menu, {stock_item(menu, _L("Setup Wizard")),
                                                          stock_item(menu, _L("Show Tip of the Day"))});
    std::vector<wxMenuItem*> items;
    items.push_back(command(menu, _L("Ask AI…"), {[this] {
        if (home())
            m_frame.select_tab(TAB_ID_PREPARE);
        if (m_shell.is_agent_pane_collapsed())
            m_shell.toggle_agent_pane();
    }, [this] { return !m_shell.task_open() && m_shell.agent_pane() != nullptr; }}));
    items.push_back(command(menu, _L("Report a problem…"), {[this] {
        const std::string footer = "\n\n---\nApp version: " + Updates::version_label() + "\nSystem: " +
                                   std::string(wxGetOsDescription().ToUTF8());
        const PrinterSetup::IssueLink link = PrinterSetup::issue_link("", "What happened:\n\n\nWhat you expected:\n\n\nSteps to make it happen again:\n", footer);
        if (!wxLaunchDefaultBrowser(wxString::FromUTF8(link.url)))
            MessageDialog(&m_frame, _L("The app couldn't open your browser."), _L("Report a problem"), wxOK | wxICON_ERROR).ShowModal();
    }}));
    items.push_back(separator(menu));
    items.insert(items.end(), stock.begin(), stock.end());
    arrange(menu, items);
}

// OrcaSlicer's "About JusPrin" item (in Help on Windows and Linux, in the app
// menu on the Mac) opens JusPrin's About window. OrcaSlicer binds the item on
// this menu; a handler bound later on the same menu runs first, and
// on_command does not pass the event on, so OrcaSlicer's About never opens.
// uninstall unbinds this one, and classic view has OrcaSlicer's back.
void AppMenus::open_about_from(wxMenu* menu)
{
    const int id = stock_item(menu, wxString::Format(_L("&About %s"), SLIC3R_APP_FULL_NAME))->GetId();
    menu->Bind(wxEVT_MENU, &AppMenus::on_command, this, id);
    m_bound.emplace_back(menu, id);
    m_commands.emplace(id, Command{[this] {
        if (const ShellTheme* theme = brand_theme()) {
            AboutWindow window(&m_frame, *theme);
            window.ShowModal();
        }
    }});
}

wxMenu* AppMenus::build_print()
{
    auto* menu = new wxMenu();
    const auto action = [this, menu](const wxString& label, PrintAction action) {
        menu->Append(command(menu, label, {[this, action] { m_shell.status_row()->request_action(action); },
                                           [this, action] { return project_shown() && m_shell.status_row()->action_enabled(action); }}));
    };
    action(_L("Slice plate") + "\tCtrl+R", PrintAction::Slice);
    m_slice = menu->FindItemByPosition(0)->GetId();
    action(_L("Slice all plates"), PrintAction::SliceAll);
    menu->AppendSeparator();
    action(_L("Check print…"), PrintAction::CheckPrint);
    action(_L("Print…"), PrintAction::Print);
    action(_L("Print all plates…"), PrintAction::PrintAll);
    action(_L("Export sliced file…"), PrintAction::Export);
    return menu;
}

wxMenu* AppMenus::build_printer()
{
    // The header's printer and filament menus, for the selected printer and
    // the first filament slot. Each opens over any screen, Home included.
    auto* menu = new wxMenu();
    const auto placeholder = [] { return wxGetApp().preset_bundle->printers.get_selected_preset().is_default; };
    menu->Append(command(menu, _L("Add a printer…"), {[this] { m_shell.open_printer_conversation(); }}));
    menu->AppendSeparator();
    // OrcaSlicer's placeholder is no printer at all, so its settings are
    // adding one.
    menu->Append(command(menu, _L("Printer settings…"), {[this, placeholder] {
        if (placeholder())
            m_shell.open_printer_conversation();
        else
            SetupCommands::open_settings_tab(Preset::TYPE_PRINTER);
    }}));
    menu->Append(command(menu, _L("Ask AI about printer settings…"), {[this, placeholder] {
        m_shell.open_printer_conversation(placeholder() ? std::string() : SetupCommands::current_printer().preset_name);
    }}));
    const auto has_filament = [] { return !SetupCommands::filament_slots().empty(); };
    menu->Append(command(menu, _L("Filament settings…"), {[] { SetupCommands::open_filament_settings(0); }, has_filament}));
    menu->Append(command(menu, _L("Ask AI about filament settings…"), {[this] {
        const auto slots = SetupCommands::filament_slots();
        m_shell.open_filament_help(0, slots.front().filament.preset_name, std::string(slots.front().filament.alias.ToUTF8()));
    }, has_filament}));
    menu->AppendSeparator();
    menu->Append(command(menu, _L("Calibrate…"), {[this] {
        const auto slots = SetupCommands::filament_slots();
        m_shell.start_calibration(std::string(SetupCommands::current_printer().nickname.ToUTF8()),
                                  slots.empty() ? std::string() : std::string(slots.front().filament.alias.ToUTF8()));
    }, [placeholder] { return !placeholder() && SetupCommands::current_printer().valid; }}));
    return menu;
}

// The saved projects as Home lists them, newest first. Read when File opens,
// not on every update: reading the store touches each project's folder.
void AppMenus::fill_open_recent()
{
    for (const auto& [id, project] : m_recent_projects)
        m_recent->Unbind(wxEVT_MENU, &AppMenus::on_recent, this, id);
    m_recent_projects.clear();
    while (m_recent->GetMenuItemCount() > 0)
        m_recent->Destroy(m_recent->FindItemByPosition(0));

    Workspace::ProjectAutosave* autosave = m_shell.autosave();
    const auto projects = autosave != nullptr ? autosave->projects() : std::nullopt;
    if (projects)
        for (const auto& project : *projects) {
            if (m_recent_projects.size() == kRecentProjects)
                break;
            wxString name = project.name.empty() || project.name == _u8L("Untitled") ? _L("Untitled project")
                                                                                     : wxString::FromUTF8(project.name);
            name.Replace("&", "&&");
            wxMenuItem* item = m_recent->Append(wxID_ANY, name);
            m_recent->Bind(wxEVT_MENU, &AppMenus::on_recent, this, item->GetId());
            m_recent_projects.emplace(item->GetId(), project.id);
        }
    m_recent_item->Enable(!m_recent_projects.empty());
}

// -- Events -----------------------------------------------------------------

bool AppMenus::run(int id)
{
    const auto it = m_commands.find(id);
    if (it == m_commands.end() || !it->second.run || (it->second.enabled && !it->second.enabled()))
        return false;
    // A copy: the command may take these menus apart.
    const std::function<void()> command = it->second.run;
    command();
    return true;
}

void AppMenus::on_char_hook(wxKeyEvent& event)
{
    if (!run_shortcut(event))
        event.Skip();
}

void AppMenus::on_command(wxCommandEvent& event) { run(event.GetId()); }

void AppMenus::on_update_ui(wxUpdateUIEvent& event)
{
    const auto it = m_commands.find(event.GetId());
    if (it == m_commands.end()) {
        event.Skip();
        return;
    }
    const Command& command = it->second;
    event.Enable(!command.enabled || command.enabled());
    if (command.checked)
        event.Check(command.checked());
    if (command.label)
        event.SetText(command.label());
}

void AppMenus::on_file_menu_open(wxMenuEvent& event)
{
    fill_open_recent();
    event.Skip();
}

void AppMenus::on_recent(wxCommandEvent& event)
{
    const auto it = m_recent_projects.find(event.GetId());
    if (it == m_recent_projects.end())
        throw std::logic_error("an Open recent row has no project");
    // A copy: opening the project refills nothing now, but the list is the
    // menu's and the menu is open.
    const std::string project = it->second;
    m_shell.home_view()->backend().open_project(project);
}

bool AppMenus::home() const { return m_frame.m_tabpanel->GetSelectedPageName() == TAB_ID_HOME; }

bool AppMenus::project_shown() const { return !home() && !m_shell.task_open(); }

} // namespace Slic3r::GUI::JusPrin
