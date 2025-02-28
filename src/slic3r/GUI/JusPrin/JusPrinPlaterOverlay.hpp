#ifndef slic3r_GUI_JusPrinPlaterOverlay_hpp_
#define slic3r_GUI_JusPrinPlaterOverlay_hpp_

#include <wx/panel.h>
#include <string>
#include "JusPrinView3D.hpp" // Include this to use the UI classes defined there

namespace Slic3r {
namespace GUI {

// Class to manage the chat panel and related UI components that display 
// on top of the Plater in both Prepare and Preview tabs
class JusPrinPlaterOverlay
{
public:
    JusPrinPlaterOverlay(wxWindow* parent);
    ~JusPrinPlaterOverlay();
    
    // Initialize the overlay components
    void init();
    
    // Chat panel management methods
    void showChatPanel();
    void hideChatPanel();
    void changeChatPanelView(const std::string& viewMode);
    void setChatPanelVisibility(bool is_visible);
    void setChatPanelNotificationBadges(int red_badge, int orange_badge, int green_badge);
    
    // Update UI positions and visibility
    void updateChatPanelRect();
    void updateActivationButtonRect();
    void showBadgesIfNecessary();
    void onParentResize();
    
    // State getters
    bool getChatPanelVisibility() const;
    std::string getChatPanelViewMode() const;
    JusPrinChatPanel* getChatPanel() const { return m_chat_panel; }

private:
    wxWindow* m_parent;
    
    // Chat panel components
    JusPrinChatPanel* m_chat_panel{nullptr};
    ChatActivationButton* m_overlay_btn{nullptr};
    ActivationButtonNotificationBadge* m_red_badge{nullptr};
    ActivationButtonNotificationBadge* m_orange_badge{nullptr};
    ActivationButtonNotificationBadge* m_green_badge{nullptr};
    
    // Chat panel state
    std::string m_chatpanel_view_mode{"large"}; // Default to large view
    int m_red_badge_count{0};
    int m_orange_badge_count{0};
    int m_green_badge_count{0};
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_JusPrinPlaterOverlay_hpp_