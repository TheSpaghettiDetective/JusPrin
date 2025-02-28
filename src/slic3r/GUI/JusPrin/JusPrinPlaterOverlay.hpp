#ifndef slic3r_GUI_JusPrinPlaterOverlay_hpp_
#define slic3r_GUI_JusPrinPlaterOverlay_hpp_

#include <wx/panel.h>
#include <string>
#include <functional>

// Forward declarations
class wxWindow;
class wxPaintEvent;
class wxMouseEvent;
class wxAnimationCtrlBase;

namespace Slic3r {
namespace GUI {

// Forward declarations
class JusPrinChatPanel;

// Notification badge for the chat activation button
class ActivationButtonNotificationBadge : public wxPanel {
public:
    ActivationButtonNotificationBadge(wxWindow* parent, const wxString& text, const wxColour& bgColor);

    // Method to update text
    void SetText(const wxString& text) {
        m_text = text;
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent&);
    wxString m_text;
    wxColour m_bgColor;
};

// Button to activate the chat panel
class ChatActivationButton : public wxPanel
{
public:
    ChatActivationButton(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
    void AddJoin(std::function<void(wxMouseEvent&)> do_some) { m_do = do_some; }

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);

private:
    bool                 m_isHovered{false};
    wxAnimationCtrlBase* m_animationCtrl{nullptr};
    std::function<void(wxMouseEvent&)> m_do{nullptr};
};

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