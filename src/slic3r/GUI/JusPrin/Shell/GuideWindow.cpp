// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "GuideWindow.hpp"

#include "ShellTheme.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include <nlohmann/json.hpp>

#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/utils.h>

#include <string>

namespace Slic3r::GUI::JusPrin {

using nlohmann::json;

GuideWindow::GuideWindow(wxWindow* parent, const ShellTheme& theme, Workspace::IWorkspace& workspace)
    : wxDialog(parent, wxID_ANY, _L("JusPrin guide"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_theme(theme)
    , m_release([this] { release(); })
{
    SetName("JusPrin guide");
    SetClientSize(FromDIP(wxSize(theme.metrics().guide.width, theme.metrics().guide.height)));
    SetSizer(new wxBoxSizer(wxVERTICAL));

    m_view = new AgentWebView(this, theme, workspace, Agent::load_agent_runtime(wxGetApp().app_config).availability, {}, {},
                              AgentPageMode::Guide);
    m_view->SetName(GetTitle());
    GetSizer()->Add(m_view, 1, wxEXPAND);
    m_view->host().set_page_message_handler([this](const std::string& type, const json& payload) { return on_page_message(type, payload); });

    // Escape belongs to the page, where it empties the search box; a dialog
    // would otherwise close on it.
    SetEscapeId(wxID_NONE);
    // A window that is not modal is only hidden on close; this one goes, so
    // the next Help › JusPrin guide… opens a fresh one.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { close(); });

    apply_appearance(GUI_App::dark_mode());
    CentreOnParent();
}

// The page's handler calls into this window, so the view goes while the
// window is still whole rather than with wx's children after it.
GuideWindow::~GuideWindow() { delete m_view; }

void GuideWindow::bring_forward()
{
    m_release.cancel();
    Show();
    Raise();
}

void GuideWindow::close()
{
    if (!m_release.request())
        Hide();
}

// The view goes first and synchronously (see WebViewRelease); the window,
// with no web view left in it, then goes the usual way.
void GuideWindow::release()
{
    delete m_view;
    m_view = nullptr;
    Destroy();
}

void GuideWindow::apply_appearance(bool dark)
{
    SetBackgroundColour(m_theme.palette(dark).surface_raised);
    if (m_view != nullptr)
        m_view->apply_appearance(dark);
    wxGetApp().UpdateDlgDarkUI(this);
    Refresh();
}

bool GuideWindow::on_page_message(const std::string& type, const json& payload)
{
    if (type != Agent::Protocol::kShellAction || !payload.is_object() || payload.value("action", std::string()) != "open_link")
        return false;
    // The page offers only https links out of the guide; anything else is
    // not one of its links and is not opened.
    const std::string url = payload.value("url", std::string());
    if (url.rfind("https://", 0) != 0)
        return false;
    CallAfter([this, url] {
        if (!wxLaunchDefaultBrowser(wxString::FromUTF8(url)))
            MessageDialog(this, _L("The app couldn't open your browser."), _L("JusPrin guide"), wxOK | wxICON_WARNING).ShowModal();
    });
    return true;
}

} // namespace Slic3r::GUI::JusPrin
