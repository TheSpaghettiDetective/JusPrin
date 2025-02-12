#include "JusPrinMainFrame.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Notebook.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace GUI {

JusPrinMainFrame::JusPrinMainFrame()
    : MainFrame()
{
    // Initialize only the essential components
    wxGetApp().update_fonts(this);
    this->SetFont(this->normal_font());

    // Create main sizer
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_main_sizer);

    // Initialize tabpanel
    init_tabpanel();

    // Set minimum size and default size
    const wxSize min_size = wxGetApp().get_min_size();
    SetMinSize(min_size);
    SetSize(wxSize(FromDIP(1200), FromDIP(800)));

    Layout();
    Fit();

    // Set the icon - using platform specific code
#ifdef _WIN32
    std::wstring path(size_t(MAX_PATH), wchar_t(0));
    int len = int(::GetModuleFileName(nullptr, path.data(), MAX_PATH));
    if (len > 0 && len < MAX_PATH) {
        path.erase(path.begin() + len, path.end());
        SetIcon(wxIcon(path, wxBITMAP_TYPE_ICO));
    }
#else
    SetIcon(wxIcon(Slic3r::var("OrcaSlicer_128px.png"), wxBITMAP_TYPE_PNG));
#endif
}

void JusPrinMainFrame::init_tabpanel()
{
    // Create a minimal tabpanel with just the essential functionality
    m_tabpanel = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, nullptr,
                             wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);

    m_tabpanel->SetBackgroundColour(*wxBLUE);
    m_tabpanel->SetFont(Slic3r::GUI::wxGetApp().normal_font());

    // Add tabpanel to main sizer
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND);

    m_settings_dialog.set_tabpanel(m_tabpanel);
}

} // namespace GUI
} // namespace Slic3r