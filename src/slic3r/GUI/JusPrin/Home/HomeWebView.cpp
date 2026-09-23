// Before every other include: slic3r/GUI/I18N.hpp defines _L only while the
// _ macro is still undefined, and this panel's own header brings in wx/panel.h,
// which defines it.
#include "slic3r/GUI/I18N.hpp"

#include "HomeWebView.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellRecipes.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <cstdlib>

#include <wx/filesys.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
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
        return wxString::FromUTF8(dev);
    const boost::filesystem::path page = home_page_path();
    if (!boost::filesystem::exists(page))
        return {};
    return wxFileSystem::FileNameToURL(wxFileName(wxString::FromUTF8(page.string())));
}

} // namespace

HomeWebView::HomeWebView(wxWindow* parent, const ShellTheme& theme, MainFrame& frame, Workspace::SpoolStore* spools)
    : wxPanel(parent, wxID_ANY), m_theme(theme)
{
    m_backend = std::make_unique<OrcaHomeBackend>(frame, spools);
    m_host    = std::make_unique<HomeHost>(*m_backend, [this](const std::string& envelope) {
        if (m_webview == nullptr)
            return;
        wxString script = "window.__jusprinBridge && window.__jusprinBridge.deliver(";
        script += wxString::FromUTF8(envelope);
        script += ")";
        WebView::RunScript(m_webview, script);
    });

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
    m_webview = WebView::CreateWebView(this, url);
    if (m_webview == nullptr) {
        show_page_error(_L("The system web view could not be created."));
        return;
    }
    // The page fills the row; a panel the shell attaches sits beside it,
    // where the page's own printers column is.
    m_row = new wxBoxSizer(wxHORIZONTAL);
    m_row->Add(m_webview, 1, wxEXPAND);
    sizer->Add(m_row, 1, wxEXPAND);
    m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &HomeWebView::on_script_message, this);
    m_webview->Bind(wxEVT_WEBVIEW_ERROR, &HomeWebView::on_load_error, this);
    m_webview->Bind(wxEVT_WEBVIEW_NAVIGATING, [this](wxWebViewEvent& event) {
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

void HomeWebView::attach_side_panel(wxWindow* panel)
{
    if (m_row == nullptr)
        return;
    if (m_side_panel != nullptr)
        m_row->Detach(m_side_panel);
    m_side_panel = panel;
    if (m_side_panel != nullptr) {
        m_row->Add(m_side_panel, 1, wxEXPAND);
        m_side_panel->Hide();
    }
    Layout();
}

void HomeWebView::show_side_panel(bool shown)
{
    if (m_side_panel == nullptr)
        return;
    m_side_panel->Show(shown);
    m_webview->Show(!shown);
    m_error->Show(!shown && !m_error->GetLabel().empty());
    Layout();
}

void HomeWebView::on_script_message(wxWebViewEvent& event)
{
    m_host->on_page_message(event.GetString().ToUTF8().data());
}

void HomeWebView::on_load_error(wxWebViewEvent& event)
{
    BOOST_LOG_TRIVIAL(error) << "JusPrin Home page failed to load: " << event.GetString().ToUTF8();
    show_page_error(_L("The Home page failed to load."));
}

void HomeWebView::show_page_error(const wxString& reason)
{
    m_error->SetLabel(reason);
    m_error->Show();
    Layout();
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
