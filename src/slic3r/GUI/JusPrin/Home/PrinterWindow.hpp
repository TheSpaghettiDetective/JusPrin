#pragma once

// A print host's own web page -- Mainsail, Fluidd, OctoPrint, PrusaLink -- in
// a window of its own, opened from the printer's card on Home.
//
// The content is Orca's Device tab view, PrinterWebView, unchanged. Orca
// loads that tab from the *selected* printer, so showing it for another
// printer would switch the open project's printer. Here it is loaded from the
// printer's own saved address instead, and nothing about the project or the
// page the person is on changes.

#include <wx/frame.h>

#include <string>

namespace Slic3r {
class DynamicPrintConfig;
namespace GUI {
class PrinterWebView;
}
} // namespace Slic3r

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class PrinterWindow final : public wxFrame
{
public:
    // `config` is the printer's own profile, whose print host is loaded.
    PrinterWindow(const std::string& printer_name, const DynamicPrintConfig& config);

    PrinterWebView* view() const { return m_view; }

    // The page `config` names for its print host, as Orca's Device tab
    // computes it; empty when the profile has no print host.
    static std::string page_url(const DynamicPrintConfig& config);

private:
    PrinterWebView* m_view{nullptr};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
