#pragma once

// The Prepare header's machine chip: printer on the left, spool on the right,
// one outline around both.
//
// The halves are two HeaderButtons rather than one control with two hit
// regions, so each gets its own hover, pressed, focus and open state for free,
// and each can anchor its own menu. Left and right arrow keys move between
// them, so the chip behaves like one control to the keyboard while remaining
// two to the pointer.
//
// The chip renders state and raises intent. It reads nothing from the spool
// store and changes nothing in Orca; StatusRow owns both.

#include "HeaderControls.hpp"

#include <wx/panel.h>
#include <wx/string.h>

#include <functional>

namespace Slic3r::GUI::JusPrin {

class PrinterSpoolChip : public wxPanel
{
public:
    PrinterSpoolChip(wxWindow* parent, const ShellTheme& theme);

    // Left half. `nozzle` is already formatted for display.
    void set_printer(const wxString& nickname, const wxString& nozzle);
    // Right half. An invalid colour draws the dashed "no spool" ring.
    void set_spool(const wxString& name, const wxColour& colour);

    void set_dark(bool dark);

    // Raised when a half is activated, by pointer or keyboard.
    void on_printer_activated(std::function<void()> handler) { m_printer_activated = std::move(handler); }
    void on_spool_activated(std::function<void()> handler) { m_spool_activated = std::move(handler); }

    HeaderButton& printer_half() { return *m_printer; }
    HeaderButton& spool_half() { return *m_spool; }

    wxSize DoGetBestSize() const override;
    // The whole chip as one image: both halves drawn side by side exactly as
    // they sit in the header.
    wxBitmap snapshot();

private:
    void layout();

    HeaderButton* m_printer{nullptr};
    HeaderButton* m_spool{nullptr};
    std::function<void()> m_printer_activated;
    std::function<void()> m_spool_activated;
};

} // namespace Slic3r::GUI::JusPrin
