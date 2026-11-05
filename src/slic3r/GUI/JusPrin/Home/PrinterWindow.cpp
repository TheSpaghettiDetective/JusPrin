// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "PrinterWindow.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellRecipes.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/uri.h>
#include <wx/webview.h>

#include <algorithm>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

// How long a page may take to load before the window says it did not. A
// printer on the local network answers within seconds; WebKit's own limit,
// over a minute, is far longer than anyone watches a white window.
constexpr int kLoadDeadlineMs = 20000;

// PrusaLink and PrusaConnect pages sign in with the saved API key, as
// MainFrame::load_printer_url passes it to the Device tab.
wxString page_api_key(const DynamicPrintConfig& config)
{
    const auto* type = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (type == nullptr || !config.has("printhost_apikey") || (type->value != htPrusaLink && type->value != htPrusaConnect))
        return {};
    return wxString::FromUTF8(config.opt_string("printhost_apikey"));
}

// "192.168.1.42:7125" for "http://user:key@192.168.1.42:7125/": where the
// window looked, without the scheme, a path, or any credentials.
std::string page_address(const std::string& url)
{
    const wxURI uri(wxString::FromUTF8(url));
    wxString    address = uri.GetServer();
    if (uri.HasPort())
        address += ":" + uri.GetPort();
    return address.empty() ? url : std::string(address.ToUTF8());
}

} // namespace

std::string PrinterWindow::page_url(const DynamicPrintConfig& config)
{
    // get_print_host_webui takes a mutable config; it only reads it.
    DynamicPrintConfig copy = config;
    return PrintHost::get_print_host_webui(&copy);
}

PrinterWindow::PrinterWindow(const std::string& printer_name, const DynamicPrintConfig& config)
    : PopOutWindow(wxString::FromUTF8(printer_name))
    , m_load_deadline(this)
    , m_url(page_url(config))
    , m_address(page_address(m_url))
    , m_api_key(page_api_key(config))
{
    // One child for the window to destroy at close (PopOutWindow::release),
    // holding the page and, in its place when it does not load, the problem.
    m_body = new wxPanel(this);
    m_view = new PrinterWebView(m_body);

    m_problem        = new wxPanel(m_body);
    m_problem_title  = new wxStaticText(m_problem, wxID_ANY, _L("The printer's page didn't load"));
    m_problem_detail = new wxStaticText(m_problem, wxID_ANY, wxEmptyString);
    m_retry          = new Button(m_problem, _L("Try again"));
    m_retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { load(); });
    auto* block = new wxBoxSizer(wxVERTICAL);
    block->Add(m_problem_title, 0, wxALIGN_CENTER_HORIZONTAL);
    block->Add(m_problem_detail, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));
    block->Add(m_retry, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(24));
    auto* centred = new wxBoxSizer(wxVERTICAL);
    centred->AddStretchSpacer();
    centred->Add(block, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(48));
    centred->AddStretchSpacer();
    m_problem->SetSizer(centred);
    m_problem->Hide();

    auto* body = new wxBoxSizer(wxVERTICAL);
    body->Add(m_view, 1, wxEXPAND);
    body->Add(m_problem, 1, wxEXPAND);
    m_body->SetSizer(body);
    set_content(m_body);
    apply_appearance();

    // The browser inside PrinterWebView handles its own load events without
    // passing them on, so the window listens on the browser itself. Handlers
    // bound later run first; these skip on, and PrinterWebView's still run.
    for (wxWindow* child : m_view->GetChildren())
        if (auto* browser = dynamic_cast<wxWebView*>(child)) {
            browser->Bind(wxEVT_WEBVIEW_LOADED, &PrinterWindow::on_loaded, this);
            browser->Bind(wxEVT_WEBVIEW_ERROR, &PrinterWindow::on_error, this);
        }
    // A closing window has already let its page go (PopOutWindow::release).
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { if (!closing()) show_problem(); }, m_load_deadline.GetId());

    Show();
    // PrinterWebView defers a load until it is shown, so the load follows Show.
    load();
}

void PrinterWindow::show_printer(const std::string& printer_name, const DynamicPrintConfig& config)
{
    SetTitle(wxString::FromUTF8(printer_name));
    if (const std::string url = page_url(config); url != m_url) {
        m_url     = url;
        m_address = page_address(url);
        m_api_key = page_api_key(config);
        load();
    }
    bring_forward();
}

bool PrinterWindow::problem_shown() const { return m_problem->IsShown(); }

void PrinterWindow::load()
{
    hide_problem();
    wxString url = wxString::FromUTF8(m_url);
    m_view->load_url(url, m_api_key);
    m_load_deadline.StartOnce(kLoadDeadlineMs);
}

void PrinterWindow::on_loaded(wxWebViewEvent& event)
{
    event.Skip();
    // A page that comes late still replaces the problem.
    if (event.GetURL().empty() || event.GetURL() == "about:blank")
        return;
    m_load_deadline.Stop();
    hide_problem();
}

void PrinterWindow::on_error(wxWebViewEvent& event)
{
    event.Skip();
    // Try again, or a new address, cancels the load before it: not a failure.
    if (event.GetInt() == wxWEBVIEW_NAV_ERR_USER_CANCELLED)
        return;
    m_load_deadline.Stop();
    show_problem();
}

void PrinterWindow::show_problem()
{
    if (m_problem->IsShown())
        return;
    // A new label each time: wx's Wrap() keeps the first text it wrapped and
    // skips a width it has already wrapped at, so a reused one comes back
    // unwrapped, or with the old address.
    auto* detail = new wxStaticText(m_problem, wxID_ANY, wxString::Format(
        _L("Nothing answered at %s. Check that the printer is turned on and on the same network as this computer, then try again."),
        wxString::FromUTF8(m_address)));
    m_problem_detail->GetContainingSizer()->Replace(m_problem_detail, detail);
    m_problem_detail->Destroy();
    m_problem_detail = detail;
    apply_appearance();
    m_problem_detail->Wrap(std::max(FromDIP(240), GetClientSize().GetWidth() / 2));
    m_view->Hide();
    m_problem->Show();
    m_body->Layout();
}

void PrinterWindow::hide_problem()
{
    if (!m_problem->IsShown())
        return;
    m_problem->Hide();
    m_view->Show();
    m_body->Layout();
}

void PrinterWindow::apply_appearance()
{
    // Without the design tokens the app keeps OrcaSlicer's own look, and so
    // do these controls.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr)
        return;
    const ShellPalette& palette = theme->palette(wxGetApp().dark_mode());
    m_problem->SetBackgroundColour(palette.surface_canvas);
    style_label(*m_problem_title, *theme, TextRole::BodyBold, palette.text_primary);
    style_label(*m_problem_detail, *theme, TextRole::Body, palette.text_secondary);
    style_primary_button(*m_retry, *theme, palette, theme->metrics().button.primary);
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
