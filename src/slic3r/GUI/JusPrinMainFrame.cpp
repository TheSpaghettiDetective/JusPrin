#include "JusPrinMainFrame.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Notebook.hpp"

namespace Slic3r {
namespace GUI {

JusPrinMainFrame::JusPrinMainFrame()
    : MainFrame()
{
}

void JusPrinMainFrame::update_layout()
{
    if (!m_main_sizer) {
        m_main_sizer = new wxBoxSizer(wxVERTICAL);
        SetSizer(m_main_sizer);
    }

    m_main_sizer->Clear();  // Remove all existing items

    // Only add the tabpanel
    if (m_tabpanel) {
        m_main_sizer->Add(m_tabpanel, 1, wxEXPAND);
    }

    m_main_sizer->Layout();
    Layout();
}

} // namespace GUI
} // namespace Slic3r