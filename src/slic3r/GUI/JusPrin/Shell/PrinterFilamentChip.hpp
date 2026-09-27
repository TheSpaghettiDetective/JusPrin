#pragma once

// The Prepare header's machine chip: printer on the left, filament on the
// right, one outline around both.
//
// The halves are two HeaderButtons rather than one control with two hit
// regions, so each gets its own hover, pressed, focus and open state for free,
// and each can anchor its own menu. Left and right arrow keys move between
// them, so the chip behaves like one control to the keyboard while remaining
// two to the pointer.
//
// The chip renders state and raises intent. It reads nothing and changes
// nothing in Orca; StatusRow owns both. It states facts and never warns.

#include "FilamentChipModel.hpp"
#include "HeaderControls.hpp"

#include <wx/panel.h>
#include <wx/string.h>

#include <functional>

namespace Slic3r::GUI::JusPrin {

class PrinterFilamentChip : public wxPanel
{
public:
    PrinterFilamentChip(wxWindow* parent, const ShellTheme& theme);

    // Left half. `nozzle` is already formatted for display; `tooltip` is the
    // full text the half may ellipsize.
    void set_printer(const wxString& nickname, const wxString& nozzle, const wxString& tooltip);
    // Right half: the slot dots and label the model describes. `label` is the
    // text the model resolved to -- a short name or a count.
    void set_filaments(const FilamentChipModel& model, const wxString& label, const wxString& tooltip);

    void set_dark(bool dark);

    // Raised when a half is activated, by pointer or keyboard.
    void on_printer_activated(std::function<void()> handler) { m_printer_activated = std::move(handler); }
    void on_filament_activated(std::function<void()> handler) { m_filament_activated = std::move(handler); }

    HeaderButton& printer_half() { return *m_printer; }
    HeaderButton& filament_half() { return *m_filament; }

    wxSize DoGetBestSize() const override;
    // The whole chip as one image: both halves drawn side by side exactly as
    // they sit in the header.
    wxBitmap snapshot();

private:
    void layout();

    HeaderButton* m_printer{nullptr};
    HeaderButton* m_filament{nullptr};
    std::function<void()> m_printer_activated;
    std::function<void()> m_filament_activated;
};

} // namespace Slic3r::GUI::JusPrin
