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
    auto createTabItem = [this](wxSize& size, std::string image, std::string text){
        wxPanel* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, size);
        panel->SetBackgroundColour(*wxRED);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        wxBitmap bitmap(image);
        wxStaticBitmap* imageCtrl = new wxStaticBitmap(panel, wxID_ANY, bitmap);
        sizer->Add(imageCtrl, 0, wxALIGN_CENTER | wxALL, 5);

        wxStaticText* textCtrl = new wxStaticText(panel, wxID_ANY, text);
        sizer->Add(textCtrl, 0, wxALIGN_CENTER | wxALL, 5);

        panel->SetSizer(sizer);
        return panel;
    };

    
    auto createTab = [this, createTabItem](wxSize& size, wxSize& item, std::vector<std::tuple<std::string, std::string>> image_texts){
        wxPanel* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, size);
        panel->SetBackgroundColour(*wxBLUE);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        for(auto& [image, text] : image_texts) {
            wxPanel* tabItem = createTabItem(item, image, text);
            sizer->Add(tabItem, 0, wxALIGN_CENTER | wxALL, 5);
        }

        panel->SetSizer(sizer);
        return panel;

    };

    std::string icon_path = (boost::format("%1%/images/OrcaSlicer_32px.png") % resources_dir()).str();
    std::vector<std::tuple<std::string, std::string>> image_texts = {
        { icon_path, "Text" },
        { icon_path, "Text" },
        { icon_path, "Text" },
        { icon_path, "Text" },
        { icon_path, "Text" },
        { icon_path, "Text" },
        { icon_path, "Text" },
        { icon_path, "Text" }
    };

    wxSize size(50, 300);
    wxSize itemSize(50, 50);
    wxPanel* tabPanel = createTab(size, itemSize, image_texts);
    
    auto sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(tabPanel, 0, wxALIGN_LEFT, 5);
    sizer->Add(m_tabpanel,0, wxEXPAND, 1);
    
    //auto node = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer,
     //                             wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    //node->SetBackgroundColour(*wxRED);
    m_main_sizer->Add(sizer, 0, wxEXPAND | wxTOP | wxLeft, 0);
    MainFrame::init_tabpanel();
    
//    m_tabpanel = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, nullptr,
//                              wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
//    m_tabpanel->SetBackgroundColour(*wxBLUE);
//    m_tabpanel->Hide();
//
//
//    m_plater = new Plater(this, this);
//    m_plater->SetBackgroundColour(*wxWHITE);
//    m_plater->Hide();
//
//    wxGetApp().plater_ = m_plater;
//
//    create_preset_tabs();

}

void JusPrinMainFrame::update_layout(){
    MainFrame::update_layout();
//    m_plater->Hide();
//    m_tabpanel->Hide(); 
    m_plater->Hide();
    m_tabpanel->Hide();
}

} // namespace GUI
} // namespace Slic3r
