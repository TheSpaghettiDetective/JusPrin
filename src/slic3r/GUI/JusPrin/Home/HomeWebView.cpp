// Before every other include: slic3r/GUI/I18N.hpp defines _L only while the
// _ macro is still undefined, and this panel's own header brings in wx/panel.h,
// which defines it.
#include "slic3r/GUI/I18N.hpp"

#include "HomeWebView.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Printers/HostLaneReader.hpp"
#include "slic3r/GUI/JusPrin/Shell/DroppedFileUrl.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellRecipes.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/JusPrin/Support/ErrorReport.hpp"
#include "slic3r/GUI/JusPrin/Support/PageLanguage.hpp"
#include "slic3r/GUI/JusPrin/Support/PageScheme.hpp"
#include "slic3r/GUI/JusPrin/Support/SentryClient.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <optional>
#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/toplevel.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

boost::filesystem::path home_page_path()
{
    return boost::filesystem::path(resources_dir()) / "jusprin" / "home" / "index.html";
}

wxString home_page_url()
{
    if (const char* dev = std::getenv("JUSPRIN_HOME_DEV_URL"); dev != nullptr && *dev != '\0')
        return with_page_language(wxString::FromUTF8(dev));
    const wxString page = page_url("home");
    return page.empty() ? page : with_page_language(page);
}

} // namespace

HomeWebView::HomeWebView(wxWindow* parent, const ShellTheme& theme, MainFrame& frame)
    : wxPanel(parent, wxID_ANY), m_theme(theme), m_live_timer(this), m_poll_timer(this)
{
    Bind(wxEVT_TIMER, reported("refreshing Home", [this](wxTimerEvent& event) { on_live_tick(event); }), m_live_timer.GetId());
    Bind(wxEVT_TIMER, reported("refreshing Home", [this](wxTimerEvent& event) { on_poll_tick(event); }), m_poll_timer.GetId());
    m_backend = std::make_unique<OrcaHomeBackend>(frame);
    m_host    = std::make_unique<HomeHost>(*m_backend, [this](const std::string& envelope) {
        if (m_webview == nullptr)
            return;
        wxString script = "window.__jusprinBridge && window.__jusprinBridge.deliver(";
        script += wxString::FromUTF8(envelope);
        script += ")";
        WebView::RunScript(m_webview, script);
    });
    m_host->set_step_listener([](const std::string& type) { sentry_breadcrumb("page", "Home " + type); });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);

    m_error = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_error->Hide();
    sizer->Add(m_error, 0, wxALL, FromDIP(16));

    apply_appearance(GUI_App::dark_mode());

    const wxString url = home_page_url();
    if (url.empty()) {
        show_page_error(_L("The packaged Home page is missing from this build."));
        return;
    }
    m_webview = WebView::CreateWebView(this, url, page_handlers());
    if (m_webview == nullptr) {
        show_page_error(_L("The system web view could not be created."));
        return;
    }
    sizer->Add(m_webview, 1, wxEXPAND);
    m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &HomeWebView::on_script_message, this);
    m_webview->Bind(wxEVT_WEBVIEW_ERROR, &HomeWebView::on_load_error, this);
    m_webview->Bind(wxEVT_WEBVIEW_NAVIGATING, [this](wxWebViewEvent& event) {
        // A file let go on Home that the page never saw (DroppedFileUrl.hpp):
        // Home stays, and the file opens as the page's own drop would open it.
        if (const std::optional<wxString> file =
                dropped_file(event.GetURL(), wxString::FromUTF8(home_page_path().string()))) {
            event.Veto();
            // After the drop has finished: opening shows dialogs and switches
            // screens, which must not run inside the web view's callback.
            CallAfter(reported("opening a dropped file", [this, path = *file]() { m_backend->open_dropped_path(path); }));
            return;
        }
        // Every (re)load needs a fresh handshake before other traffic.
        m_host->reset_page();
        event.Skip();
    });
}

HomeWebView::~HomeWebView()
{
    // The webview outlives this frame's teardown callbacks in wx's child
    // destruction order; drop the host first so it cannot run script against a
    // dying view, then the backend it reads through.
    m_live_timer.Stop();
    m_poll_timer.Stop();
    m_host.reset();
    m_backend.reset();
}

void HomeWebView::apply_appearance(bool dark)
{
    const ShellPalette& palette = m_theme.palette(dark);
    SetBackgroundColour(palette.surface_canvas);
    style_label(*m_error, m_theme, TextRole::Body, palette.text_secondary);
    m_host->push_appearance(dark);
    Refresh();
}

void HomeWebView::refresh(const std::string& added) { m_host->push_state(added); }

void HomeWebView::set_live(bool live)
{
    if (!live) {
        m_live_timer.Stop();
        m_poll_timer.Stop();
    } else if (!m_live_timer.IsRunning()) {
        m_live_timer.Start(1000);
        m_poll_timer.Start(100);
    }
}

void HomeWebView::on_poll_tick(wxTimerEvent&) { m_host->poll(); }

void HomeWebView::on_live_tick(wxTimerEvent&)
{
    // Nothing to update while the printer conversation covers the page or the
    // window is minimized. The first tick after either is over catches up:
    // the comparison is against what was last sent, not the last tick.
    if (const auto* top = dynamic_cast<const wxTopLevelWindow*>(wxGetTopLevelParent(this)); top != nullptr && top->IsIconized())
        return;
    // Print hosts are read on their own schedule, and the conversation that
    // covers the page reads them too, so they are kept fresh either way.
    Printers::refresh_host_readings();
    if (m_webview == nullptr || !m_webview->IsShownOnScreen())
        return;
    m_host->refresh_if_changed();
}

void HomeWebView::on_script_message(wxWebViewEvent& event)
{
    const std::string text = event.GetString().ToUTF8().data();
    // The top of this entry point (AGENTS.md, Error handling): a failure in
    // handling the message ends here, where it is reported. The message's
    // type says what the app was doing.
    try {
        m_host->on_page_message(text);
    } catch (...) {
        const nlohmann::json envelope = nlohmann::json::parse(text, nullptr, false);
        const std::string    type     = envelope.is_object() ? envelope.value("type", std::string()) : std::string();
        report_unexpected_error("handling the Home page's " + (type.empty() ? std::string("unreadable") : type) + " message",
                                describe_current_exception());
    }
}

void HomeWebView::on_load_error(wxWebViewEvent& event)
{
    BOOST_LOG_TRIVIAL(error) << "JusPrin Home page failed to load: " << event.GetString().ToUTF8();
    show_page_error(_L("The Home page failed to load."));
}

void HomeWebView::show_page_error(const wxString& reason)
{
    BOOST_LOG_TRIVIAL(error) << "JusPrin Home page shows its error: " << reason.ToUTF8().data();
    m_error->SetLabel(reason);
    m_error->Show();
    Layout();
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
