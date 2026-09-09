#include "HeaderControls.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/weakref.h>
#include <algorithm>
#include <memory>

namespace Slic3r::GUI::JusPrin {
namespace {

// Small semantic strokes share the header's foreground and DPI transform.
void draw_icon(wxGraphicsContext& gc, HeaderIcon icon, double x, double y, double size, const wxColour& color)
{
    gc.PushState();
    gc.Translate(x, y);
    gc.Scale(size / 16., size / 16.);
    gc.SetPen(wxPen(color, 1));
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    auto line = [&](std::initializer_list<wxPoint2DDouble> points) {
        auto path = gc.CreatePath();
        bool first = true;
        for (const auto& p : points) {
            if (first) path.MoveToPoint(p.m_x, p.m_y);
            else path.AddLineToPoint(p.m_x, p.m_y);
            first = false;
        }
        gc.StrokePath(path);
    };
    switch (icon) {
    case HeaderIcon::Back: line({{10,3},{5,8},{10,13}}); break;
    case HeaderIcon::Right: line({{6,3},{11,8},{6,13}}); break;
    case HeaderIcon::Down: line({{4,6},{8,10},{12,6}}); break;
    case HeaderIcon::Up: line({{4,10},{8,6},{12,10}}); break;
    case HeaderIcon::Check: line({{3,8.5},{6.5,12},{13,4}}); break;
    case HeaderIcon::Caret: {
        // A filled disclosure triangle, as the chip halves use in the design.
        auto p = gc.CreatePath();
        p.MoveToPoint(4.5,6.5); p.AddLineToPoint(11.5,6.5); p.AddLineToPoint(8,10.5); p.CloseSubpath();
        gc.SetBrush(wxBrush(color)); gc.SetPen(*wxTRANSPARENT_PEN);
        gc.FillPath(p);
        break;
    }
    case HeaderIcon::Printer:
        // A printer: paper feeding out of a body, not the abstract mark the
        // previous single chip inherited.
        line({{4.5,6},{4.5,2.5},{11.5,2.5},{11.5,6}});
        line({{2.5,6},{13.5,6},{13.5,11},{2.5,11},{2.5,6}});
        line({{4.5,9},{4.5,13.5},{11.5,13.5},{11.5,9}});
        break;
    case HeaderIcon::Monitor:
        line({{2,3},{14,3},{14,11},{2,11},{2,3}});
        line({{6,14},{10,14}}); line({{8,11},{8,14}});
        break;
    case HeaderIcon::More:
        gc.SetBrush(wxBrush(color));
        for (int i = 0; i < 3; ++i) gc.DrawEllipse(3 + i * 4, 7, 1.5, 1.5);
        break;
    case HeaderIcon::Cancel: line({{4,4},{12,12}}); line({{12,4},{4,12}}); break;
    case HeaderIcon::Slice:
        gc.DrawEllipse(1.5,1.5,13,13);
        line({{6,4.5},{11,8},{6,11.5},{6,4.5}});
        break;
    case HeaderIcon::Eye: {
        auto p = gc.CreatePath();
        p.MoveToPoint(1,8); p.AddCurveToPoint(5,2,11,2,15,8);
        p.AddCurveToPoint(11,14,5,14,1,8); gc.StrokePath(p);
        gc.DrawEllipse(6,6,4,4); break;
    }
    case HeaderIcon::Plates:
        line({{2,5},{8,2},{14,5},{8,8},{2,5}});
        line({{2,8},{8,11},{14,8}}); line({{2,11},{8,14},{14,11}}); break;
    case HeaderIcon::Export:
        line({{8,2},{8,9.5}}); line({{4.5,6},{8,9.5},{11.5,6}});
        line({{3,11},{3,13.5},{13,13.5},{13,11}}); break;
    case HeaderIcon::Print:
        line({{2,7},{14,2},{9,14},{7,9},{2,7}}); line({{7,9},{14,2}}); break;
    case HeaderIcon::PanelOpen:
    case HeaderIcon::PanelClosed:
        line({{2.5,3.5},{13.5,3.5},{13.5,12.5},{2.5,12.5},{2.5,3.5}});
        line({{9.5,3.5},{9.5,12.5}});
        if (icon == HeaderIcon::PanelOpen) {
            gc.SetBrush(wxBrush(color));
            gc.SetPen(*wxTRANSPARENT_PEN);
            gc.DrawRectangle(9.5,3.5,4,9);
        }
        break;
    case HeaderIcon::None: break;
    }
    gc.PopState();
}

wxColour status_colour(StatusTone tone, const ShellPalette& p)
{
    switch (tone) {
    case StatusTone::Positive: return p.status_success;
    case StatusTone::Busy:     return p.action_primary;
    case StatusTone::Warning:  return p.status_warning;
    case StatusTone::Neutral:  return p.text_secondary;
    case StatusTone::None:     break;
    }
    return p.text_secondary;
}

// A remembered spool's colour, or the dashed ring that means "no spool chosen
// yet". Both are drawn at the same size so rows stay aligned either way.
void draw_dot(wxGraphicsContext& gc, const std::optional<wxColour>& dot, double x, double y, double size,
              const ShellPalette& p)
{
    if (!dot) return;
    if (dot->IsOk()) {
        // A quiet ring keeps a white or near-background spool visible.
        gc.SetPen(wxPen(p.border_subtle));
        gc.SetBrush(wxBrush(*dot));
    } else {
        wxPen dashed(p.text_secondary, 1, wxPENSTYLE_SHORT_DASH);
        gc.SetPen(dashed);
        gc.SetBrush(*wxTRANSPARENT_BRUSH);
    }
    gc.DrawEllipse(x, y, size, size);
}

} // namespace

HeaderButton::HeaderButton(wxWindow* parent, const ShellTheme& theme, HeaderStyle style,
                           const wxString& label, HeaderIcon icon)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
      m_theme(theme), m_style(style), m_icon(icon)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(role_font());
    SetLabel(label);
    Bind(wxEVT_PAINT, &HeaderButton::paint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hover = true; Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hover = false; Refresh(); });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { m_pressed = false; Refresh(); e.Skip(); });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!IsEnabled()) return;
        SetFocus(); m_pressed = true; CaptureMouse(); Refresh();
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        const bool invoke = m_pressed && GetClientRect().Contains(e.GetPosition());
        const bool on_action = m_row_action != HeaderIcon::None && row_action_rect().Contains(e.GetPosition());
        m_pressed = false;
        if (HasCapture()) ReleaseMouse();
        Refresh();
        if (!invoke) return;
        if (on_action) invoke_row_action();
        else activate(false);
    });
    // Right-clicking anywhere on the row is the pointer equivalent of the
    // row action, for people who do not aim at a glyph that appears on hover.
    Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& e) {
        if (m_row_action == HeaderIcon::None) { e.Skip(); return; }
        SetFocus();
        invoke_row_action();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { m_pressed = false; Refresh(); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE) { m_pressed = true; Refresh(); }
        else if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) activate(true);
        else e.Skip();
    });
    Bind(wxEVT_KEY_UP, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE && m_pressed) { m_pressed = false; Refresh(); activate(true); }
        else e.Skip();
    });
}

