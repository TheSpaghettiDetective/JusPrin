#include "AgentWebView.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/filesys.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/webview.h>

namespace Slic3r::GUI::JusPrin {

namespace {

// Streaming cadence. The host emits one delta per tick; ticks with no active
// stream are no-ops.
constexpr int kStreamIntervalMs = 33;

// How long the page may take to load and complete the hello handshake before
// the internal-connection error is shown.
constexpr int kHandshakeDeadlineMs = 20000;

constexpr int kStreamTimerId    = wxID_HIGHEST + 1801;
constexpr int kHandshakeTimerId = wxID_HIGHEST + 1802;

boost::filesystem::path agent_page_path()
{
    return boost::filesystem::path(resources_dir()) / "jusprin" / "agent" / "index.html";
}

} // namespace

AgentWebView::AgentWebView(wxWindow*                  parent,
                           const ShellTheme&          theme,
                           Workspace::IWorkspace&     workspace,
                           Agent::ProjectPersistence& persistence,
                           Agent::AgentAvailability   availability,
                           Agent::AgentServicePtr      agent,
                           Agent::AgentSetupServicePtr setup)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_host(std::make_unique<Agent::AgentHost>(workspace, persistence, availability, GUI_App::dark_mode(),
                                                std::move(agent), std::move(setup)))
    , m_stream_timer(this, kStreamTimerId)
    , m_handshake_timer(this, kHandshakeTimerId)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);

    // Internal-connection error surface; hidden until a failure occurs.
    m_error_panel = new wxPanel(this, wxID_ANY);
    auto* error_sizer = new wxBoxSizer(wxVERTICAL);
    m_error_title = new wxStaticText(m_error_panel, wxID_ANY, _L("The Agent panel could not connect"));
    m_error_title->SetFont(Label::Head_14);
    m_error_detail = new wxStaticText(m_error_panel, wxID_ANY, wxEmptyString);
    m_error_detail->SetFont(Label::Body_12);
    auto* retry_button = new Button(m_error_panel, _L("Retry"));
    retry_button->SetFont(Label::Body_12);
    retry_button->SetCornerRadius(FromDIP(8));
    retry_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { reload(); });
    error_sizer->AddSpacer(FromDIP(24));
    error_sizer->Add(m_error_title, 0, wxLEFT | wxRIGHT, FromDIP(16));
    error_sizer->AddSpacer(FromDIP(8));
    error_sizer->Add(m_error_detail, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(16));
    error_sizer->AddSpacer(FromDIP(16));
    error_sizer->Add(retry_button, 0, wxLEFT, FromDIP(16));
    m_error_panel->SetSizer(error_sizer);
    m_error_panel->Hide();
    sizer->Add(m_error_panel, 1, wxEXPAND);

    const boost::filesystem::path page = agent_page_path();
    if (!boost::filesystem::exists(page)) {
        show_bridge_error(_L("The packaged Agent page is missing from this build."));
    } else {
        m_page_url = wxFileSystem::FileNameToURL(wxFileName(wxString::FromUTF8(page.string())));
        m_webview  = WebView::CreateWebView(this, m_page_url);
        if (m_webview == nullptr) {
            show_bridge_error(_L("The system web view could not be created."));
        } else {
            sizer->Add(m_webview, 1, wxEXPAND);
            m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &AgentWebView::on_script_message, this);
            m_webview->Bind(wxEVT_WEBVIEW_ERROR, &AgentWebView::on_load_error, this);
            m_webview->Bind(wxEVT_WEBVIEW_NAVIGATING, [this](wxWebViewEvent& event) {
                // Every (re)load needs a fresh handshake before other traffic.
                m_host->reset_page();
                m_handshake_timer.StartOnce(kHandshakeDeadlineMs);
                event.Skip();
            });

            m_host->set_send([this](const std::string& envelope) {
                if (m_webview == nullptr)
                    return;
                wxString script = "window.__jusprinBridge && window.__jusprinBridge.deliver(";
                script += wxString::FromUTF8(envelope);
                script += ")";
                WebView::RunScript(m_webview, script);
            });
            m_host->set_handshake_listener([this]() {
                m_handshake_timer.Stop();
                hide_bridge_error();
            });
            m_handshake_timer.StartOnce(kHandshakeDeadlineMs);
        }
    }

    Bind(wxEVT_TIMER, [this](wxTimerEvent& event) {
        if (event.GetId() == kHandshakeTimerId)
            on_handshake_deadline(event);
        else if (event.GetId() == kStreamTimerId) {
            m_host->pump_stream();
            m_host->pump_tools();
            m_host->pump_setup();
        } else
            event.Skip();
    });
    m_stream_timer.Start(kStreamIntervalMs);

    apply_appearance(GUI_App::dark_mode());
}

