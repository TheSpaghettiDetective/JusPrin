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
    void update_layout() override;

protected:
    // Override methods from MainFrame as needed
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_JusPrinMainFrame_hpp_