void HeaderButton::activate(bool from_keyboard)
{
    if (!IsEnabled()) return;
    m_keyboard_activated = from_keyboard;
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}
void HeaderButton::SetLabel(const wxString& label) { wxControl::SetLabel(label); InvalidateBestSize(); Refresh(); }
bool HeaderButton::Enable(bool enabled) { const bool changed = wxControl::Enable(enabled); Refresh(); return changed; }
void HeaderButton::set_dark(bool dark) { m_dark = dark; Refresh(); }
void HeaderButton::set_icon(HeaderIcon icon) { m_icon = icon; InvalidateBestSize(); Refresh(); }
void HeaderButton::set_status(bool visible, bool warning) { m_status = visible; m_warning = warning; InvalidateBestSize(); Refresh(); }
void HeaderButton::set_detail(const wxString& detail) { m_decoration.detail = detail; InvalidateBestSize(); Refresh(); }
void HeaderButton::set_row_action(HeaderIcon icon) { m_row_action = icon; InvalidateBestSize(); Refresh(); }

wxRect HeaderButton::row_action_rect() const
{
    if (m_row_action == HeaderIcon::None) return {};
    const int size = FromDIP(20);
    return {GetClientSize().x - FromDIP(6) - size, (GetClientSize().y - size) / 2, size, size};
}

