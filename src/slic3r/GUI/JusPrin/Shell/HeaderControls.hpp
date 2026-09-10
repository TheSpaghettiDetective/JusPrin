#pragma once

#include "ShellTheme.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include <wx/bitmap.h>

class wxGraphicsContext;
#include <wx/control.h>
#include <wx/weakref.h>
#include <functional>
#include <optional>
#include <vector>

namespace Slic3r::GUI::JusPrin {

// Caret is the chip's own solid disclosure triangle; Down/Up/Right are the
// lighter strokes the menu rows use.
// PanelOpen and PanelClosed are one control's two states: a window with its
// right-hand panel filled in, or the same window with that panel empty.
enum class HeaderIcon { None, Back, Down, Up, Right, Caret, More, Printer, Monitor, Check, Slice, Eye, Plates, Export, Print, Cancel,
                        PanelOpen, PanelClosed };

// ChipLeft and ChipRight are the two halves of the printer/spool chip. Each is
// a separate focus target, and each paints its own outer half of one shared
// outline, the way PrimaryLeft/PrimaryRight already split the print action.
enum class HeaderStyle { Quiet, ChipLeft, ChipRight, PrimaryLeft, PrimaryRight, Outline, Menu };

// A status is a colour plus a word, never a colour alone: the design system
// requires state to be readable without relying on hue.
enum class StatusTone { None, Neutral, Positive, Busy, Warning };

// Everything a menu row can carry beyond its label. Grouped rather than spread
// over a dozen setters because HeaderMenu builds a row in one shot, and a row
// with no decoration should read as exactly that at the call site.
struct HeaderRowDecoration
{
    // Leading dot. Set with a valid colour for a filled swatch; set with an
    // invalid colour for the dashed outline the "no spool yet" rows use.
    std::optional<wxColour> dot;
    // Right-aligned secondary text, in the label role.
    wxString   detail;
    // Draws the detail immediately after the label, separated by a middot,
    // instead of right-aligning it. The chip's nozzle reads as part of one
    // line -- "X1 Carbon · 0.4" -- while staying regular beside the bold
    // printer name, which one text run could not do.
    bool       detail_inline{false};
    // Second line under the label, in text/secondary.
    wxString   sub_label;
    bool       bold{false};
    bool       danger{false};
    bool       check{false};
    HeaderIcon trailing{HeaderIcon::None};
    StatusTone status{StatusTone::None};
    wxString   status_word;
};

// Fork-owned painting, with the same wxEVT_BUTTON contract as native controls.
// Each half of the split action remains a separate keyboard focus target.
class HeaderButton : public wxControl
{
public:
    HeaderButton(wxWindow* parent, const ShellTheme& theme, HeaderStyle style,
                 const wxString& label, HeaderIcon icon);
    void set_dark(bool dark);
    void set_icon(HeaderIcon icon);
    void set_status(bool visible, bool warning = false);
    void set_detail(const wxString& detail);
    void set_decoration(HeaderRowDecoration decoration);
    // A second, interactive glyph at the row's right end, revealed while the
    // row is hovered or selected. Clicking it -- or right-clicking the row --
    // raises wxEVT_MENU instead of wxEVT_BUTTON, so one row can carry a
    // secondary action without a second focusable control.
    void set_row_action(HeaderIcon icon);
    bool has_row_action() const { return m_row_action != HeaderIcon::None; }
    // Raises the row action as though its glyph had been clicked.
    void invoke_row_action();
    const HeaderRowDecoration& decoration() const { return m_decoration; }
    // Caps the label before it ellipsizes, so one long preset name cannot push
    // the rest of the header off the row. 0 means no cap.
    void set_label_cap(int cap_dip);
    void set_menu_selected(bool selected) { m_menu_selected = selected; Refresh(); }
    // A primary-left half squares its right edge only while a menu half sits beside it.
    void set_attached(bool attached) { m_attached = attached; Refresh(); }
    // A menu trigger holds a pressed fill and flips its chevron for as long as
    // its menu is open. HeaderMenu owns the flag; nothing else may set it.
    void set_open(bool open) { m_open = open; Refresh(); }
    bool is_open() const { return m_open; }
    // Whether the last activation came from Return/Space rather than a click.
    // A menu anchored here highlights its first row only in the keyboard case.
    bool activated_by_keyboard() const { return m_keyboard_activated; }
    void SetLabel(const wxString& label) override;
    bool Enable(bool enabled = true) override;
    wxSize DoGetBestSize() const override;
    // As with Orca's custom Button, macOS must ask the wx control rather
    // than the generic NSView whether it can become first responder.
    bool AcceptsFocus() const override { return true; }
    // Draws the control into an offscreen bitmap using the same code the paint
    // event uses, so appearance can be verified from inside the running
    // application in both modes without a screen capture.
    wxBitmap snapshot();

private:
    void paint(wxPaintEvent&);
    void draw(wxDC& dc, wxGraphicsContext& context, const wxSize& client);
    void activate(bool from_keyboard);
    const wxFont& role_font() const;
    const wxFont& detail_font() const;
    int  trailing_reserve() const;
    // Screen rect of the row-action glyph, empty when there is none.
    wxRect row_action_rect() const;
    const ShellTheme& m_theme;
    HeaderStyle m_style;
    HeaderIcon m_icon;
    HeaderRowDecoration m_decoration;
    HeaderIcon m_row_action{HeaderIcon::None};
    int m_label_cap{0};
    bool m_dark{false}, m_hover{false}, m_pressed{false}, m_status{false}, m_warning{false};
    bool m_menu_selected{false};
    bool m_attached{true};
    bool m_open{false};
    bool m_keyboard_activated{false};
};

struct HeaderMenuItem {
    wxString label;
    HeaderIcon icon{HeaderIcon::None};
    wxString detail;
    bool enabled{true};
    bool separator{false};
    std::function<void()> invoke;
    // A title row: an Eyebrow-styled caption that is never selectable and
    // never invokable. `label` carries the caption.
    bool title{false};
    // Keeps the popup open when invoked -- for a row that swaps the menu's own
    // contents (a sub-list, a back step) rather than finishing a task.
    bool keeps_open{false};
    // Secondary action revealed at the row's right end on hover, right-click,
    // or Right arrow. Always keeps the popup open: it opens a nested step.
    HeaderIcon row_action{HeaderIcon::None};
    std::function<void()> invoke_row_action;
    HeaderRowDecoration decoration;
};

// Right-aligned transient menu. The popup owns only presentation; callbacks
// return to the owner after dismissal, never run commands inside mouse capture.
//
// A menu can also replace its own contents in place -- the spool menu's
// "Other spool…" view does this rather than opening a second popup, so the
// popup keeps its position, its keyboard model, and its dismissal handling.
class HeaderMenu : public PopupWindow
{
public:
    HeaderMenu(wxWindow* parent, const ShellTheme& theme, bool dark,
               std::vector<HeaderMenuItem> items);
    void open(HeaderButton& anchor);
    HeaderButton* selected_item() const { return m_selected < 0 ? nullptr : m_items[m_selected]; }

