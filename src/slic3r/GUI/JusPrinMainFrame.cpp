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
    // Create a custom panel that will act like a button
    class TabButton : public wxPanel {
    public:
        TabButton(wxWindow* parent, wxSize& size, const std::string& image, const std::string& text)
            : wxPanel(parent, wxID_ANY, wxDefaultPosition, size)
        {
            SetBackgroundColour(wxColour(240, 240, 240)); // Default color

            wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

            wxBitmap bitmap(image);
            m_image = new wxStaticBitmap(this, wxID_ANY, bitmap);
            sizer->Add(m_image, 0, wxALIGN_CENTER | wxALL, 5);

            m_text = new wxStaticText(this, wxID_ANY, text);
            sizer->Add(m_text, 0, wxALIGN_CENTER | wxALL, 5);

            SetSizer(sizer);

            // Bind mouse events
            Bind(wxEVT_ENTER_WINDOW, &TabButton::OnMouseEnter, this);
            Bind(wxEVT_LEAVE_WINDOW, &TabButton::OnMouseLeave, this);
            Bind(wxEVT_LEFT_DOWN, &TabButton::OnMouseClick, this);
        }

        void SetSelected(bool selected) {
            if (selected) {
                SetBackgroundColour(wxColour(200, 200, 255)); // Selected color
                // Draw a colored bar on the left using a panel
                if (!m_indicator) {
                    m_indicator = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(3, -1));
                    m_indicator->SetBackgroundColour(wxColour(0, 0, 255)); // Indicator color
                }
                m_indicator->Show();
            } else {
                SetBackgroundColour(wxColour(240, 240, 240)); // Default color
                if (m_indicator) {
                    m_indicator->Hide();
                }
            }
            Refresh();
        }

    private:
        void OnMouseEnter(wxMouseEvent& event) {
            if (!IsSelected()) {
                SetBackgroundColour(wxColour(220, 220, 220)); // Hover color
                Refresh();
            }
            event.Skip();
        }

        void OnMouseLeave(wxMouseEvent& event) {
            if (!IsSelected()) {
                SetBackgroundColour(wxColour(240, 240, 240)); // Default color
                Refresh();
            }
            event.Skip();
        }

        void OnMouseClick(wxMouseEvent& event) {
            // Notify parent about selection
            wxCommandEvent selEvent(wxEVT_BUTTON, GetId());
            ProcessEvent(selEvent);
            event.Skip();
        }

        bool IsSelected() const {
            return GetBackgroundColour() == wxColour(200, 200, 255);
        }

        wxStaticBitmap* m_image;
        wxStaticText* m_text;
        wxPanel* m_indicator = nullptr;
    };

    return new TabButton(parent, size, image, text);
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
