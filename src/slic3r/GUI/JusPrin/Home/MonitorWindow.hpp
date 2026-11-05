#pragma once

// A Bambu Lab printer's monitor -- camera, progress, temperatures, controls,
// storage, firmware and the assistant -- in a window of its own, opened from
// the printer's card on Home or from the header's printer menu.
//
// The content is Orca's own Device page, MonitorPanel, as a second instance.
// The one inside the main window is a Notebook page whose content follows
// the *selected* printer profile (a print host's page when that profile is
// not a Bambu Lab printer), so jumping to it from a card showed another
// printer's page. This window always holds the monitor.
//
// The window watches one printer and is independent of the main window: it
// stays until the person closes it or quits the app. But MonitorPanel shows
// whatever machine Orca has selected, and Orca has one selected machine and
// one printer agent at a time, both chosen by the main window. When Orca
// moves to another machine -- the project's printer changes, or a print is
// sent to another printer -- the window stops updating, disables the
// controls (they would act on the other machine) and says so in a row at
// the top, with a Close button. Launching the monitor again opens a new
// window. Each printer gets a window of its own, and opening a second one
// makes Orca move, so the first goes stale the same way. Choosing another
// printer in the window's own device list is the person moving the window
// to that printer, not the window going stale.

#include "PopOutWindow.hpp"

#include <wx/timer.h>

#include <string>

namespace Slic3r { namespace GUI {
class MonitorPanel;
namespace JusPrin {
class NoticeRow;
}
}} // namespace Slic3r::GUI

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class MonitorWindow final : public PopOutWindow
{
public:
    // Shows the device `device_id` and brings the window forward.
    explicit MonitorWindow(const std::string& device_id);

    // Orca moved to another machine, so the window no longer updates.
    bool outdated() const { return m_outdated; }

    const std::string& device_id() const { return m_device; }
    MonitorPanel* panel() const { return m_panel; }

private:
    // The panel's selection is queued; this follows it to `device_id`.
    void watch(const std::string& device_id);
    void check_machine();
    void mark_outdated();

    MonitorPanel* m_panel{nullptr};
    NoticeRow*    m_notice{nullptr};
    wxTimer       m_check_timer;
    std::string   m_device;
    // Whether Orca has selected m_device since the window was asked to show
    // it, and how many checks have passed without it.
    bool          m_selected{false};
    int           m_checks_waited{0};
    bool          m_outdated{false};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
