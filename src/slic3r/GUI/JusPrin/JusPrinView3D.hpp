#ifndef slic3r_GUI_JusPrinView3D_hpp_
#define slic3r_GUI_JusPrinView3D_hpp_

#include "../../GUI/GUI_Preview.hpp"
#include "JusPrinChatPanel.hpp"

// Forward declarations
class wxStaticBitmap;
class wxWindow;
class wxSizeEvent;

namespace Slic3r {
namespace GUI {

class JusPrinView3D : public View3D {
public:
    JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
    virtual ~JusPrinView3D();

protected:
    void OnSize(wxSizeEvent& evt);
    void OnCanvasClick(SimpleEvent& evt);

private:
    JusPrinChatPanel* m_chat_panel;
    wxStaticBitmap* m_overlay_image;
    wxAnimationCtrlBase* m_animationCtrl{nullptr};

    void init_jusprin_elements();
};

} // namespace GUI
} // namespace Slic3r

#endif