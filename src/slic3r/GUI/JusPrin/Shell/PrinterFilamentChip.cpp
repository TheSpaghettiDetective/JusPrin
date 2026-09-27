#include "PrinterFilamentChip.hpp"

#include "slic3r/GUI/I18N.hpp"

#include <wx/dcmemory.h>
#include <wx/sizer.h>
#include <algorithm>

namespace Slic3r::GUI::JusPrin {

namespace {
// Neither half may grow past this, so one long preset name cannot push the
// print action off the row. The full text stays available in the tooltip.
constexpr int kHalfLabelCapDip = 240;
} // namespace

PrinterFilamentChip::PrinterFilamentChip(wxWindow* parent, const ShellTheme& theme)
    : wxPanel(parent, wxID_ANY)
{
    SetName("Printer and filament");
    // The halves paint edge to edge; the panel itself is never visible.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, [](wxPaintEvent&) {});

    m_printer = new HeaderButton(this, theme, HeaderStyle::ChipLeft, wxEmptyString, HeaderIcon::Printer);
    m_printer->SetName("Printer");
    m_printer->set_label_cap(kHalfLabelCapDip);
    m_filament = new HeaderButton(this, theme, HeaderStyle::ChipRight, wxEmptyString, HeaderIcon::None);
    m_filament->SetName("Filament");
    m_filament->set_label_cap(kHalfLabelCapDip);

    m_printer->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (m_printer_activated) m_printer_activated(); });
    m_filament->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (m_filament_activated) m_filament_activated(); });

    // Left and right move between the halves; the halves themselves keep
    // Return and Space, which HeaderButton already handles.
    auto arrows = [](HeaderButton* self, HeaderButton* other) {
        self->Bind(wxEVT_KEY_DOWN, [self, other](wxKeyEvent& event) {
            const int key = event.GetKeyCode();
            if (key == WXK_LEFT || key == WXK_RIGHT) {
                if (self != other) other->SetFocus();
                return;
            }
            event.Skip();
        });
    };
    arrows(m_printer, m_filament);
    arrows(m_filament, m_printer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { layout(); event.Skip(); });
}

void PrinterFilamentChip::set_printer(const wxString& nickname, const wxString& nozzle, const wxString& tooltip)
{
    const wxString name = nickname.empty() ? _L("Select printer") : nickname;
    m_printer->SetLabel(name);
    HeaderRowDecoration decoration;
    // "X1 Carbon · 0.4" reads as one line.
    decoration.detail        = nozzle;
    decoration.detail_inline = true;
    decoration.trailing      = HeaderIcon::Caret;
    m_printer->set_decoration(std::move(decoration));
    // The tooltip carries the full text the chip may have ellipsized. It says
    // what is selected and nothing about whether it is loaded: the chip makes
    // no claim about the physical machine.
    m_printer->SetToolTip(tooltip.empty() ? name : tooltip);
    InvalidateBestSize();
    layout();
}

void PrinterFilamentChip::set_filaments(const FilamentChipModel& model, const wxString& label, const wxString& tooltip)
{
    m_filament->SetLabel(label.empty() ? _L("Select filament") : label);
    HeaderRowDecoration decoration;
    for (const ChipDot& dot : model.dots)
        decoration.slot_dots.push_back({wxColour(wxString::FromUTF8(dot.colour)), dot.faded});
    if (model.folded > 0)
        decoration.slot_more = wxString::Format("+%d", int(model.folded));
    decoration.small_slot_dots = model.shrunk;
    decoration.trailing        = HeaderIcon::Caret;
    m_filament->set_decoration(std::move(decoration));
    m_filament->SetToolTip(tooltip.empty() ? label : tooltip);
    InvalidateBestSize();
    layout();
}

void PrinterFilamentChip::set_dark(bool dark)
{
    m_printer->set_dark(dark);
    m_filament->set_dark(dark);
    Refresh();
}

wxSize PrinterFilamentChip::DoGetBestSize() const
{
    const wxSize left = m_printer->GetBestSize(), right = m_filament->GetBestSize();
    return {left.x + right.x, std::max(left.y, right.y)};
}

wxBitmap PrinterFilamentChip::snapshot()
{
    const wxBitmap left = m_printer->snapshot(), right = m_filament->snapshot();
    wxBitmap combined(left.GetWidth() + right.GetWidth(), std::max(left.GetHeight(), right.GetHeight()));
    wxMemoryDC dc(combined);
    dc.DrawBitmap(left, 0, 0);
    dc.DrawBitmap(right, left.GetWidth(), 0);
    dc.SelectObject(wxNullBitmap);
    return combined;
}

void PrinterFilamentChip::layout()
{
    const wxSize size = GetClientSize();
    if (size.x <= 0) return;
    // Under pressure both halves give up width in proportion to what they
    // asked for, so neither collapses to its ellipsis while the other keeps
    // slack.
    const int wanted_left  = m_printer->GetBestSize().x;
    const int wanted_right = m_filament->GetBestSize().x;
    const int wanted       = wanted_left + wanted_right;
    const int left = wanted <= size.x || wanted == 0 ? wanted_left : size.x * wanted_left / wanted;
    m_printer->SetSize(0, 0, left, size.y);
    m_filament->SetSize(left, 0, size.x - left, size.y);
}

} // namespace Slic3r::GUI::JusPrin
