#include "ClassicView.hpp"

#include "ShellRecipes.hpp"
#include "ShellTheme.hpp"

#include "slic3r/GUI/BBLTopbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"

#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

// One row across the window: the sentence on the left, the way back on the
// right. Not dismissible: it is the mode indicator, and the way back goes
// with it.
class ClassicViewBanner : public wxPanel
{
public:
    ClassicViewBanner(wxWindow* parent, const ShellTheme& theme, std::function<void()> on_back)
        : wxPanel(parent, wxID_ANY), m_theme(theme)
    {
        SetName("Classic view banner");
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_text = new wxStaticText(this, wxID_ANY, _L("You're in the classic OrcaSlicer view."));
        m_back = new Button(this, _L("Back to JusPrin"));
        m_back->SetName(_L("Back to JusPrin"));

        // The button's own height plus 8 above and below sets the row's.
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->AddSpacer(FromDIP(16));
        row->Add(m_text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
        row->Add(m_back, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(8));
        row->AddSpacer(FromDIP(16));
        SetSizer(row);

        // Disabled at once: a second press queued before the first runs would
        // ask for a return the first has already made.
        m_back->Bind(wxEVT_BUTTON, [this, on_back = std::move(on_back)](wxCommandEvent&) {
            m_back->Disable();
            on_back();
        });
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            const ShellPalette& p = m_theme.palette(m_dark);
            dc.SetBackground(wxBrush(p.surface_canvas));
            dc.Clear();
            dc.SetPen(wxPen(p.border_subtle));
            dc.DrawLine(0, GetClientSize().y - 1, GetClientSize().x, GetClientSize().y - 1);
        });
        Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
            apply_appearance(GUI_App::dark_mode());
            event.Skip();
        });
        apply_appearance(GUI_App::dark_mode());
    }

    Button* back_button() const { return m_back; }

    void apply_appearance(bool dark)
    {
        m_dark = dark;
        const ShellPalette& palette = m_theme.palette(dark);
        // Before the button: its resting fill is whatever it sits on.
        SetBackgroundColour(palette.surface_canvas);
        style_label(*m_text, m_theme, TextRole::Body, palette.text_secondary);
        style_button(*m_back, m_theme, palette, m_theme.metrics().button.secondary);
        Layout();
        Refresh();
    }

private:
    const ShellTheme& m_theme;
    wxStaticText* m_text{nullptr};
    Button* m_back{nullptr};
    bool m_dark{false};
};

ClassicViewReturn::ClassicViewReturn(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer, const ShellTheme& theme,
                                     std::function<void()> on_return)
    : m_frame(frame), m_tabpanel(tabpanel), m_main_sizer(main_sizer), m_theme(theme), m_on_return(std::move(on_return))
{}

ClassicViewReturn::~ClassicViewReturn()
{
    // After the frame is gone on_frame_destroy has already abandoned
    // everything, so this touches no widget then.
    uninstall();
}

void ClassicViewReturn::install()
{
    // Both hold whenever the shell is off: its uninstall, and its rollback of
    // a failed install, put the Notebook back in the main sizer.
    if (m_installed)
        throw std::logic_error("the classic view's way back is already installed");
    std::size_t at = 0;
    for (wxSizerItem* item : m_main_sizer.GetChildren()) {
        if (item->GetWindow() == &m_tabpanel)
            break;
        ++at;
    }
    if (at == m_main_sizer.GetItemCount())
        throw std::logic_error("the tab panel is not in the main layout");

    // The frame may outlive this controller, which lives in a static slot,
    // so its destruction must reach the controller first.
    m_frame.Bind(wxEVT_DESTROY, &ClassicViewReturn::on_frame_destroy, this);
    m_installed = true;

    m_banner = new ClassicViewBanner(&m_frame, m_theme, [this] { m_frame.CallAfter(m_on_return); });
    m_main_sizer.Insert(at, m_banner, 0, wxEXPAND);
    m_frame.Layout();

    m_view_menu = find_view_menu();
    m_menu_separator = m_view_menu->AppendSeparator();
    m_menu_row = m_view_menu->Append(wxID_ANY, _L("Switch to JusPrin view"));
    m_view_menu->Bind(wxEVT_MENU, &ClassicViewReturn::on_menu_row, this, m_menu_row->GetId());
}

void ClassicViewReturn::uninstall()
{
    if (!m_installed)
        return;
    m_installed = false;
    m_frame.Unbind(wxEVT_DESTROY, &ClassicViewReturn::on_frame_destroy, this);
    if (m_menu_row != nullptr) {
        m_view_menu->Unbind(wxEVT_MENU, &ClassicViewReturn::on_menu_row, this, m_menu_row->GetId());
        m_view_menu->Destroy(m_menu_row);
        m_view_menu->Destroy(m_menu_separator);
        m_menu_row = nullptr;
        m_menu_separator = nullptr;
    }
    m_view_menu = nullptr;
    m_main_sizer.Detach(m_banner);
    m_banner->Destroy();
    m_banner = nullptr;
    m_frame.Layout();
}

wxWindow* ClassicViewReturn::banner() const { return m_banner; }

Button* ClassicViewReturn::back_button() const { return m_banner != nullptr ? m_banner->back_button() : nullptr; }

wxMenu* ClassicViewReturn::find_view_menu() const
{
    // By OrcaSlicer's localized title, as the shell finds its Calibration menu.
#ifdef __APPLE__
    wxMenuBar* bar = m_frame.GetMenuBar();
    const int at = bar->FindMenu(_L("View"));
    if (at == wxNOT_FOUND)
        throw std::runtime_error("OrcaSlicer's View menu is not on the menu bar");
    return bar->GetMenu(at);
#else
    const wxString title = wxStripMenuCodes(_L("View"));
    for (wxMenuItem* item : m_frame.topbar()->GetTopMenu()->GetMenuItems())
        if (item->IsSubMenu() && item->GetItemLabelText() == title)
            return item->GetSubMenu();
    throw std::runtime_error("OrcaSlicer's View menu is not in the title bar menu");
#endif
}

void ClassicViewReturn::on_frame_destroy(wxWindowDestroyEvent& event)
{
    // Teardown abandons: the banner and the menu go with the frame.
    if (event.GetWindow() == &m_frame) {
        m_installed = false;
        m_banner = nullptr;
        m_view_menu = nullptr;
        m_menu_separator = nullptr;
        m_menu_row = nullptr;
    }
    event.Skip();
}

void ClassicViewReturn::on_menu_row(wxCommandEvent&)
{
    m_frame.CallAfter(m_on_return);
}

} // namespace Slic3r::GUI::JusPrin