void HeaderButton::invoke_row_action()
{
    if (m_row_action == HeaderIcon::None || !IsEnabled()) return;
    wxCommandEvent event(wxEVT_MENU, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

void HeaderButton::set_label_cap(int cap_dip) { m_label_cap = cap_dip; InvalidateBestSize(); Refresh(); }
void HeaderButton::set_decoration(HeaderRowDecoration decoration)
{
    m_decoration = std::move(decoration);
    SetFont(role_font());
    InvalidateBestSize();
    Refresh();
}

// Chip halves carry the bold label role; the print action and an emphasised
// menu row carry bold body; everything else is Body, the control role.
const wxFont& HeaderButton::role_font() const
{
    if (m_decoration.bold || m_style == HeaderStyle::PrimaryLeft) return m_theme.font(TextRole::BodyBold);
    if (m_style == HeaderStyle::ChipLeft || m_style == HeaderStyle::ChipRight) return m_theme.font(TextRole::LabelBold);
    return m_theme.font(TextRole::Body);
}

// Secondary text beside a label: the label role, in the teletype face when
// it is a measurement.
const wxFont& HeaderButton::detail_font() const
{
    return m_decoration.technical ? m_theme.mono_font(TextRole::Label) : m_theme.font(TextRole::Label);
}

// How much of a row's width is reserved on the right for everything that is
// not the label. Shared by the size calculation and the painter so a row can
// never measure one way and paint another.
int HeaderButton::trailing_reserve() const
{
    int reserve = FromDIP(12);
    if (m_row_action != HeaderIcon::None) reserve += FromDIP(20);
    if (m_status) reserve += FromDIP(16);
    if (m_decoration.check) reserve += FromDIP(20);
    if (m_decoration.trailing != HeaderIcon::None) reserve += FromDIP(20);
    if (m_decoration.status != StatusTone::None) {
        reserve += FromDIP(14); // dot and its gap
        if (!m_decoration.status_word.empty()) reserve += GetTextExtent(m_decoration.status_word).x + FromDIP(4);
    }
    if (!m_decoration.detail.empty() && !m_decoration.detail_inline) {
        wxClientDC dc(const_cast<HeaderButton*>(this));
        dc.SetFont(detail_font());
        reserve += dc.GetTextExtent(m_decoration.detail).x + FromDIP(16);
    }
    return reserve;
}

wxSize HeaderButton::DoGetBestSize() const
{
    const ShellMetrics& m = m_theme.metrics();
    // No recipe covers a split action: its arrow half is the functional icon
    // plus a 12 DIP gutter, as tall as the status row it lives in.
    if (m_style == HeaderStyle::PrimaryRight) return FromDIP(wxSize(m.button.icon.icon_size + m.space_3, m.status_row.height));
    if (m_style == HeaderStyle::Outline) return FromDIP(wxSize(m.button.icon.width, m.button.icon.height));
    int label = GetTextExtent(GetLabel()).x;
    if (!m_decoration.sub_label.empty()) {
        wxClientDC dc(const_cast<HeaderButton*>(this));
        dc.SetFont(m_theme.font(TextRole::Metadata));
        label = std::max(label, dc.GetTextExtent(m_decoration.sub_label).x);
    }
    if (m_label_cap > 0) label = std::min(label, FromDIP(m_label_cap));
    if (!m_decoration.detail.empty() && m_decoration.detail_inline) {
        wxClientDC dc(const_cast<HeaderButton*>(this));
        dc.SetFont(detail_font());
        // Measured exactly as painted: " \xC2\xB7" plus a 3 DIP gap, or the chip
        // reserves space it never fills and the chevron drifts right.
        label += dc.GetTextExtent(wxString::FromUTF8(" \xC2\xB7")).x + FromDIP(3) +
                 dc.GetTextExtent(m_decoration.detail).x;
    }
    const int leading = FromDIP(12) + (m_icon == HeaderIcon::None ? 0 : FromDIP(24)) +
                        (m_decoration.dot.has_value() ? FromDIP(16) : 0);
    // A two-line row is one spacing step taller than the menu row recipe;
    // the print action and the quiet buttons fill the status row.
    const int height = m_style == HeaderStyle::Menu ? (m_decoration.sub_label.empty() ? m.menu_row.height : m.menu_row.height + m.space_3) :
                       m_style == HeaderStyle::ChipLeft || m_style == HeaderStyle::ChipRight ? m.chip.height : m.status_row.height;
    return {leading + label + trailing_reserve(), FromDIP(height)};
}

void HeaderButton::paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    // wxGraphicsContext::Create has no generic wxDC overload, so each caller
    // builds the context for its own concrete DC and hands it to draw().
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    draw(dc, *gc, GetClientSize());
}

// Painting is separated from the paint event so the same code can draw into an
// offscreen bitmap. Appearance is then verifiable from inside the running
// application, without a screen capture -- which also makes the light and dark
// pair reproducible rather than a photograph of one machine's screen.
wxBitmap HeaderButton::snapshot()
{
    const wxSize size = GetClientSize().x > 0 ? GetClientSize() : GetBestSize();
    wxBitmap bitmap(size);
    wxMemoryDC dc(bitmap);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (gc) draw(dc, *gc, size);
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

void HeaderButton::draw(wxDC& dc, wxGraphicsContext& context, const wxSize& client)
{
    const auto& p = m_theme.palette(m_dark);
    const bool primary = m_style == HeaderStyle::PrimaryLeft || m_style == HeaderStyle::PrimaryRight;
    const bool chip    = m_style == HeaderStyle::ChipLeft || m_style == HeaderStyle::ChipRight;
    dc.SetBackground(wxBrush(m_style == HeaderStyle::Menu ? p.surface_raised : p.surface_canvas));
    dc.Clear();
    wxGraphicsContext* gc = &context;

    const auto foreground = !IsEnabled() ? p.action_disabled_text :
                            m_decoration.danger ? p.status_danger :
                            primary ? p.action_primary_text :
                            m_style == HeaderStyle::Quiet ? p.text_secondary : p.text_primary;
    const bool highlighted = (m_hover || m_open || m_menu_selected) && IsEnabled();
    wxColour fill = primary ? (!IsEnabled() ? p.action_disabled : m_pressed || m_open ? p.action_primary_pressed :
                              m_hover ? p.action_primary_hover : p.action_primary) :
                   highlighted ? p.surface_selected :
                   // The spool half sits on a quieter ground than the printer
                   // half, so the chip reads as two things inside one outline.
                   m_style == HeaderStyle::ChipRight ? p.surface_subtle :
                   m_style == HeaderStyle::Menu ? p.surface_raised : p.surface_canvas;

    const ShellMetrics& m = m_theme.metrics();
    const double w = client.x, h = client.y;
    const double r = FromDIP(primary ? m.radius_compact :
                             chip ? m.chip.radius :
                             m_style == HeaderStyle::Menu ? m.menu_row.radius :
                             m_style == HeaderStyle::Outline ? m.button.icon.radius : m.radius_standard);
    if (chip) {
        // Each half draws the whole chip's rounded rectangle, extended past
        // the shared inner edge so only its own outer corners round. The top
        // and bottom borders then meet exactly across the seam.
        gc->SetPen(wxPen(p.border_subtle));
        gc->SetBrush(wxBrush(fill));
        gc->DrawRoundedRectangle(m_style == HeaderStyle::ChipLeft ? 0.5 : 0.5 - r, 0.5, w - 1 + r, h - 1, r);
        if (m_style == HeaderStyle::ChipRight) {
            gc->SetPen(wxPen(p.border_subtle));
            gc->StrokeLine(0.5, 0.5, 0.5, h - 0.5);
        }
    } else {
        gc->SetPen(primary || m_style == HeaderStyle::Quiet || m_style == HeaderStyle::Menu ? *wxTRANSPARENT_PEN : wxPen(p.border_subtle));
        gc->SetBrush(wxBrush(fill));
        gc->DrawRoundedRectangle(0.5,0.5,w-1,h-1,r);
    }
    if (primary && (m_style == HeaderStyle::PrimaryRight || m_attached)) {
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRectangle(m_style == HeaderStyle::PrimaryLeft ? w-r-1 : 0,0,r+1,h);
    }
    if (m_style == HeaderStyle::PrimaryRight) {
        gc->SetPen(wxPen(p.action_primary_hover));
        gc->StrokeLine(0,FromDIP(7),0,h-FromDIP(7));
    }

    const double icon_size = FromDIP(m.button.icon.icon_size);
    const HeaderIcon icon = m_open && m_icon == HeaderIcon::Down ? HeaderIcon::Up : m_icon;
    if (m_style == HeaderStyle::PrimaryRight || m_style == HeaderStyle::Outline) {
        draw_icon(*gc, icon, (w-icon_size)/2,(h-icon_size)/2,icon_size,foreground);
        if (HasFocus()) {
            gc->SetBrush(*wxTRANSPARENT_BRUSH); gc->SetPen(wxPen(p.border_focus,FromDIP(2)));
            gc->DrawRoundedRectangle(2,2,w-4,h-4,std::max(0.,r-2));
        }
        return;
    }

    double x = FromDIP(12);
    if (icon != HeaderIcon::None) {
        draw_icon(*gc,icon,x,(h-icon_size)/2,icon_size,foreground);
        x += FromDIP(24);
    }
    if (m_decoration.dot.has_value()) {
        const double dot = FromDIP(8);
        draw_dot(*gc, m_decoration.dot, x, (h-dot)/2, dot, p);
        x += FromDIP(16);
    }

    // Right edge inward: trailing glyph, check, status, detail. Each consumes
    // its slot so the label knows exactly how much room is left.
    double right = w - FromDIP(12);
    if (m_row_action != HeaderIcon::None) {
        // Revealed on hover or keyboard selection; the slot is always
        // reserved, so rows do not shift when the pointer crosses them.
        if (highlighted) {
            const wxRect rect = row_action_rect();
            draw_icon(*gc, m_row_action, rect.x + (rect.width-icon_size)/2, rect.y + (rect.height-icon_size)/2,
                      icon_size, p.text_secondary);
        }
        right -= FromDIP(20);
    }
    if (m_decoration.trailing != HeaderIcon::None) {
        draw_icon(*gc, m_decoration.trailing, right-icon_size, (h-icon_size)/2, icon_size, p.text_secondary);
        right -= FromDIP(20);
    }
    if (m_decoration.check) {
        draw_icon(*gc, HeaderIcon::Check, right-icon_size, (h-icon_size)/2, icon_size, p.action_primary);
        right -= FromDIP(20);
    }
    if (!m_decoration.detail.empty() && !m_decoration.detail_inline) {
        gc->SetFont(detail_font(), p.text_secondary);
        double tw,th; gc->GetTextExtent(m_decoration.detail,&tw,&th);
        gc->DrawText(m_decoration.detail, right-tw, (h-th)/2);
        right -= tw + FromDIP(16);
    }
    if (m_decoration.status != StatusTone::None) {
        if (!m_decoration.status_word.empty()) {
            gc->SetFont(m_theme.font(TextRole::Label), p.text_secondary);
            double tw,th; gc->GetTextExtent(m_decoration.status_word,&tw,&th);
            gc->DrawText(m_decoration.status_word, right-tw, (h-th)/2);
            right -= tw + FromDIP(4);
        }
        const double dot = FromDIP(6);
        const wxColour tone = status_colour(m_decoration.status, p);
        if (m_decoration.status == StatusTone::Neutral) {
            // "Not connected" is an absence, not a state: a dashed ring says
            // so without borrowing a status colour.
            gc->SetPen(wxPen(tone, 1, wxPENSTYLE_SHORT_DASH));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
        } else {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(tone));
        }
        gc->DrawEllipse(right-dot, h/2-dot/2, dot, dot);
        right -= FromDIP(14);
    }

    int available = std::max(0, int(right - x));
    // An inline detail is part of the same line and must never be overrun by
    // the trailing glyph, so it claims its width first and the label
    // ellipsizes into what is left. Under pressure the name gives way and the
    // measurement survives, which is the useful half of "X1 Carbon \xC2\xB7 0.4".
    double inline_run = 0;
    if (!m_decoration.detail.empty() && m_decoration.detail_inline) {
        dc.SetFont(GetFont());
        inline_run = dc.GetTextExtent(wxString::FromUTF8(" \xC2\xB7")).x + FromDIP(3);
        dc.SetFont(detail_font());
        inline_run += dc.GetTextExtent(m_decoration.detail).x;
        available = std::max(0, available - int(inline_run));
    }
    if (m_label_cap > 0) available = std::min(available, FromDIP(m_label_cap));
    dc.SetFont(GetFont());
    const wxString text = wxControl::Ellipsize(GetLabel(), dc, wxELLIPSIZE_END, available);
    gc->SetFont(GetFont(),foreground);
    double tw,th; gc->GetTextExtent(text,&tw,&th);
    const bool two_line = !m_decoration.sub_label.empty();
    const double label_y = two_line ? h/2 - th - FromDIP(1) : (h-th)/2;
    gc->DrawText(text,x,label_y);
    if (!m_decoration.detail.empty() && m_decoration.detail_inline) {
        // Separator and value continue the same line, each in its own face.
        // The separator carries its own leading space; the trailing gap comes
        // from the monospace advance, so adding one here would double it.
        const wxString separator = wxString::FromUTF8(" \xC2\xB7");
        gc->SetFont(GetFont(), p.text_secondary);
        double sw,sh; gc->GetTextExtent(separator,&sw,&sh);
        gc->DrawText(separator, x+tw, (h-sh)/2);
        gc->SetFont(detail_font(), p.text_secondary);
        double dh, dw; gc->GetTextExtent(m_decoration.detail,&dw,&dh);
        gc->DrawText(m_decoration.detail, x+tw+sw+FromDIP(3), (h-dh)/2);
    }
    if (two_line) {
        dc.SetFont(m_theme.font(TextRole::Metadata));
        const wxString sub = wxControl::Ellipsize(m_decoration.sub_label, dc, wxELLIPSIZE_END, available);
        gc->SetFont(m_theme.font(TextRole::Metadata), p.text_secondary);
        gc->DrawText(sub, x, h/2 + FromDIP(1));
    }
    if (m_status) {
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(m_warning ? p.status_warning : p.status_success_on_action));
        gc->DrawEllipse(x+tw+FromDIP(8),h/2-FromDIP(3),FromDIP(6),FromDIP(6));
    }

    // Menu rows never paint a focus ring: the popup owns selection for the whole
    // list, so a row holding native focus is an artifact, not a selected row.
    if (HasFocus() && m_style != HeaderStyle::Menu) {
        gc->SetBrush(*wxTRANSPARENT_BRUSH); gc->SetPen(wxPen(p.border_focus,FromDIP(2)));
        gc->DrawRoundedRectangle(2,2,w-4,h-4,std::max(0.,r-2));
    }
}

