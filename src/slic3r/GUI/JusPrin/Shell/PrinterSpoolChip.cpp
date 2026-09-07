#include "PrinterSpoolChip.hpp"

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

PrinterSpoolChip::PrinterSpoolChip(wxWindow* parent, const ShellTheme& theme)
    : wxPanel(parent, wxID_ANY)
{
    SetName("Printer and spool");
    // The halves paint edge to edge; the panel itself is never visible.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, [](wxPaintEvent&) {});

    m_printer = new HeaderButton(this, theme, HeaderStyle::ChipLeft, wxEmptyString, HeaderIcon::Printer);
    m_printer->SetName("Printer");
    m_printer->set_label_cap(kHalfLabelCapDip);
    m_spool = new HeaderButton(this, theme, HeaderStyle::ChipRight, wxEmptyString, HeaderIcon::None);
    m_spool->SetName("Spool");
    m_spool->set_label_cap(kHalfLabelCapDip);

    m_printer->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (m_printer_activated) m_printer_activated(); });
    m_spool->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (m_spool_activated) m_spool_activated(); });

    // Left and right move between the halves; the halves themselves keep
    // Return and Space, which HeaderButton already handles.
    auto arrows = [this](HeaderButton* self, HeaderButton* other) {
        self->Bind(wxEVT_KEY_DOWN, [self, other](wxKeyEvent& event) {
            const int key = event.GetKeyCode();
            if (key == WXK_LEFT || key == WXK_RIGHT) {
                if (self != other) other->SetFocus();
                return;
            }
            event.Skip();
        });
    };
    arrows(m_printer, m_spool);
    arrows(m_spool, m_printer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { layout(); event.Skip(); });
}

void PrinterSpoolChip::set_printer(const wxString& nickname, const wxString& nozzle)
{
    const wxString name = nickname.empty() ? _L("Select printer") : nickname;
    m_printer->SetLabel(name);
    HeaderRowDecoration decoration;
    // "X1 Carbon · 0.4" reads as one line, with the nozzle in the teletype
    // face the design system reserves for a measurement.
    decoration.detail        = nozzle;
    decoration.technical     = true;
    decoration.detail_inline = true;
    decoration.trailing  = HeaderIcon::Caret;
    m_printer->set_decoration(std::move(decoration));
    // The tooltip carries the full text the chip may have ellipsized. It says
    // what is selected and nothing about whether it is loaded: the chip makes
    // no claim about the physical machine.
    m_printer->SetToolTip(nozzle.empty() ? name : name + wxString::FromUTF8(" \xC2\xB7 ") + nozzle + " mm");
    InvalidateBestSize();
    layout();
}

void PrinterSpoolChip::set_spool(const wxString& name, const wxColour& colour)
{
    const wxString label = name.empty() ? _L("Select spool") : name;
    m_spool->SetLabel(label);
    HeaderRowDecoration decoration;
    decoration.dot      = colour; // an invalid colour draws the dashed ring
    decoration.trailing = HeaderIcon::Caret;
    m_spool->set_decoration(std::move(decoration));
    m_spool->SetToolTip(label);
    InvalidateBestSize();
    layout();
}

void PrinterSpoolChip::set_dark(bool dark)
{
    m_printer->set_dark(dark);
    m_spool->set_dark(dark);
    Refresh();
}

wxSize PrinterSpoolChip::DoGetBestSize() const
{
    const wxSize left = m_printer->GetBestSize(), right = m_spool->GetBestSize();
    return {left.x + right.x, std::max(left.y, right.y)};
}

wxBitmap PrinterSpoolChip::snapshot()
{
    const wxBitmap left = m_printer->snapshot(), right = m_spool->snapshot();
    wxBitmap combined(left.GetWidth() + right.GetWidth(), std::max(left.GetHeight(), right.GetHeight()));
    wxMemoryDC dc(combined);
    dc.DrawBitmap(left, 0, 0);
    dc.DrawBitmap(right, left.GetWidth(), 0);
    dc.SelectObject(wxNullBitmap);
    return combined;
}

void PrinterSpoolChip::layout()
{
    const wxSize size = GetClientSize();
    if (size.x <= 0) return;
    // Under pressure both halves give up width in proportion to what they
    // asked for, so neither collapses to its ellipsis while the other keeps
    // slack.
    const int wanted_left  = m_printer->GetBestSize().x;
    const int wanted_right = m_spool->GetBestSize().x;
    const int wanted       = wanted_left + wanted_right;
    const int left = wanted <= size.x || wanted == 0 ? wanted_left : size.x * wanted_left / wanted;
    m_printer->SetSize(0, 0, left, size.y);
    m_spool->SetSize(left, 0, size.x - left, size.y);
}

} // namespace Slic3r::GUI::JusPrin
