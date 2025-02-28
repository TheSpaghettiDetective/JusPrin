#include "JusPrinView3D.hpp"
#include "JusPrinPlaterOverlay.hpp" // Include for direct overlay access
#include "../GUI_App.hpp" // For wxGetApp()

#include "../GLCanvas3D.hpp"
#include "../GUI_Preview.hpp"
#include "../Event.hpp"

namespace {
    // Chat panel size constants
    constexpr double CHAT_HEIGHT_RATIO_SMALL = 0.25;
    constexpr double CHAT_WIDTH_RATIO_SMALL = 0.5;
    constexpr double CHAT_HEIGHT_RATIO_LARGE = 0.75;
    constexpr double CHAT_WIDTH_RATIO_LARGE = 0.85;

    // Define static configs
    const Slic3r::GUI::ChatPanelConfig SMALL_CONFIG {
        CHAT_HEIGHT_RATIO_SMALL,
        CHAT_WIDTH_RATIO_SMALL
    };

    const Slic3r::GUI::ChatPanelConfig LARGE_CONFIG {
        CHAT_HEIGHT_RATIO_LARGE,
        CHAT_WIDTH_RATIO_LARGE
    };
}

namespace Slic3r {
namespace GUI {

JusPrinView3D::JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : View3D(parent, bed, model, config, process)
{
    // Chat panel now managed by Plater, no initialization needed here
}

JusPrinView3D::~JusPrinView3D()
{
    // Chat panel and related components are now owned by Plater
}

JusPrinChatPanel* JusPrinView3D::jusprinChatPanel() const {
    return wxGetApp().plater()->jusprinChatPanel();
}

void JusPrinView3D::OnCanvasMouseDown(SimpleEvent& evt) {
    // Still send out of focus event when canvas is clicked
    auto* overlay = wxGetApp().plater()->getJusPrinOverlay();
    if (overlay && overlay->getChatPanelVisibility()) {
        auto* chat_panel = overlay->getChatPanel();
        if (chat_panel) chat_panel->SendChatPanelFocusEvent("out_of_focus");
    }
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r
