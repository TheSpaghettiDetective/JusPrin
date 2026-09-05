#pragma once

#include "ShellTheme.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include <wx/control.h>
#include <wx/weakref.h>
#include <functional>
#include <vector>

namespace Slic3r::GUI::JusPrin {

enum class HeaderIcon { None, Back, Down, More, Machine, Slice, Eye, Plates, Export, Print, Cancel };
enum class HeaderStyle { Quiet, Setup, PrimaryLeft, PrimaryRight, Outline, Menu };

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
    void set_slots(std::vector<wxColour> colors);
    void set_detail(const wxString& detail);
    void SetLabel(const wxString& label) override;
    bool Enable(bool enabled = true) override;
    wxSize DoGetBestSize() const override;
    // As with Orca's custom Button, macOS must ask the wx control rather
    // than the generic NSView whether it can become first responder.
    bool AcceptsFocus() const override { return true; }

private:
    void paint(wxPaintEvent&);
    void activate();
    const ShellTheme& m_theme;
    HeaderStyle m_style;
    HeaderIcon m_icon;
    std::vector<wxColour> m_slots;
    wxString m_detail;
    bool m_dark{false}, m_hover{false}, m_pressed{false}, m_status{false}, m_warning{false};
};

struct HeaderMenuItem {
    wxString label;
    HeaderIcon icon{HeaderIcon::None};
    wxString detail;
    bool enabled{true};
    bool separator{false};
    std::function<void()> invoke;
};

// Right-aligned transient menu. The popup owns only presentation; callbacks
// return to the owner after dismissal, never run commands inside mouse capture.
class HeaderMenu : public PopupWindow
{
public:
    HeaderMenu(wxWindow* parent, const ShellTheme& theme, bool dark,
               std::vector<HeaderMenuItem> items);
    void open(wxWindow& anchor);
protected:
    void OnDismiss() override;
private:
    void close();
    std::vector<HeaderButton*> m_items;
    wxWeakRef<wxWindow> m_anchor;
    bool m_closed{false};
};

} // namespace Slic3r::GUI::JusPrin