    // Swaps the rows for a new set and re-lays out at the same anchor. The
    // width stays put so the popup does not jump as the person types.
    void replace_items(std::vector<HeaderMenuItem> items);
    // A custom view above the rows -- the search field of the "Other spool…"
    // step. The builder runs with the popup as parent; nullptr clears it.
    // `after_rows` places the custom view below that many rows, so a step can
    // keep its title/back row at the top where the design puts it.
    void set_header_builder(std::function<wxWindow*(wxWindow*)> builder, int after_rows = 0);
    // Invoked when the popup dismisses for any reason, after the anchor has
    // been released. Used to drop menu-owned state the owner is holding.
    void set_dismiss_listener(std::function<void()> listener) { m_dismissed = std::move(listener); }

    // Dismisses the popup and releases its anchor. Public because closing a
    // menu is a normal thing for an owner (or a test) to ask for; Dismiss()
    // alone would leave the anchor painting its open state.
    void close();

protected:
    void OnDismiss() override;

private:
    void select_item(int index);
    void build(std::vector<HeaderMenuItem> items);
    void on_key(wxKeyEvent& event);
    void reposition();

    const ShellTheme& m_theme;
    bool m_dark{false};
    std::vector<HeaderButton*> m_items;
    wxWindow* m_header{nullptr};
    std::function<wxWindow*(wxWindow*)> m_header_builder;
    int m_header_after_rows{0};
    std::function<void()> m_dismissed;
    wxWeakRef<HeaderButton> m_anchor;
    bool m_closed{false};
    int m_selected{-1};
    int m_width{0};
};

} // namespace Slic3r::GUI::JusPrin
