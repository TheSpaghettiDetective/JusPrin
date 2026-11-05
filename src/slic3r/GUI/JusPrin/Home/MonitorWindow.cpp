#include "MonitorWindow.hpp"

#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Shell/NoticeRow.hpp"
#include "slic3r/GUI/Monitor.hpp"

#include <wx/display.h>
#include <wx/sizer.h>

#include <algorithm>
#include <stdexcept>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

constexpr int kCheckMs = 1000;
// How many checks a new selection gets to take effect: a LAN printer
// connects first, and Orca selects it once it has.
constexpr int kSelectionChecks = 5;

MachineObject* selected_machine()
{
    DeviceManager* devices = wxGetApp().getDeviceManager();
    return devices != nullptr ? devices->get_selected_machine() : nullptr;
}

// The name the person gave the printer, or else the machine's own.
wxString printer_title(const MachineObject& machine)
{
    for (const Printers::NamedPrinter& named : Printers::named_printers())
        if (named.device_id == machine.get_dev_id())
            return wxString::FromUTF8(named.name);
    return wxString::FromUTF8(machine.get_dev_name());
}

} // namespace

MonitorWindow::MonitorWindow(const std::string& device_id)
    : PopOutWindow(wxString()), m_check_timer(this)
{
    // The shell, which opens every monitor window, does not install without
    // the design tokens.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr)
        throw std::logic_error("a monitor window was opened without the JusPrin design tokens");

    auto* body = new wxPanel(this);
    m_notice = new NoticeRow(body, *theme, _L("This window stopped updating because the app switched to another printer."),
                             _L("Close"), [this] { CallAfter([this] { close(); }); }, true);
    m_notice->Hide();
    m_panel = new MonitorPanel(body, wxID_ANY);
    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(m_notice, 0, wxEXPAND);
    column->Add(m_panel, 1, wxEXPAND);
    body->SetSizer(column);
    set_content(body);
    // The monitor's columns need more width than a web page does; without it
    // the controls sit behind a horizontal scroll bar. Fit them, within the
    // screen.
    const wxRect screen = wxDisplay(this).GetClientArea();
    SetSize(wxSize(std::min(std::max(GetSize().x, GetBestSize().x), screen.width),
                   std::min(std::max(GetSize().y, GetBestSize().y), screen.height)));
    CentreOnScreen();
    Show();
    // A Notebook shows its page through the page's own Show(), which is
    // where MonitorPanel starts reading the machine; a frame does not, so
    // the window does it.
    m_panel->Show(true);

    // Every selection the panel makes, from below or from its own device
    // list, is the printer this window watches from then on. Bound
    // after the panel's own handler, so this one runs first.
    m_panel->Bind(wxEVT_COMMAND_CHOICE_SELECTED, [this](wxCommandEvent& event) {
        watch(event.GetString().ToStdString());
        event.Skip();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { check_machine(); }, m_check_timer.GetId());
    m_check_timer.Start(kCheckMs);

    // Upstream reconnects a LAN printer that is selected again, dropping its
    // connection for nothing, so a printer already selected is left as it is.
    // Otherwise the selection is queued, and the window watches the printer
    // from now so a second click finds this window.
    m_device = device_id;
    for (const Printers::NamedPrinter& named : Printers::named_printers())
        if (named.device_id == device_id)
            SetTitle(wxString::FromUTF8(named.name));
    if (const MachineObject* shown = selected_machine(); shown != nullptr && shown->get_dev_id() == device_id)
        watch(device_id);
    else
        m_panel->select_machine(device_id);
    bring_forward();
}

void MonitorWindow::watch(const std::string& device_id)
{
    m_device        = device_id;
    m_selected      = false;
    m_checks_waited = 0;
    check_machine();
}

void MonitorWindow::check_machine()
{
    // Closing destroys the panel before the frame goes.
    if (m_outdated || closing())
        return;
    if (const MachineObject* shown = selected_machine(); shown != nullptr && shown->get_dev_id() == m_device) {
        if (!m_selected) {
            m_selected = true;
            SetTitle(printer_title(*shown));
        }
        return;
    }
    if (!m_selected && ++m_checks_waited < kSelectionChecks)
        return;
    mark_outdated();
}

void MonitorWindow::mark_outdated()
{
    m_outdated = true;
    m_check_timer.Stop();
    // MonitorPanel::Show(false) is what stops its refresh; showing the
    // wxPanel again keeps its last picture on screen. The panel may already
    // have refreshed once for the new machine before this check ran.
    m_panel->Show(false);
    m_panel->wxPanel::Show(true);
    // Its controls act on the machine Orca has selected, which is no longer
    // this window's printer.
    m_panel->Disable();
    m_notice->Show();
    m_notice->GetParent()->Layout();
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
