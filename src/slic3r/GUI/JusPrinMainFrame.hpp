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
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_JusPrinMainFrame_hpp_
