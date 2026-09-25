#include "PrinterWindow.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/sizer.h>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

// PrusaLink and PrusaConnect pages sign in with the saved API key, as
// MainFrame::load_printer_url passes it to the Device tab.
wxString page_api_key(const DynamicPrintConfig& config)
{
    const auto* type = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (type == nullptr || !config.has("printhost_apikey") || (type->value != htPrusaLink && type->value != htPrusaConnect))
        return {};
    return wxString::FromUTF8(config.opt_string("printhost_apikey"));
}

} // namespace

std::string PrinterWindow::page_url(const DynamicPrintConfig& config)
{
    // get_print_host_webui takes a mutable config; it only reads it.
    DynamicPrintConfig copy = config;
    return PrintHost::get_print_host_webui(&copy);
}

// No parent: the window is the person's to move, minimise and put beside or
// behind the app, with its own taskbar entry. Home's backend closes it when
// the app's main window goes.
PrinterWindow::PrinterWindow(const std::string& printer_name, const DynamicPrintConfig& config)
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(printer_name))
{
    if (MainFrame* frame = wxGetApp().mainframe)
        SetIcons(frame->GetIcons());
    m_view       = new PrinterWebView(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_view, wxSizerFlags(1).Expand());
    SetSizer(sizer);
    SetSize(FromDIP(wxSize(1200, 800)));
    CentreOnScreen();
    // The title bar follows the app's appearance, as Orca's own frames do.
    wxGetApp().UpdateFrameDarkUI(this);
    Show();
    // PrinterWebView defers a load until it is shown, so the load follows Show.
    wxString url = wxString::FromUTF8(page_url(config));
    m_view->load_url(url, page_api_key(config));
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
