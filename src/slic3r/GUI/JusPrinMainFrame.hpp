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

protected:
    void init_tabpanel() override;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_JusPrinMainFrame_hpp_