#include "JusPrinPlaterOverlay.hpp"
#include "JusPrinChatPanel.hpp"
#include "JusPrinView3D.hpp" // We need the original implementations for the UI classes

#include <wx/window.h>
#include <wx/animate.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include "../GUI_App.hpp"

namespace Slic3r {
namespace GUI {

// Implementation moved to JusPrinView3D.cpp
// We don't need to redefine it here as we're just reusing the class

// Constants
namespace {
    // Chat panel size constants
    constexpr int MIN_CHAT_HEIGHT = 340;
    constexpr int MIN_CHAT_WIDTH = 420;
    constexpr double CHAT_HEIGHT_RATIO_SMALL = 0.25;
    constexpr double CHAT_WIDTH_RATIO_SMALL = 0.5;
    constexpr double CHAT_HEIGHT_RATIO_LARGE = 0.75;
    constexpr double CHAT_WIDTH_RATIO_LARGE = 0.85;
    constexpr int CHAT_BOTTOM_MARGIN = 10;

    // Button constants
    constexpr int BUTTON_RADIUS = 12;
    constexpr int OVERLAY_IMAGE_HEIGHT = 38;
    constexpr int OVERLAY_IMAGE_WIDTH = 238;
    constexpr int OVERLAY_PADDING = 8;

    // Badge constants
    constexpr int BADGE_SIZE = 22;

