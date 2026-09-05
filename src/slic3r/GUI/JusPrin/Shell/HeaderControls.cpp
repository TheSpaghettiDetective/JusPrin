#include "HeaderControls.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/statline.h>
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
    case HeaderIcon::Down: line({{4,6},{8,10},{12,6}}); break;
    case HeaderIcon::More:
        gc.SetBrush(wxBrush(color));
        for (int i = 0; i < 3; ++i) gc.DrawEllipse(3 + i * 4, 7, 1.5, 1.5);
        break;
    case HeaderIcon::Cancel: line({{4,4},{12,12}}); line({{12,4},{4,12}}); break;
    case HeaderIcon::Machine:
        gc.DrawEllipse(1.5,1.5,13,13);
        line({{5,5},{11,11}}); line({{11,5},{5,11}});
        break;
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
        line({{7,4},{3,4},{3,13},{12,13},{12,9}});
        line({{8,2},{14,2},{14,8}}); line({{14,2},{7,9}}); break;
    case HeaderIcon::Print:
        line({{2,7},{14,2},{9,14},{7,9},{2,7}}); line({{7,9},{14,2}}); break;
    case HeaderIcon::None: break;
    }
    gc.PopState();
}

} // namespace

HeaderButton::HeaderButton(wxWindow* parent, const ShellTheme& theme, HeaderStyle style,
                           const wxString& label, HeaderIcon icon)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
      m_theme(theme), m_style(style), m_icon(icon)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(style == HeaderStyle::Setup ? Label::Head_12 :
            style == HeaderStyle::PrimaryLeft ? Label::Head_14 : Label::Body_14);
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
        m_pressed = false;
        if (HasCapture()) ReleaseMouse();
        Refresh();
        if (invoke) activate();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { m_pressed = false; Refresh(); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE) { m_pressed = true; Refresh(); }
        else if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) activate();
        else e.Skip();
    });
    Bind(wxEVT_KEY_UP, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE && m_pressed) { m_pressed = false; Refresh(); activate(); }
        else e.Skip();
    });
}

void HeaderButton::activate()
{
    if (!IsEnabled()) return;
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}
void HeaderButton::SetLabel(const wxString& label) { wxControl::SetLabel(label); InvalidateBestSize(); Refresh(); }
bool HeaderButton::Enable(bool enabled) { const bool changed = wxControl::Enable(enabled); Refresh(); return changed; }
void HeaderButton::set_dark(bool dark) { m_dark = dark; Refresh(); }
void HeaderButton::set_icon(HeaderIcon icon) { m_icon = icon; InvalidateBestSize(); Refresh(); }
void HeaderButton::set_status(bool visible, bool warning) { m_status = visible; m_warning = warning; InvalidateBestSize(); Refresh(); }
void HeaderButton::set_slots(std::vector<wxColour> colors) { m_slots = std::move(colors); InvalidateBestSize(); Refresh(); }
void HeaderButton::set_detail(const wxString& detail) { m_detail = detail; InvalidateBestSize(); Refresh(); }

wxSize HeaderButton::DoGetBestSize() const
{
    if (m_style == HeaderStyle::PrimaryRight) return FromDIP(wxSize(28,34));
    if (m_style == HeaderStyle::Outline) return FromDIP(wxSize(28,28));
    const int label = GetTextExtent(GetLabel()).x;
    int extra = 24 + (m_icon == HeaderIcon::None ? 0 : 24) + (m_status ? 16 : 0);
    if (m_style == HeaderStyle::Setup) extra += 56; // Four slots and chevron.
    const int detail = m_detail.empty() ? 0 : FromDIP(28) + GetTextExtent(m_detail).x;
    return {label + FromDIP(extra) + detail, FromDIP(m_style == HeaderStyle::Setup ? 28 : m_style == HeaderStyle::Menu ? 40 : 34)};
}