HeaderMenu::HeaderMenu(wxWindow* parent, const ShellTheme& theme, bool dark, std::vector<HeaderMenuItem> items)
    : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxFRAME_SHAPED)
    , m_theme(theme)
    , m_dark(dark)
{
    SetName("Header menu");
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    const auto palette = theme.palette(dark);
    const int radius = theme.metrics().popover.radius;
    Bind(wxEVT_PAINT,[this,palette,radius](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(palette.surface_raised)); dc.Clear();
        dc.SetPen(wxPen(palette.border_subtle)); dc.SetBrush(wxBrush(palette.surface_raised));
        dc.DrawRoundedRectangle(GetClientRect().Deflate(1),FromDIP(radius));
    });
    build(std::move(items));
    Bind(wxEVT_CHAR_HOOK,&HeaderMenu::on_key,this);
    // wxPopupFocusHandler forwards CHAR (not CHAR_HOOK) and dismisses an
    // unhandled key on macOS. Handle that native route as well.
    Bind(wxEVT_CHAR,&HeaderMenu::on_key,this);
}

void HeaderMenu::build(std::vector<HeaderMenuItem> items)
{
    const auto palette = m_theme.palette(m_dark);
    const PopoverMetrics& popover = m_theme.metrics().popover;
    const MenuRowMetrics& menu_row = m_theme.metrics().menu_row;
    // A rebuild replaces every row, so drop the old selection rather than
    // leaving an index pointing into a destroyed vector.
    m_selected = -1;
    m_items.clear();
    if (auto* old = GetSizer()) {
        old->Clear(true);
        SetSizer(nullptr, true);
    }
    m_header = nullptr;

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddSpacer(FromDIP(popover.padding_y));
    int placed_rows = 0;
    bool after_row = false; // the row gap sits only between two consecutive rows
    auto place_header = [&] {
        if (!m_header_builder || m_header != nullptr) return;
        m_header = m_header_builder(this);
        if (m_header) sizer->Add(m_header,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,FromDIP(8));
        after_row = false;
    };
    if (m_header_after_rows == 0) place_header();
    for (auto& item : items) {
        if (placed_rows == m_header_after_rows) place_header();
        if (item.separator) {
            auto* line = new wxStaticLine(this);
            line->SetForegroundColour(palette.border_subtle);
            sizer->Add(line,0,wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM,FromDIP(menu_row.side_inset));
            after_row = false;
        }
        // A separator-only entry carries no caption of its own.
        if (item.title && item.label.empty()) continue;
        if (item.title) {
            // Eyebrow: a caption, not a row. It takes no focus, no hover, and
            // no keyboard stop, so arrow keys skip straight over it.
            auto* caption = new wxStaticText(this,wxID_ANY,item.label);
            caption->SetFont(m_theme.font(TextRole::Metadata));
            caption->SetForegroundColour(palette.text_secondary);
            caption->SetBackgroundColour(palette.surface_raised);
            sizer->Add(caption,0,wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM,FromDIP(8));
            ++placed_rows;
            after_row = false;
            continue;
        }
        auto* button = new HeaderButton(this,m_theme,HeaderStyle::Menu,item.label,item.icon);
        button->SetName(item.label);
        item.decoration.detail = item.decoration.detail.empty() ? item.detail : item.decoration.detail;
        button->set_decoration(item.decoration);
        button->set_dark(m_dark);
        button->set_row_action(item.row_action);
        button->Enable(item.enabled);
        if (item.invoke_row_action)
            button->Bind(wxEVT_MENU,[this,action=std::move(item.invoke_row_action)](wxCommandEvent&) {
                // Like keeps_open rows, this rebuilds the popup's own contents.
                // MSVC binds 'this' in a nested lambda's init-capture to the
                // enclosing closure, not to HeaderMenu, so name the menu first.
                auto* menu = this;
                CallAfter([self=wxWeakRef<wxWindow>(menu),action] { if (self) action(); });
            });
        m_items.push_back(button);
        button->Bind(wxEVT_ENTER_WINDOW,[this,index=int(m_items.size()-1)](wxMouseEvent& e) {
            if (m_items[index]->IsEnabled()) select_item(index);
            e.Skip();
        });
        if (after_row) sizer->AddSpacer(FromDIP(popover.row_gap));
        sizer->Add(button,0,wxEXPAND | wxLEFT | wxRIGHT,FromDIP(menu_row.side_inset));
        ++placed_rows;
        after_row = true;
        button->Bind(wxEVT_BUTTON,[this,owner=wxWeakRef<wxWindow>(GetParent()),invoke=std::move(item.invoke),
                                   keeps_open=item.keeps_open](wxCommandEvent&) {
            if (!invoke) return;
            if (keeps_open) {
                // The handler rebuilds this menu's rows, which destroys the
                // button currently delivering this event. Defer past it.
                // MSVC binds 'this' in a nested lambda's init-capture to the
                // enclosing closure, not to HeaderMenu, so name the menu first.
                auto* menu = this;
                CallAfter([self=wxWeakRef<wxWindow>(menu),invoke] { if (self) invoke(); });
                return;
            }
            close();
            if (owner) owner->CallAfter([owner,invoke] { if (owner) invoke(); });
        });
    }
    place_header(); // a step with fewer rows than requested still gets its view
    sizer->AddSpacer(FromDIP(popover.padding_y));
    SetSizerAndFit(sizer);
    // The first build fixes the width; later rebuilds keep it so swapping to
    // the search view does not make the popup jump under the pointer.
    if (m_width == 0)
        m_width = std::clamp(GetSize().x,FromDIP(280),FromDIP(420));
    SetSize(wxSize(m_width,GetSize().y));
    Layout();
#if defined(__WXOSX__) || defined(__WXMSW__)
    // Preserve the actual rounded silhouette (and the compositor's popup
    // shadow), rather than painting a rounded border into a square window.
    auto outline = wxGraphicsRenderer::GetDefaultRenderer()->CreatePath();
    outline.AddRoundedRectangle(0,0,GetSize().x,GetSize().y,FromDIP(popover.radius));
    SetShape(outline);
#endif
}

