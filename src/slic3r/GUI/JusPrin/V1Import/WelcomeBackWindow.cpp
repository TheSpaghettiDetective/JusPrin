// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "WelcomeBackWindow.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"

#include <nlohmann/json.hpp>

#include <wx/sizer.h>

namespace Slic3r::GUI::JusPrin::V1Import {

using nlohmann::json;

namespace {

// The pacing the Preferences window gives its host: sign-in and the account
// check advance only when the host is pumped.
constexpr int kPumpIntervalMs = 33;
constexpr int kPumpTimerId    = wxID_HIGHEST + 1811;

} // namespace

WelcomeBackWindow::WelcomeBackWindow(wxWindow* parent, const ShellTheme& theme, Workspace::IWorkspace& workspace, Report report)
    : wxDialog(parent, wxID_ANY, _L("Welcome back"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_theme(theme)
    , m_report(std::move(report))
    , m_pump(this, kPumpTimerId)
{
    SetName("JusPrin Welcome back");
    // The Preferences window's size: the same kind of window over the app.
    SetClientSize(FromDIP(wxSize(theme.metrics().preferences.width, theme.metrics().preferences.height)));
    SetSizer(new wxBoxSizer(wxVERTICAL));

    // OrcaSlicer has loaded the imported presets by now; one it could not
    // load is listed as not usable rather than as imported.
    const PresetBundle& bundle = *wxGetApp().preset_bundle;
    for (const std::string& name : m_report.printers)
        if (bundle.printers.find_preset(name) == nullptr)
            m_unusable.push_back(name);
    for (const std::string& name : m_report.presets)
        if (bundle.filaments.find_preset(name) == nullptr && bundle.prints.find_preset(name) == nullptr)
            m_unusable.push_back(name);

    // Its own document, never the project's: the window holds no chat.
    Agent::ProjectPersistence::Config storage;
    storage.in_memory = true;
    m_persistence     = std::make_unique<Agent::ProjectPersistence>(workspace, std::move(storage));

    // The setup service Preferences uses, so the account v1 signed in to is
    // checked, and signed in to again, where Preferences would.
    Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
    m_view = new AgentWebView(this, theme, workspace, *m_persistence, runtime.availability, {}, runtime.setup,
                              AgentPageMode::WelcomeBack);
    m_view->SetName(_L("Welcome back"));
    GetSizer()->Add(m_view, 1, wxEXPAND);

    Agent::AgentHost& host = m_view->host();
    host.set_session_state_provider([this] { return state(); });
    host.set_page_message_handler([this](const std::string& type, const json& payload) { return on_page_message(type, payload); });

    Bind(wxEVT_TIMER, &WelcomeBackWindow::on_pump, this, kPumpTimerId);
    m_pump.Start(kPumpIntervalMs);
    const bool dark = GUI_App::dark_mode();
    SetBackgroundColour(m_theme.palette(dark).surface_raised);
    m_view->apply_appearance(dark);
    wxGetApp().UpdateDlgDarkUI(this);
    CentreOnParent();
}

WelcomeBackWindow::~WelcomeBackWindow()
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

json WelcomeBackWindow::state() const
{
    json payload        = to_json(m_report);
    payload["kind"]     = "welcome_back";
    payload["unusable"] = m_unusable;
    return payload;
}

bool WelcomeBackWindow::on_page_message(const std::string& type, const json& payload)
{
    if (type == Agent::Protocol::kShellAction && payload.is_object() && payload.value("action", std::string()) == "close") {
        CallAfter([this] { EndModal(wxID_OK); });
        return true;
    }
    return false;
}

void WelcomeBackWindow::on_pump(wxTimerEvent&)
{
    if (m_view == nullptr)
        return;
    m_view->host().pump_setup();
}

} // namespace Slic3r::GUI::JusPrin::V1Import
