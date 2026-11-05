#include "NoticeRow.hpp"

#include "ShellRecipes.hpp"
#include "ShellTheme.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"

#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r::GUI::JusPrin {

NoticeRow::NoticeRow(wxWindow* parent, const ShellTheme& theme, const wxString& text, const wxString& action,
                     std::function<void()> on_action, bool warning)
    : wxPanel(parent, wxID_ANY), m_theme(theme), m_warning(warning)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_text   = new wxStaticText(this, wxID_ANY, text);
    m_button = new Button(this, action);
    m_button->SetName(action);

    // The button's own height plus 8 above and below sets the row's. The
    // status dot is painted into the space left for it before the sentence.
    const int dot = FromDIP(theme.metrics().status_dot.size);
    auto*     row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(FromDIP(16));
    if (m_warning)
        row->AddSpacer(dot + FromDIP(8));
    row->Add(m_text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    row->Add(m_button, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(8));
    row->AddSpacer(FromDIP(16));
    SetSizer(row);

    // Disabled at once: a second press queued before the first runs would
    // ask again for what the first has already done.
    m_button->Bind(wxEVT_BUTTON, [this, on_action = std::move(on_action)](wxCommandEvent&) {
        m_button->Disable();
        on_action();
    });
    Bind(wxEVT_PAINT, [this, dot](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const ShellPalette&   p = m_theme.palette(m_dark);
        const wxSize          size = GetClientSize();
        dc.SetBackground(wxBrush(p.surface_canvas));
        dc.Clear();
        dc.SetPen(wxPen(p.border_subtle));
        dc.DrawLine(0, size.y - 1, size.x, size.y - 1);
        if (m_warning) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(p.status_warning));
            dc.DrawEllipse(FromDIP(16), (size.y - dot) / 2, dot, dot);
        }
    });
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        apply_appearance(GUI_App::dark_mode());
        event.Skip();
    });
    apply_appearance(GUI_App::dark_mode());
}

void NoticeRow::apply_appearance(bool dark)
{
    m_dark = dark;
    const ShellPalette& palette = m_theme.palette(dark);
    // Before the button: its resting fill is whatever it sits on.
    SetBackgroundColour(palette.surface_canvas);
    style_label(*m_text, m_theme, TextRole::Body, m_warning ? palette.text_primary : palette.text_secondary);
    style_button(*m_button, m_theme, palette, m_theme.metrics().button.secondary);
    Layout();
    Refresh();
}

} // namespace Slic3r::GUI::JusPrin
