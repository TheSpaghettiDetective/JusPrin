#include "ClassicView.hpp"
#include "NoticeRow.hpp"

#include "slic3r/GUI/BBLTopbar.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"

#include <wx/menu.h>
#include <wx/sizer.h>

#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

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

    // Not dismissible: it is the mode indicator, and the way back goes with it.
    m_banner = new NoticeRow(&m_frame, m_theme, _L("You're in the classic OrcaSlicer view."), _L("Back to JusPrin"),
                             [this] { m_frame.CallAfter(m_on_return); });
    m_banner->SetName("Classic view banner");
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

Button* ClassicViewReturn::back_button() const { return m_banner != nullptr ? m_banner->button() : nullptr; }

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
