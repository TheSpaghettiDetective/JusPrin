#pragma once

// A print host's own web page -- Mainsail, Fluidd, OctoPrint, PrusaLink -- in
// a window of its own, opened from the printer's card on Home.
//
// The content is Orca's Device tab view, PrinterWebView, unchanged. Orca
// loads that tab from the *selected* printer, so showing it for another
// printer would switch the open project's printer. Here it is loaded from the
// printer's own saved address instead, and nothing about the project or the
// page the person is on changes.
//
// PrinterWebView only logs a page that fails to load, and WebKit gives up on
// an address that never answers after more than a minute, so the window
// watches the load itself and says in words when the page is not there.

#include "PopOutWindow.hpp"

#include <wx/timer.h>

#include <string>

class Button;
class wxPanel;
class wxStaticText;
class wxWebViewEvent;

namespace Slic3r {
class DynamicPrintConfig;
namespace GUI {
class PrinterWebView;
}
} // namespace Slic3r

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class PrinterWindow final : public PopOutWindow
{
public:
    // `config` is the printer's own profile, whose print host is loaded.
    PrinterWindow(const std::string& printer_name, const DynamicPrintConfig& config);

    // Brings the window forward for the printer as it is saved now: a page
    // at another address than the one shown is loaded.
    void show_printer(const std::string& printer_name, const DynamicPrintConfig& config);

    PrinterWebView* view() const { return m_view; }
    // Whether the window says the page did not load, in place of the page.
    bool problem_shown() const;

    // The page `config` names for its print host, as Orca's Device tab
    // computes it; empty when the profile has no print host.
    static std::string page_url(const DynamicPrintConfig& config);

private:
    void load();
    void on_loaded(wxWebViewEvent& event);
    void on_error(wxWebViewEvent& event);
    void show_problem();
    void hide_problem();
    void apply_appearance();

    wxPanel*        m_body{nullptr};
    PrinterWebView* m_view{nullptr};
    wxPanel*        m_problem{nullptr};
    wxStaticText*   m_problem_title{nullptr};
    wxStaticText*   m_problem_detail{nullptr};
    Button*         m_retry{nullptr};
    wxTimer         m_load_deadline;
    std::string     m_url;
    std::string     m_address;
    wxString        m_api_key;
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
