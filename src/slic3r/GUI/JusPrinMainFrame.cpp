#include "JusPrinMainFrame.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Notebook.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r {
namespace GUI {

JusPrinMainFrame::JusPrinMainFrame()
    : MainFrame(){}

void JusPrinMainFrame::init_tabpanel()
{
    // First call parent's init_tabpanel
    MainFrame::init_tabpanel();

    // Create our custom tab panel
    std::string icon_path = (boost::format("%1%/images/OrcaSlicer_32px.png") % resources_dir()).str();
    std::vector<std::tuple<std::string, std::string>> image_texts = {
        { icon_path, "Text" },
        { icon_path, "Text1" },
        { icon_path, "Text2" },
        { icon_path, "Text3" },
        { icon_path, "Text4" },
        { icon_path, "Text5" },
    };

    wxSize size(50, 300);
    wxSize itemSize(50, 50);
    wxPanel* tabPanel = createTab(this, size, itemSize, image_texts);

    // Create webview panel instead of test panel
    m_jusprinwebview = new WebViewPanel(this);

    // Create horizontal sizer
    auto horizontalSizer = new wxBoxSizer(wxHORIZONTAL);
    horizontalSizer->Add(tabPanel, 0, wxEXPAND);
    horizontalSizer->Add(m_jusprinwebview, 1, wxEXPAND);

    // Clear and set the main sizer
    m_main_sizer->Clear();
    m_main_sizer->Add(horizontalSizer, 1, wxEXPAND);

    Layout();
}

void JusPrinMainFrame::update_layout(){
    MainFrame::update_layout();
//    m_plater->Hide();
//    m_tabpanel->Hide();
    m_plater->Hide();
    m_tabpanel->Hide();
}

wxPanel* JusPrinMainFrame::createTabItem(wxWindow* parent, wxSize& size, std::string image, std::string text) {
    wxPanel* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, size);
    panel->SetBackgroundColour(*wxRED);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxBitmap bitmap(image);
    wxStaticBitmap* imageCtrl = new wxStaticBitmap(panel, wxID_ANY, bitmap);
    sizer->Add(imageCtrl, 0, wxALIGN_CENTER | wxALL, 5);

    wxStaticText* textCtrl = new wxStaticText(panel, wxID_ANY, text);
    sizer->Add(textCtrl, 0, wxALIGN_CENTER | wxALL, 5);

    panel->SetSizer(sizer);
    return panel;
}

wxPanel* JusPrinMainFrame::createTab(wxWindow* parent, wxSize& size, wxSize& item, std::vector<std::tuple<std::string, std::string>>& image_texts) {
    wxPanel* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, size);
    panel->SetBackgroundColour(*wxBLUE);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    for(auto& [image, text] : image_texts) {
        wxPanel* tabItem = createTabItem(panel, item, image, text);
        sizer->Add(tabItem, 0, wxALIGN_CENTER | wxALL, 5);
    }

    panel->SetSizer(sizer);
    return panel;
}

} // namespace GUI
} // namespace Slic3r