void HeaderMenu::set_header_builder(std::function<wxWindow*(wxWindow*)> builder, int after_rows)
{
    m_header_builder    = std::move(builder);
    m_header_after_rows = after_rows;
}

void HeaderMenu::replace_items(std::vector<HeaderMenuItem> items)
{
    build(std::move(items));
    reposition();
}

void HeaderMenu::on_key(wxKeyEvent& e)
{
    const int key = e.GetKeyCode();
    if (key == WXK_ESCAPE || key == WXK_TAB) { close(); return; }
    if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER || key == WXK_SPACE) {
        if (auto* button = selected_item()) {
            wxCommandEvent click(wxEVT_BUTTON,button->GetId());
            click.SetEventObject(button);
            button->ProcessWindowEvent(click);
            return;
        }
        e.Skip();
        return;
    }
    if (key == WXK_RIGHT) {
        // The keyboard equivalent of the hover-revealed row action.
        if (auto* button = selected_item(); button != nullptr && button->has_row_action()) {
            button->invoke_row_action();
            return;
        }
        e.Skip();
        return;
    }
    if (key != WXK_UP && key != WXK_DOWN && key != WXK_HOME && key != WXK_END) { e.Skip(); return; }
    if (m_items.empty()) { e.Skip(); return; }
    int index = m_selected;
    // With no row selected, Down starts before the first and Up wraps back
    // from the last -- the same offset trick Home and End use below.
    if (index < 0) index = key == WXK_UP ? 0 : -1;
    if (key == WXK_HOME) index = -1;
    if (key == WXK_END) index = 0;
    const int step = key == WXK_UP || key == WXK_END ? -1 : 1;
    for (size_t n=0;n<m_items.size();++n) {
        index = (index+step+int(m_items.size()))%int(m_items.size());
        if (m_items[index]->IsEnabled()) { select_item(index); break; }
    }
}

