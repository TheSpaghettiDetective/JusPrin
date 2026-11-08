// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "ServerSheet.hpp"

#include "HeaderControls.hpp"
#include "ShellRecipes.hpp"
#include "ShellTheme.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/webview.h>

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

namespace {

// How long the link and the page together may take before the sheet says the
// page could not be loaded; WebKit's own limit is over a minute.
constexpr int kLoadDeadlineMs   = 20000;
// How long to wait before trying again to destroy a closed sheet whose web
// view was installing its script handler (see release()).
constexpr int kReleaseRetryMs   = 50;
constexpr int kHeaderHeightDip  = 48;

ServerSheet* g_sheet = nullptr;

wxString page_title(const std::string& page)
{
    return page == "billing" ? _L("Billing and usage") : _L("Credits and plans");
}

// The server's answer to POST /v2/web_link, on the GUI thread: a URL, or empty
// when the server could not be reached or refused (its sign-in has ended).
// Signed out, the app asks without a token, and the link is to Credits and
// plans as someone without an account sees it.
void request_web_link(const std::string& page, const std::string& from, std::function<void(std::string)> done)
{
    AppConfig*        config = wxGetApp().app_config;
    const std::string token  = Agent::cloud_account_token(config);
    auto finish = [done = std::move(done)](std::string url) {
        wxGetApp().CallAfter([done, url = std::move(url)] { done(url); });
    };
    Http http = Http::post(Agent::cloud_web_link_url(config));
    if (!token.empty())
        http.header("Authorization", "Bearer " + token);
    http.header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .set_post_body(nlohmann::json{{"page", page}, {"from", from}}.dump())
        .tls_verify(true)
        .timeout_connect(10)
        .timeout_max(30)
        .on_complete([finish](std::string body, unsigned status) {
            const nlohmann::json answer = nlohmann::json::parse(body, nullptr, false);
            std::string          url = status == 200 && answer.is_object() ? answer.value("url", std::string()) : std::string();
            if (url.empty())
                BOOST_LOG_TRIVIAL(warning) << "JusPrin page link answered " << status;
            finish(std::move(url));
        })
        .on_error([finish](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(warning) << "JusPrin page link failed: " << status << " " << error;
            finish({});
        })
        .perform();
}

} // namespace

void ServerSheet::open(wxWindow* parent, const std::string& page, const std::string& from, std::function<void()> closed)
{
    // Without the design tokens there is no sheet to draw; the page opens in
    // the person's browser, signed in the same way.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr) {
        request_web_link(page, from, [closed](const std::string& url) {
            if (!url.empty())
                wxLaunchDefaultBrowser(wxString::FromUTF8(url));
            if (closed)
                closed();
        });
        return;
    }
    // Nothing outside the sheet takes a click while it is open; this is for
    // a second request the same moment.
    if (g_sheet != nullptr) {
        g_sheet->show_page(page, from);
        return;
    }
    auto* sheet = new ServerSheet(parent, *theme);
    g_sheet     = sheet;
    sheet->show_page(page, from);
    sheet->ShowModal();
    g_sheet = nullptr;
    // The opener reads the account again: a purchase may have gone through.
    if (closed)
        closed();
    sheet->release();
}

ServerSheet* ServerSheet::current() { return g_sheet != nullptr && !g_sheet->m_closing ? g_sheet : nullptr; }

