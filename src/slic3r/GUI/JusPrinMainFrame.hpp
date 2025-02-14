#ifndef slic3r_JusPrinMainFrame_hpp_
#define slic3r_JusPrinMainFrame_hpp_

#include "MainFrame.hpp"

namespace Slic3r {
namespace GUI {

class JusPrinMainFrame : public MainFrame
{
public:
    JusPrinMainFrame();
    ~JusPrinMainFrame() = default;

    void init_tabpanel() override;
    void update_layout() override;

private:
    wxPanel* createTabItem(wxWindow* parent, wxSize& size, std::string image, std::string text);
    wxPanel* createTab(wxWindow* parent, wxSize& size, wxSize& item, std::vector<std::tuple<std::string, std::string>>& image_texts);

    WebViewPanel* m_jusprinwebview{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_JusPrinMainFrame_hpp_