void HeaderMenu::open(HeaderButton& anchor)
{
    m_anchor = &anchor;
    anchor.set_open(true);
    reposition();
    Popup();
    Raise();
    // Like an owner-drawn list, the popup owns its keyboard selection. Cocoa
    // can deliver keys to the popup without assigning focus to a child view.
    // A mouse-opened menu highlights nothing until the pointer or an arrow key
    // picks a row; a keyboard-opened one starts on the first row so a keyboard
    // user has a visible starting point. Arrows work from either state.
    if (anchor.activated_by_keyboard())
        for (size_t i=0;i<m_items.size();++i) if (m_items[i]->IsEnabled()) { select_item(int(i)); break; }
}

void HeaderMenu::reposition()
{
    if (!m_anchor) return;
    Position(m_anchor->ClientToScreen(wxPoint(m_anchor->GetSize().x-GetSize().x,m_anchor->GetSize().y+FromDIP(4))),wxSize(0,0));
}

void HeaderMenu::select_item(int index)
{
    if (m_selected >= 0 && m_selected < int(m_items.size())) m_items[m_selected]->set_menu_selected(false);
    m_selected = index;
    m_items[index]->set_menu_selected(true);
}

void HeaderMenu::OnDismiss()
{
    if (m_closed) return; // Native dismissal and an explicit Escape can coincide.
    m_closed = true;
    if (m_anchor) { m_anchor->set_open(false); m_anchor->SetFocus(); }
    if (m_dismissed) m_dismissed();
    Destroy();
}

void HeaderMenu::close() { Dismiss(); OnDismiss(); }

} // namespace Slic3r::GUI::JusPrin