AgentWebView::~AgentWebView()
{
    m_stream_timer.Stop();
    m_handshake_timer.Stop();
    // The webview outlives this frame's teardown callbacks in wx's child
    // destruction order; drop the transport first so the host cannot run
    // script against a dying view.
    m_host->set_send({});
    m_host->set_handshake_listener({});
}

void AgentWebView::on_script_message(wxWebViewEvent& event)
{
    m_host->on_page_message(event.GetString().ToUTF8().data());
}

void AgentWebView::on_load_error(wxWebViewEvent& event)
{
    m_last_load_error = event.GetString();
    BOOST_LOG_TRIVIAL(error) << "JusPrin agent page failed to load: " << m_last_load_error.ToUTF8();
    show_bridge_error(_L("The Agent page failed to load."));
}

void AgentWebView::on_handshake_deadline(wxTimerEvent&)
{
    if (!m_host->handshake_complete())
        show_bridge_error(_L("The Agent page did not complete the bridge handshake."));
}

void AgentWebView::reload()
{
    hide_bridge_error();
    m_last_load_error.clear();
    if (m_webview != nullptr && !m_page_url.empty()) {
        m_host->reset_page();
        m_handshake_timer.StartOnce(kHandshakeDeadlineMs);
        WebView::LoadUrl(m_webview, m_page_url);
    } else {
        // Without a webview (missing bundle or creation failure) retry can
        // only re-check the packaged page.
        const boost::filesystem::path page = agent_page_path();
        if (boost::filesystem::exists(page))
            show_bridge_error(_L("Restart the application to load the Agent page."));
        else
            show_bridge_error(_L("The packaged Agent page is missing from this build."));
    }
}

wxString AgentWebView::diagnostics_text(const wxString& reason) const
{
    wxString text = reason;
    text += "\n\n";
    text += _L("This is an internal connection between the application and its Agent panel, not a network service.");
    text += "\n";
    text += wxString::Format("URL: %s", m_page_url.empty() ? wxString(agent_page_path().string()) : m_page_url);
    if (!m_last_load_error.empty())
        text += wxString::Format("\n%s: %s", _L("Last load error"), m_last_load_error);
    text += wxString::Format("\n%s: %llu %s / %llu %s", _L("Bridge traffic"),
                             static_cast<unsigned long long>(m_host->messages_sent()), _L("sent"),
                             static_cast<unsigned long long>(m_host->messages_received()), _L("received"));
    return text;
}

void AgentWebView::show_bridge_error(const wxString& reason)
{
    m_error_detail->SetLabel(diagnostics_text(reason));
    m_error_detail->Wrap(std::max(FromDIP(160), GetClientSize().GetWidth() - FromDIP(32)));
    if (m_webview != nullptr)
        m_webview->Hide();
    m_error_panel->Show();
    Layout();
}

void AgentWebView::hide_bridge_error()
{
    if (!m_error_panel->IsShown())
        return;
    m_error_panel->Hide();
    if (m_webview != nullptr)
        m_webview->Show();
    Layout();
}

void AgentWebView::apply_appearance(bool dark)
{
    const ShellPalette& palette = m_theme.palette(dark);
    SetBackgroundColour(palette.surface_subtle);
    m_error_panel->SetBackgroundColour(palette.surface_subtle);
    m_error_title->SetForegroundColour(palette.text_primary);
    m_error_detail->SetForegroundColour(palette.text_secondary);
    m_host->set_appearance(dark);
    Refresh();
}

} // namespace Slic3r::GUI::JusPrin