ServerSheet::ServerSheet(wxWindow* parent, const ShellTheme& theme)
    : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_theme(theme)
{
    SetName("Server sheet");
    const ShellPalette& palette = theme.palette(wxGetApp().dark_mode());
    SetBackgroundColour(palette.surface_raised);

    // One child for release() to destroy, holding the title row, the page and
    // what shows in its place.
    m_body = new wxPanel(this);
    m_body->SetBackgroundColour(palette.surface_raised);
    wxWindow* header = build_header();
    m_view           = WebView::CreateWebView(m_body, wxEmptyString);

    m_status        = new wxPanel(m_body);
    m_status_title  = new wxStaticText(m_status, wxID_ANY, wxEmptyString);
    m_status_detail = new wxStaticText(m_status, wxID_ANY, _L("Check your internet connection, then try again."));
    m_retry         = new Button(m_status, _L("Try again"));
    m_retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { load(); });
    m_status->SetBackgroundColour(palette.surface_canvas);
    style_label(*m_status_title, theme, TextRole::BodyBold, palette.text_primary);
    style_label(*m_status_detail, theme, TextRole::Body, palette.text_secondary);
    style_primary_button(*m_retry, theme, palette, theme.metrics().button.primary);
    auto* block = new wxBoxSizer(wxVERTICAL);
    block->Add(m_status_title, 0, wxALIGN_CENTER_HORIZONTAL);
    block->Add(m_status_detail, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));
    block->Add(m_retry, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(24));
    auto* centred = new wxBoxSizer(wxVERTICAL);
    centred->AddStretchSpacer();
    centred->Add(block, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(48));
    centred->AddStretchSpacer();
    m_status->SetSizer(centred);

    auto* body = new wxBoxSizer(wxVERTICAL);
    body->Add(header, 0, wxEXPAND);
    body->Add(m_view, 1, wxEXPAND);
    body->Add(m_status, 1, wxEXPAND);
    m_body->SetSizer(body);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_body, 1, wxEXPAND);
    SetSizer(sizer);

    if (m_view != nullptr) {
        m_view->Bind(wxEVT_WEBVIEW_NAVIGATING, &ServerSheet::on_navigating, this);
        m_view->Bind(wxEVT_WEBVIEW_LOADED, &ServerSheet::on_loaded, this);
        m_view->Bind(wxEVT_WEBVIEW_ERROR, &ServerSheet::on_error, this);
        // A link the page opens in a new window goes to the person's browser:
        // the sheet holds one page.
        m_view->Bind(wxEVT_WEBVIEW_NEWWINDOW, [](wxWebViewEvent& event) { wxLaunchDefaultBrowser(event.GetURL()); });
        // WebView::CreateWebView gives every page the app's message bridge; a
        // server page's messages end here and reach nothing.
        m_view->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [](wxWebViewEvent&) {});
    }
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { close(); });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE)
            close();
        else
            event.Skip();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { if (!m_closing) set_state(State::Failed); }, m_load_deadline.GetId());
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { release(); }, m_release_timer.GetId());

    // As wide as the boards draw it, and never taller than the screen it is on.
    wxSize size = FromDIP(wxSize(760, 760));
    if (const int display = wxDisplay::GetFromWindow(parent); display != wxNOT_FOUND)
        size.SetHeight(std::min(size.GetHeight(), wxDisplay(display).GetClientArea().GetHeight() * 9 / 10));
    SetSize(size);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

ServerSheet::~ServerSheet()
{
    *m_alive = false;
    if (g_sheet == this)
        g_sheet = nullptr;
}

wxWindow* ServerSheet::build_header()
{
    const ShellMetrics& m       = m_theme.metrics();
    const bool          dark    = wxGetApp().dark_mode();
    const ShellPalette& palette = m_theme.palette(dark);
    auto*               header  = new wxPanel(m_body, wxID_ANY);
    header->SetBackgroundStyle(wxBG_STYLE_PAINT);
    header->SetMinSize(wxSize(-1, FromDIP(kHeaderHeightDip)));
    header->Bind(wxEVT_PAINT, [this, header](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(header);
        const ShellPalette&   p    = m_theme.palette(wxGetApp().dark_mode());
        const wxSize          size = header->GetClientSize();
        dc.SetBackground(wxBrush(p.surface_raised));
        dc.Clear();
        dc.SetPen(wxPen(p.border_subtle));
        dc.DrawLine(0, size.y - 1, size.x, size.y - 1);
    });

    m_title = new wxStaticText(header, wxID_ANY, wxEmptyString);
    style_label(*m_title, m_theme, TextRole::BodyBold, palette.text_primary);
    m_title->SetBackgroundColour(palette.surface_raised);
    m_browser = new HeaderButton(header, m_theme, HeaderStyle::Quiet, _L("Open in browser"), HeaderIcon::None);
    m_browser->set_dark(dark);
    m_browser->set_surface(HeaderSurface::Raised);
    m_browser->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_in_browser(); });
    m_close = new HeaderButton(header, m_theme, HeaderStyle::Quiet, wxEmptyString, HeaderIcon::Cancel);
    m_close->SetName(_L("Close"));
    m_close->SetToolTip(_L("Close"));
    m_close->set_dark(dark);
    m_close->set_surface(HeaderSurface::Raised);
    m_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { close(); });

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(FromDIP(m.space_4));
    row->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_4));
    row->Add(m_browser, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_2));
    row->Add(m_close, 0, wxALIGN_CENTER_VERTICAL);
    row->AddSpacer(FromDIP(m.space_2));
    header->SetSizer(row);
    return header;
}