    // Animation/Image constants
    constexpr int ANIMATION_WIDTH = 227;
    constexpr int ANIMATION_HEIGHT = 28;
}

// Implementation of JusPrinPlaterOverlay

JusPrinPlaterOverlay::JusPrinPlaterOverlay(wxWindow* parent)
    : m_parent(parent)
{
}

JusPrinPlaterOverlay::~JusPrinPlaterOverlay()
{
    // Don't delete wxWidgets elements - they will be handled by parent window
}

void JusPrinPlaterOverlay::init()
{
    // Create the chat panel as a child of the parent window
    m_chat_panel = new JusPrinChatPanel(m_parent);
    m_chat_panel->Hide();

    // Create overlay button
    m_overlay_btn = new ChatActivationButton(m_parent, wxID_ANY,
        wxPoint((m_parent->GetClientSize().GetWidth() - 200) / 2, m_parent->GetClientSize().GetHeight() - 40),
        wxSize(200, 100));

    // Bind click event to show chat panel
    auto open_chat = [this](wxMouseEvent& evt) {
        showChatPanel();  // Show the chat panel
        m_chat_panel->SendChatPanelFocusEvent("in_focus");
        evt.Skip();
    };
    m_overlay_btn->Bind(wxEVT_LEFT_DOWN, open_chat);
    m_overlay_btn->AddJoin(open_chat);

    // Create notification badges
    m_red_badge = new ActivationButtonNotificationBadge(m_parent, "", wxColour("#E65C5C"));
    m_orange_badge = new ActivationButtonNotificationBadge(m_parent, "", wxColour("#FDB074"));
    m_green_badge = new ActivationButtonNotificationBadge(m_parent, "", wxColour("#009685"));
    
    m_red_badge->SetSize(BADGE_SIZE, BADGE_SIZE);
    m_orange_badge->SetSize(BADGE_SIZE, BADGE_SIZE);
    m_green_badge->SetSize(BADGE_SIZE, BADGE_SIZE);

    // Set proper z-order
    m_overlay_btn->Raise();
    m_green_badge->Raise();
    m_orange_badge->Raise();
    m_red_badge->Raise();
    m_chat_panel->Raise();

    // Initially hide badge elements
    m_red_badge->Hide();
    m_orange_badge->Hide();
    m_green_badge->Hide();

    // Initialize position of overlay button
    updateActivationButtonRect();
    
    // If in developer mode, show the chat panel, otherwise show the button
    if (wxGetApp().app_config->get_bool("developer_mode")) {
        changeChatPanelView("large");
        showChatPanel();
    } else {
        m_chat_panel->Hide();
        m_overlay_btn->Show();
    }
}

void JusPrinPlaterOverlay::showChatPanel()
{
    if (!m_chat_panel) return;

    m_chat_panel->Show();
    m_chat_panel->SetFocus();
    m_overlay_btn->Hide();
    showBadgesIfNecessary();
}

void JusPrinPlaterOverlay::hideChatPanel()
{
    if (!m_chat_panel) return;

    m_chat_panel->Hide();
    m_overlay_btn->Show();
    showBadgesIfNecessary();
}

void JusPrinPlaterOverlay::updateChatPanelRect()
{
    if (!m_chat_panel) return;

    // Get the appropriate ratio based on view mode
    double height_ratio = (m_chatpanel_view_mode == "large") ? CHAT_HEIGHT_RATIO_LARGE : CHAT_HEIGHT_RATIO_SMALL;
    double width_ratio = (m_chatpanel_view_mode == "large") ? CHAT_WIDTH_RATIO_LARGE : CHAT_WIDTH_RATIO_SMALL;

    wxSize size = m_parent->GetClientSize();
    int chat_width = std::max(MIN_CHAT_WIDTH, (int)(size.GetWidth() * width_ratio));
    int chat_height = std::max(MIN_CHAT_HEIGHT, (int)(size.GetHeight() * height_ratio));

    m_chat_panel->SetSize(
        (size.GetWidth() - chat_width) / 2,
        size.GetHeight() - chat_height - CHAT_BOTTOM_MARGIN,
        chat_width,
        chat_height
    );
}

void JusPrinPlaterOverlay::updateActivationButtonRect()
{
    if (!m_overlay_btn) return;
    
    int image_height = OVERLAY_IMAGE_HEIGHT + OVERLAY_PADDING;
    int image_width = OVERLAY_IMAGE_WIDTH + OVERLAY_PADDING;
    int button_y = m_parent->GetClientSize().GetHeight() - image_height - CHAT_BOTTOM_MARGIN;

    m_overlay_btn->SetSize(
        (m_parent->GetClientSize().GetWidth() - image_width) / 2,
        button_y,
        image_width,
        image_height
    );
}

void JusPrinPlaterOverlay::showBadgesIfNecessary() 
{
    if (!m_red_badge || !m_orange_badge || !m_green_badge) return;

    auto formatBadgeText = [](int count) {
        return count > 9 ? "9+" : std::to_string(count);
    };

    m_red_badge->SetText(formatBadgeText(m_red_badge_count));
    m_orange_badge->SetText(formatBadgeText(m_orange_badge_count));
    m_green_badge->SetText(formatBadgeText(m_green_badge_count));

    int image_width = OVERLAY_IMAGE_WIDTH + OVERLAY_PADDING;
    wxRect btn_rect = m_overlay_btn->GetRect();
    int button_y = btn_rect.GetY();

    const int num_visible_badges = (m_red_badge_count > 0) +
           (m_orange_badge_count > 0) +
           (m_green_badge_count > 0);

#ifdef __APPLE__
    constexpr int BADGE_OFFSET_Y = 8;
    constexpr int RIGHT_MARGIN = 10;
    constexpr double BADGE_OVERLAP = 0.75;
#else
    constexpr int BADGE_OFFSET_Y = BADGE_SIZE;
    constexpr int RIGHT_MARGIN = 0;
    constexpr double BADGE_OVERLAP = 1.0;
#endif

    int icon_x = (m_parent->GetClientSize().GetWidth() + image_width) / 2;
    if (num_visible_badges == 1) {
        icon_x -= BADGE_SIZE + RIGHT_MARGIN;
    } else if (num_visible_badges > 1) {
        icon_x -= BADGE_SIZE + BADGE_SIZE * (num_visible_badges - 1) * BADGE_OVERLAP + RIGHT_MARGIN;
    }

    // Position badges
    if (m_green_badge_count > 0) {
        m_green_badge->SetPosition({icon_x, button_y - BADGE_OFFSET_Y});
        icon_x += BADGE_SIZE*BADGE_OVERLAP;
    }

    if (m_orange_badge_count > 0) {
        m_orange_badge->SetPosition({icon_x, button_y - BADGE_OFFSET_Y});
        icon_x += BADGE_SIZE*BADGE_OVERLAP;
    }

    if (m_red_badge_count > 0) {
        m_red_badge->SetPosition({icon_x, button_y - BADGE_OFFSET_Y});
    }

    m_red_badge->Refresh();
    m_orange_badge->Refresh();
    m_green_badge->Refresh();

    bool show_badges = m_overlay_btn->IsShown();
    
    // Handle badge visibility
    if (show_badges) {
        // Show badges with positive counts
        if (m_green_badge_count > 0) m_green_badge->Show();
        if (m_orange_badge_count > 0) m_orange_badge->Show();
        if (m_red_badge_count > 0) m_red_badge->Show();
    } else {
        // Hide all badges when button is hidden
        m_green_badge->Hide();
        m_orange_badge->Hide();
        m_red_badge->Hide();
    }
}

void JusPrinPlaterOverlay::changeChatPanelView(const std::string& viewMode)
{
    if (!m_chat_panel) return;

    m_chatpanel_view_mode = viewMode;
    updateChatPanelRect();
}

void JusPrinPlaterOverlay::setChatPanelVisibility(bool is_visible)
{
    if (is_visible) {
        showChatPanel();
    } else {
        hideChatPanel();
    }
}

void JusPrinPlaterOverlay::setChatPanelNotificationBadges(int red_badge, int orange_badge, int green_badge)
{
    m_red_badge_count = red_badge;
    m_orange_badge_count = orange_badge;
    m_green_badge_count = green_badge;
    showBadgesIfNecessary();
}

void JusPrinPlaterOverlay::onParentResize()
{
    updateChatPanelRect();
    updateActivationButtonRect();
    showBadgesIfNecessary();
}

bool JusPrinPlaterOverlay::getChatPanelVisibility() const
{
    return m_chat_panel && m_chat_panel->IsShown();
}

std::string JusPrinPlaterOverlay::getChatPanelViewMode() const
{
    return m_chatpanel_view_mode;
}

}} // namespace Slic3r::GUI