void HeaderButton::paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const auto& p = m_theme.palette(m_dark);
    dc.SetBackground(wxBrush(m_style == HeaderStyle::Menu ? p.surface_raised : p.surface_canvas));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    const bool primary = m_style == HeaderStyle::PrimaryLeft || m_style == HeaderStyle::PrimaryRight;
    const auto foreground = !IsEnabled() ? p.action_disabled_text : primary ? p.action_primary_text :
                            m_style == HeaderStyle::Quiet ? p.text_secondary : p.text_primary;
    wxColour fill = primary ? (!IsEnabled() ? p.action_disabled : m_pressed ? p.action_primary_pressed :
                              m_hover ? p.action_primary_hover : p.action_primary) :
                   m_hover && IsEnabled() ? p.surface_selected :
                   m_style == HeaderStyle::Setup ? p.surface_subtle :
                   m_style == HeaderStyle::Menu ? p.surface_raised : p.surface_canvas;
    const double w = GetClientSize().x, h = GetClientSize().y, r = FromDIP(primary ? 8 : 4);
    gc->SetPen(primary || m_style == HeaderStyle::Quiet || m_style == HeaderStyle::Menu ? *wxTRANSPARENT_PEN : wxPen(p.border_subtle));
    gc->SetBrush(wxBrush(fill));
    gc->DrawRoundedRectangle(0.5,0.5,w-1,h-1,r);
    if (primary) {
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRectangle(m_style == HeaderStyle::PrimaryLeft ? w-r-1 : 0,0,r+1,h);
    }
    if (m_style == HeaderStyle::PrimaryRight) {
        gc->SetPen(wxPen(p.action_primary_hover));
        gc->StrokeLine(0,FromDIP(7),0,h-FromDIP(7));
    }
    const double icon_size = FromDIP(16);
    if (m_style == HeaderStyle::PrimaryRight || m_style == HeaderStyle::Outline) {
        draw_icon(*gc, m_icon, (w-icon_size)/2,(h-icon_size)/2,icon_size,foreground);
    } else {
        double x = FromDIP(12);
        if (m_icon != HeaderIcon::None) {
            draw_icon(*gc,m_icon,x,(h-icon_size)/2,icon_size,foreground);
            x += FromDIP(24);
        }
        double reserve = FromDIP(12 + (m_status ? 16 : 0) + (m_style == HeaderStyle::Setup ? 56 : 0));
        if (!m_detail.empty()) {
            gc->SetFont(Label::Body_10,p.text_secondary);
            double tw,th; gc->GetTextExtent(m_detail,&tw,&th);
            gc->DrawText(m_detail,w-FromDIP(12)-tw,(h-th)/2);
            reserve += tw + FromDIP(20);
        }
        dc.SetFont(GetFont());
        const wxString text = wxControl::Ellipsize(GetLabel(), dc, wxELLIPSIZE_END, std::max(0,int(w-x-reserve)));
        gc->SetFont(GetFont(),foreground);
        double tw,th; gc->GetTextExtent(text,&tw,&th);
        gc->DrawText(text,x,(h-th)/2);
        if (m_status) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(m_warning ? p.status_warning : p.status_success_on_action));
            gc->DrawEllipse(x+tw+FromDIP(8),h/2-FromDIP(3),FromDIP(6),FromDIP(6));
        }
        if (m_style == HeaderStyle::Setup) {
            for (int i=0;i<4;++i) {
                gc->SetPen(wxPen(p.text_secondary));
                gc->SetBrush(i < int(m_slots.size()) ? wxBrush(m_slots[i]) : *wxTRANSPARENT_BRUSH);
                gc->DrawEllipse(w-FromDIP(64-i*10),h/2-FromDIP(3),FromDIP(6),FromDIP(6));
            }
            draw_icon(*gc,HeaderIcon::Down,w-FromDIP(24),(h-icon_size)/2,icon_size,foreground);
        }
    }
    if (HasFocus()) {
        gc->SetBrush(*wxTRANSPARENT_BRUSH); gc->SetPen(wxPen(p.border_focus,FromDIP(2)));
        gc->DrawRoundedRectangle(2,2,w-4,h-4,std::max(0.,r-2));
    }
}