void ServerSheet::show_page(const std::string& page, const std::string& from)
{
    m_page = page;
    m_from = from;
    SetTitle(page_title(page));
    m_title->SetLabel(page_title(page));
    load();
}

void ServerSheet::load()
{
    set_state(State::Loading);
    m_load_deadline.StartOnce(kLoadDeadlineMs);
    request_link([this](std::string url) {
        if (url.empty() || m_view == nullptr) {
            m_load_deadline.Stop();
            set_state(State::Failed);
            return;
        }
        m_view->LoadURL(wxString::FromUTF8(url));
    });
}

void ServerSheet::request_link(std::function<void(std::string)> use)
{
    const unsigned request = ++m_request;
    request_web_link(m_page, m_from, [this, alive = std::weak_ptr<bool>(m_alive), request, use = std::move(use)](std::string url) {
        if (!alive.expired() && !m_closing && request == m_request)
            use(std::move(url));
    });
}

void ServerSheet::open_in_browser()
{
    // A fresh link: the one the sheet opened has been used.
    request_link([this](std::string url) {
        if (url.empty()) {
            set_state(State::Failed);
            return;
        }
        wxLaunchDefaultBrowser(wxString::FromUTF8(url));
        close();
    });
}

void ServerSheet::set_state(State state)
{
    m_state = state;
    if (state == State::Loading)
        m_status_title->SetLabel(wxString::Format(_L("Loading %s…"), page_title(m_page)));
    else if (state == State::Failed)
        m_status_title->SetLabel(wxString::Format(_L("Couldn't load %s"), page_title(m_page)));
    m_status_detail->Show(state == State::Failed);
    m_retry->Show(state == State::Failed);
    if (m_view != nullptr)
        m_view->Show(state == State::Page);
    m_status->Show(state != State::Page);
    m_body->Layout();
}

void ServerSheet::on_navigating(wxWebViewEvent& event)
{
    if (!Agent::is_cloud_web_done(wxGetApp().app_config, event.GetURL().ToUTF8().data()))
        return;
    event.Veto();
    // Not from inside the web view's own callback: closing destroys it.
    CallAfter([this] { close(); });
}

void ServerSheet::on_loaded(wxWebViewEvent& event)
{
    if (event.GetURL().empty() || event.GetURL() == "about:blank")
        return;
    m_load_deadline.Stop();
    set_state(State::Page);
}

void ServerSheet::on_error(wxWebViewEvent& event)
{
    // A new load, or the done address the sheet turned away, cancels the one
    // before it: not a failure.
    if (event.GetInt() == wxWEBVIEW_NAV_ERR_USER_CANCELLED || m_closing)
        return;
    m_load_deadline.Stop();
    set_state(State::Failed);
}

void ServerSheet::close()
{
    if (m_closing)
        return;
    m_closing = true;
    m_load_deadline.Stop();
    if (IsModal())
        EndModal(wxID_CANCEL);
}

// On macOS a new web view installs its script message handler from a
// CallAfter (WebView::CreateWebView), and a view destroyed while that install
// waits is called back after it is gone; see PopOutWindow::release. The body,
// and the web view in it, is deleted at a moment no install is running.
void ServerSheet::release()
{
    if (wxGetApp().is_adding_script_handler()) {
        Hide();
        m_release_timer.StartOnce(kReleaseRetryMs);
        return;
    }
    if (m_body != nullptr) {
        m_body->Destroy();
        m_body = nullptr;
        m_view = nullptr;
    }
    Destroy();
}

} // namespace Slic3r::GUI::JusPrin
