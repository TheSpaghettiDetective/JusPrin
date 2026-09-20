#pragma once

// wx transport for the Home bridge: hosts the packaged local React page in a
// wxWebView and forwards JSON envelopes between the page and the HomeHost.
// When the page cannot be loaded at all, the panel says so in place of the
// gallery rather than showing an empty screen that looks like "no projects".

#include "HomeHost.hpp"
#include "OrcaHomeBackend.hpp"

#include <wx/panel.h>

#include <memory>

class wxWebView;
class wxWebViewEvent;
class wxStaticText;
class wxBoxSizer;

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
    // changes while another screen is in front. `added` leads the column
    // with a successful Add and sends its receipt, for this one refresh.
    void refresh(const AddedPrinterEntry* added = nullptr);

    HomeHost&        host() { return *m_host; }
    OrcaHomeBackend& backend() { return *m_backend; }
    wxWebView* webview() const { return m_webview; }

    // Puts a panel of the shell's beside the page, at a fixed width, where
    // the page's own printers column is. The page hides that column while the
    // panel is shown, so the two never both claim it. The shell owns the
    // panel's lifetime; passing nullptr takes it back out.
    void attach_side_panel(wxWindow* panel, int width_dip);
    void show_side_panel(bool shown);

private:
    void on_script_message(wxWebViewEvent& event);
    void on_load_error(wxWebViewEvent& event);
    void show_page_error(const wxString& reason);

    const ShellTheme&                 m_theme;
    // The backend outlives the host that reads through it.
    std::unique_ptr<OrcaHomeBackend>  m_backend;
    std::unique_ptr<HomeHost>         m_host;
    wxWebView*                m_webview{nullptr};
    wxStaticText*             m_error{nullptr};
    // The page and the shell's panel side by side; the page takes the whole
    // row while no panel is attached.
    wxBoxSizer*               m_row{nullptr};
    wxWindow*                 m_side_panel{nullptr};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
