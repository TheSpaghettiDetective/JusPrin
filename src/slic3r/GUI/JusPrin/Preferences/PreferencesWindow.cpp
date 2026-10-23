// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "PreferencesWindow.hpp"

#include "PreferencesSettings.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/intl.h>
#include <wx/sizer.h>
#include <wx/translation.h>

#include <wx/stdpaths.h>

#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#else
#include <spawn.h>
extern char** environ;
#endif

namespace Slic3r::GUI::JusPrin::Preferences {

using nlohmann::json;

namespace {

// The same pacing the shell gives the docked panel's host: key checks and
// tool connections advance only when the host is pumped.
constexpr int kPumpIntervalMs = 33;
constexpr int kPumpTimerId    = wxID_HIGHEST + 1810;

// Set while the window's link opens OrcaSlicer's dialog, so that the
// open_preferences() call it makes reaches the dialog.
bool g_opening_orca_dialog = false;

// The sites whose "Open in slicer" buttons JusPrin answers: Printables uses
// prusaslicer://, Thingiverse cura://.
const std::vector<std::wstring> kModelSiteLinks{L"prusaslicer", L"cura"};

bool links_associated()
{
#ifdef _WIN32
    for (const std::wstring& prefix : kModelSiteLinks) {
        std::wstring registered;
        if (!wxGetApp().check_url_association(prefix, registered))
            return false;
    }
    return true;
#else
    return false;
#endif
}

// What JusPrin's own update check found. That check is not built yet; it is
// a separate piece of work, which also records when the check last ran. Until
// it exists the Updates section shows only the version, and nothing here asks
// OrcaSlicer's check, which looks for OrcaSlicer releases.
struct KnownUpdate
{
    std::string version;
    std::string url;
};

KnownUpdate known_update() { return {}; }

// The languages this build has a catalog for, each named in its own
// language, and English, which needs none. The same list OrcaSlicer's dialog
// starts from; it also drops the catalogs it has no name for.
std::vector<std::pair<std::string, std::string>> available_languages()
{
    std::vector<const wxLanguageInfo*> infos{wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH)};
    for (const wxString& name : wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY))
        if (const wxLanguageInfo* info = wxLocale::FindLanguageInfo(name); info != nullptr)
            infos.push_back(info);
    std::sort(infos.begin(), infos.end(), [](const wxLanguageInfo* a, const wxLanguageInfo* b) { return a->Description < b->Description; });
    infos.erase(std::unique(infos.begin(), infos.end()), infos.end());
    std::vector<std::pair<std::string, std::string>> languages;
    for (const wxLanguageInfo* info : infos) {
        const wxString& name = info->DescriptionNative.empty() ? info->Description : info->DescriptionNative;
        languages.emplace_back(info->CanonicalName.ToUTF8().data(), name.ToUTF8().data());
    }
    return languages;
}

Facts current_facts()
{
    Facts facts;
    facts.languages        = available_languages();
    facts.running_language = wxGetApp().current_language_code().ToUTF8().data();
    facts.version   = SoftFever_VERSION;
    const KnownUpdate update = known_update();
    facts.update_version  = update.version;
    facts.update_url      = update.url;
    for (const Agent::ProviderProfile& profile : Agent::provider_profiles())
        if (!Agent::load_provider_api_key(wxGetApp().app_config, profile.id).empty())
            facts.configured_providers.push_back(profile.id);
    facts.versions_folder = (boost::filesystem::path(data_dir()) / "jusprin" / "projects").string();
#ifdef _WIN32
    facts.windows = true;
#endif
    facts.links_associated = links_associated();
    return facts;
}

} // namespace