HeaderMenu::HeaderMenu(wxWindow* parent, const ShellTheme& theme, bool dark, std::vector<HeaderMenuItem> items)
    : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxFRAME_SHAPED)
{
    SetName("Header menu");
    const auto palette = theme.palette(dark);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT,[this,palette](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(palette.surface_raised)); dc.Clear();
        dc.SetPen(wxPen(palette.border_subtle)); dc.SetBrush(wxBrush(palette.surface_raised));
        dc.DrawRoundedRectangle(GetClientRect().Deflate(1),FromDIP(8));
    });
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddSpacer(FromDIP(4));
    for (auto& item : items) {
        if (item.separator) {
            auto* line = new wxStaticLine(this);
            line->SetForegroundColour(palette.border_subtle);
            sizer->Add(line,0,wxEXPAND | wxLEFT | wxRIGHT,FromDIP(1));
        }
        auto* button = new HeaderButton(this,theme,HeaderStyle::Menu,item.label,item.icon);
        button->SetName(item.label); button->set_detail(item.detail); button->set_dark(dark); button->Enable(item.enabled);
        m_items.push_back(button);
        sizer->Add(button,0,wxEXPAND | wxLEFT | wxRIGHT,FromDIP(4));
        button->Bind(wxEVT_BUTTON,[this,owner=wxWeakRef<wxWindow>(parent),invoke=std::move(item.invoke)](wxCommandEvent&) {
            close();
            if (owner && invoke) owner->CallAfter([owner,invoke] { if (owner) invoke(); });
        });
    }
    sizer->AddSpacer(FromDIP(4));
    SetSizerAndFit(sizer);
    SetSize(wxSize(std::clamp(GetSize().x,FromDIP(228),FromDIP(420)),GetSize().y));
    Layout();
#if defined(__WXOSX__) || defined(__WXMSW__)
    // Preserve the actual rounded silhouette (and the compositor's popup
    // shadow), rather than painting a rounded border into a square window.
    auto outline = wxGraphicsRenderer::GetDefaultRenderer()->CreatePath();
    outline.AddRoundedRectangle(0,0,GetSize().x,GetSize().y,FromDIP(8));
    SetShape(outline);
#endif
    Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& e) {
        const int key = e.GetKeyCode();
        if (key == WXK_ESCAPE || key == WXK_TAB) { close(); return; }
        if (key != WXK_UP && key != WXK_DOWN && key != WXK_HOME && key != WXK_END) { e.Skip(); return; }
        const auto it = std::find(m_items.begin(),m_items.end(),wxWindow::FindFocus());
        int index = it == m_items.end() ? -1 : int(it-m_items.begin());
        if (key == WXK_HOME) index = -1;
        if (key == WXK_END) index = 0;
        const int step = key == WXK_UP || key == WXK_END ? -1 : 1;
        for (size_t n=0;n<m_items.size();++n) {
            index = (index+step+int(m_items.size()))%int(m_items.size());
            if (m_items[index]->IsEnabled()) { m_items[index]->SetFocus(); break; }
        }
    });
}

void HeaderMenu::open(wxWindow& anchor)
{
    m_anchor = &anchor;
    Position(anchor.ClientToScreen(wxPoint(anchor.GetSize().x-GetSize().x,anchor.GetSize().y)),wxSize(0,0));
    Popup();
    // wxOSX shows popup panels without activating them. Make this panel key
    // before focusing a row, or keyboard input remains in the main window.
    Raise();
    for (auto* button : m_items) if (button->IsEnabled()) { button->SetFocus(); break; }
}
void HeaderMenu::OnDismiss()
{
    if (m_closed) return; // Native dismissal and an explicit Escape can coincide.
    m_closed = true;
    if (m_anchor) m_anchor->SetFocus();
    Destroy();
}
void HeaderMenu::close() { Dismiss(); OnDismiss(); }

} // namespace Slic3r::GUI::JusPrin
