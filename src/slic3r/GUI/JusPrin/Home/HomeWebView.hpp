#pragma once

// wx transport for the Home bridge: hosts the packaged local React page in a
// wxWebView and forwards JSON envelopes between the page and the HomeHost.
// When the page cannot be loaded at all, the panel says so in place of the
// gallery rather than showing an empty screen that looks like "no projects".

#include "HomeHost.hpp"

#include <wx/panel.h>

#include <memory>

class wxWebView;
class wxWebViewEvent;
class wxStaticText;

namespace Slic3r { namespace GUI {
class MainFrame;
}} // namespace Slic3r::GUI

namespace Slic3r { namespace GUI { namespace JusPrin {

class ShellTheme;

namespace Workspace {
class SpoolStore;
}

namespace Home {

class HomeWebView : public wxPanel
{
public:
    HomeWebView(wxWindow* parent, const ShellTheme& theme, MainFrame& frame, Workspace::SpoolStore* spools);
    ~HomeWebView() override;

    void apply_appearance(bool dark);
    // Re-reads the recent-project list and the device state. The shell calls
    // this when Home becomes visible: the gallery is a view of state that
    // changes while another screen is in front.
    void refresh();

    HomeHost&  host() { return *m_host; }
    wxWebView* webview() const { return m_webview; }

private:
    void on_script_message(wxWebViewEvent& event);
    void on_load_error(wxWebViewEvent& event);
    void show_page_error(const wxString& reason);

    const ShellTheme&         m_theme;
    std::unique_ptr<HomeHost> m_host;
    wxWebView*                m_webview{nullptr};
    wxStaticText*             m_error{nullptr};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
