#ifndef slic3r_GUI_JusPrinView3D_hpp_
#define slic3r_GUI_JusPrinView3D_hpp_

#include "../../GUI/GUI_Preview.hpp"
#include "JusPrinChatPanel.hpp"

// Forward declarations
class wxWindow;

namespace Slic3r {
namespace GUI {

// Chat panel size configuration
struct ChatPanelConfig {
    double height_ratio;
    double width_ratio;
};

class JusPrinView3D : public View3D {
public:
    JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
    virtual ~JusPrinView3D();

    // Access the chat panel from the plater
    JusPrinChatPanel* jusprinChatPanel() const;

protected:
    void OnCanvasMouseDown(SimpleEvent& evt);

private:
    // Chat panel and related components are now managed by Plater
};


} // namespace GUI
} // namespace Slic3r

#endif