PreferencesWindow::PreferencesWindow(wxWindow*               parent,
                                     const ShellTheme&       theme,
                                     Workspace::IWorkspace&  workspace,
                                     const Agent::AgentHost& docked,
                                     OpenRequest             request)
    : wxDialog(parent, wxID_ANY, _L("Preferences"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_theme(theme)
    , m_request(std::move(request))
    , m_pump(this, kPumpTimerId)
{
    SetName("JusPrin Preferences");
    SetClientSize(FromDIP(wxSize(theme.metrics().preferences.width, theme.metrics().preferences.height)));
    SetSizer(new wxBoxSizer(wxVERTICAL));

    // Its own document, never the project's: the window holds no chat.
    Agent::ProjectPersistence::Config storage;
    storage.in_memory = true;
    m_persistence     = std::make_unique<Agent::ProjectPersistence>(workspace, std::move(storage));

    // The setup service Agent setup uses, so a key checked here is stored
    // where setup stores it. No agent: nothing here is a chat.
    Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
    m_view = new AgentWebView(this, theme, workspace, *m_persistence, runtime.availability, {}, runtime.setup,
                              AgentPageMode::Preferences);
    m_view->SetName(_L("Preferences"));
    GetSizer()->Add(m_view, 1, wxEXPAND);

    Agent::AgentHost& host = m_view->host();
    host.share_mcp_connect(docked);
    host.set_session_state_provider([this] { return state(); });
    host.set_page_message_handler([this](const std::string& type, const json& payload) { return on_page_message(type, payload); });

    Bind(wxEVT_TIMER, &PreferencesWindow::on_pump, this, kPumpTimerId);
    m_pump.Start(kPumpIntervalMs);
    apply_appearance();
    CentreOnParent();
}

PreferencesWindow::~PreferencesWindow()
{
    m_pump.Stop();
    // The host inside the view holds this window's document by reference,
    // and wx would destroy the view only after the document is gone.
    if (m_view != nullptr) {
        m_view->host().set_session_state_provider({});
        m_view->host().set_page_message_handler({});
        delete m_view;
        m_view = nullptr;
    }
    m_persistence.reset();
}

json PreferencesWindow::state() const
{
    json payload = snapshot(*wxGetApp().app_config, current_facts());
    payload["section"] = m_request.section.empty() ? "account" : m_request.section;
    return payload;
}

void PreferencesWindow::push_state()
{
    if (m_view != nullptr && m_view->host().handshake_complete())
        m_view->host().send_page_envelope(Agent::Protocol::kPreferences, state());
}

bool PreferencesWindow::on_page_message(const std::string& type, const json& payload)
{
    if (type == Agent::Protocol::kShellAction && payload.is_object() && payload.value("action", std::string()) == "close") {
        CallAfter([this] { close_for({}); });
        return true;
    }
    if (type != Agent::Protocol::kPreferencesAction)
        return false;
    const Outcome outcome = apply_action(*wxGetApp().app_config, payload);
    if (!outcome.error.empty())
        BOOST_LOG_TRIVIAL(warning) << "JusPrin Preferences refused an action: " << outcome.error;
    // The page draws what is stored, so a refused change springs back.
    push_state();
    if (outcome.error.empty() && outcome.effect != Effect::None) {
        // Not from inside the web view's message handler: some of these open
        // a modal dialog or close this window.
        CallAfter([this, outcome] { carry_out(outcome); });
    }
    return true;
}

void PreferencesWindow::carry_out(const Outcome& outcome)
{
    GUI_App& app = wxGetApp();
    switch (outcome.effect) {
    case Effect::None: break;
    case Effect::Theme:
        // What OrcaSlicer's dark mode checkbox does once it has stored the
        // setting, called in place.
        app.Update_dark_mode_flag();
#ifdef _MSW_DARK_MODE
        app.force_colors_update();
        app.update_ui_from_settings();
#endif
        if (app.plater() != nullptr) {
            SimpleEvent evt(EVT_GLCANVAS_COLOR_MODE_CHANGED);
            wxPostEvent(app.plater(), evt);
        }
        apply_appearance();
        break;
    case Effect::Restart: close_for({AfterClose::Kind::Restart}); break;
    case Effect::Associate3mf:
#ifdef _WIN32
        if (outcome.on)
            app.associate_files(L"3mf");
        else
            app.disassociate_files(L"3mf");
#endif
        break;
    case Effect::AssociateModels:
#ifdef _WIN32
        // What OrcaSlicer's three checkboxes and its dialog's closing pass do
        // together: .stp is associated with .step, and only by that pass.
        for (const wchar_t* extension : {L"stl", L"step", L"drc"}) {
            if (outcome.on)
                app.associate_files(extension);
            else
                app.disassociate_files(extension);
        }
        if (outcome.on)
            app.associate_files(L"stp");
#endif
        break;
    case Effect::AssociateLinks:
#ifdef _WIN32
        for (const std::wstring& prefix : kModelSiteLinks) {
            if (outcome.on)
                app.associate_url(prefix);
            else
                app.disassociate_url(prefix);
        }
#endif
        push_state();
        break;
    case Effect::ChooseDownloads: {
        wxDirDialog dialog(this, _L("Choose Download Directory"), wxString::FromUTF8(app.app_config->get("download_path")),
                           wxDD_NEW_DIR_BUTTON);
        if (dialog.ShowModal() == wxID_OK) {
            app.app_config->set("download_path", dialog.GetPath().ToUTF8().data());
            app.app_config->save();
            push_state();
        }
        break;
    }
    case Effect::ShowVersions: {
        const auto folder = boost::filesystem::path(data_dir()) / "jusprin" / "projects";
        boost::system::error_code ignored;
        boost::filesystem::create_directories(folder, ignored);
        desktop_open_any_folder(folder.string());
        break;
    }
    case Effect::OpenUpdate:
        if (const KnownUpdate update = known_update(); !update.url.empty())
            wxLaunchDefaultBrowser(wxString::FromUTF8(update.url));
        break;
    case Effect::OrcaPreferences: close_for({AfterClose::Kind::OrcaPreferences}); break;
    }
}

void PreferencesWindow::close_for(AfterClose after)
{
    m_after = std::move(after);
    if (IsModal())
        EndModal(wxID_OK);
    else
        Close();
}

void PreferencesWindow::apply_appearance()
{
    const bool dark = GUI_App::dark_mode();
    SetBackgroundColour(m_theme.palette(dark).surface_raised);
    if (m_view != nullptr)
        m_view->apply_appearance(dark);
    // The title bar follows the theme the way OrcaSlicer's dialogs do.
    wxGetApp().UpdateDlgDarkUI(this);
    Refresh();
}

void PreferencesWindow::on_pump(wxTimerEvent&)
{
    if (m_view == nullptr)
        return;
    Agent::AgentHost& host = m_view->host();
    host.pump_tools();
    host.pump_setup();
}

namespace {

// Set once the main window has agreed to close for a restart; the new
// process starts as this one exits.
bool g_restart_on_exit = false;
#ifndef _WIN32
std::vector<std::string> g_restart_arguments;
#endif

// Starts JusPrin again with this process's own command line (--datadir and
// all), as the last thing this process does. Starting it any earlier would
// leave this process's Edge browser, in the old language, running beside the
// new one's pages, and with "Allow only one instance" the new process would
// hand itself to this one and quit.
void start_again_at_exit()
{
    if (!g_restart_on_exit)
        return;
#ifdef _WIN32
    std::wstring        command_line = ::GetCommandLineW();
    STARTUPINFOW        startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    if (::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    }
#else
    std::vector<char*> argv;
    for (std::string& argument : g_restart_arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t child = 0;
    posix_spawn(&child, argv.front(), nullptr, nullptr, argv.data(), environ);
#endif
}

} // namespace

void restart_application()
{
    GUI_App& app = wxGetApp();
    if (app.mainframe == nullptr)
        return;
    // The open project first: autosave keeps it, so the next start offers it
    // back as it is now.
    if (ShellController* shell = installed_shell(); shell != nullptr && shell->autosave() != nullptr)
        shell->autosave()->save_now();
    static const bool registered = std::atexit(start_again_at_exit) == 0;
    if (!registered)
        return;
#ifndef _WIN32
    g_restart_arguments.clear();
    g_restart_arguments.emplace_back(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data());
    for (int index = 1; index < app.argc; ++index)
        g_restart_arguments.emplace_back(app.argv[index].ToUTF8().data());
#endif
    g_restart_on_exit = true;
    // An ordinary close, so OrcaSlicer still asks about anything unsaved; a
    // person who cancels there keeps this run and gets no restart.
    if (!app.mainframe->Close()) {
        g_restart_on_exit = false;
        // The Preferences window is gone, so say why nothing happened.
        wxMessageBox(_L("JusPrin couldn't close the project, so it didn't restart. Your project stays open, and JusPrin "
                        "will use the new language the next time it starts."),
                     _L("Restart"), wxOK | wxICON_INFORMATION, app.mainframe);
    }
}

bool answers_open_request(size_t open_on_tab, const std::string& highlight_option)
{
    return !g_opening_orca_dialog && open_on_tab == 0 && highlight_option.empty();
}

void run_after_close(const AfterClose& after)
{
    switch (after.kind) {
    case AfterClose::Kind::Nothing: break;
    case AfterClose::Kind::Restart:
        // Closing the main window destroys the shell that opened this one,
        // so the restart runs from the application, on its next turn.
        wxGetApp().CallAfter([] { restart_application(); });
        break;
    case AfterClose::Kind::OrcaPreferences:
        wxGetApp().CallAfter([] {
            g_opening_orca_dialog = true;
            wxGetApp().open_preferences();
            g_opening_orca_dialog = false;
        });
        break;
    }
}

} // namespace Slic3r::GUI::JusPrin::Preferences
