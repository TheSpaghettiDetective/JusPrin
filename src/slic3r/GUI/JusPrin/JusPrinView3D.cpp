#include "JusPrinView3D.hpp"

#include <wx/statbmp.h>
#include <wx/bitmap.h>
#include <wx/animate.h>
#include <wx/glcanvas.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include "../GLCanvas3D.hpp"
#include "../GUI_Preview.hpp"
#include "../Event.hpp"
#include "../GUI_App.hpp"

namespace {

    // Chat panel size constants
    constexpr int MIN_CHAT_HEIGHT = 340;
    constexpr int MIN_CHAT_WIDTH = 420;

    // Button constants
    constexpr int BUTTON_RADIUS = 12;
    constexpr int BUTTON_SHADOW_OFFSET = 3;

    constexpr int CHAT_BOTTOM_MARGIN = 10;

    // Animation/Image constants
    constexpr int ANIMATION_WIDTH = 227;
    constexpr int ANIMATION_HEIGHT = 28;

    // Badge constants
    constexpr int BADGE_SIZE = 22;

    // Overlay button constants
    constexpr int OVERLAY_IMAGE_HEIGHT = 38;
    constexpr int OVERLAY_IMAGE_WIDTH = 238;
    constexpr int OVERLAY_PADDING = 8;

    // Move these constants to namespace scope since they're used in static config
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

ChatActivationButton::ChatActivationButton(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxTAB_TRAVERSAL | wxBORDER_NONE)
{
#ifdef __APPLE__
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    SetBackgroundColour(wxColour(0, 0, 0, 0));
#endif

    Bind(wxEVT_PAINT, &ChatActivationButton::OnPaint, this);
    m_animationCtrl = new wxAnimationCtrl(this, wxID_ANY);
    wxAnimation animation;
    wxString    gif_url = from_u8((boost::filesystem::path(resources_dir()) /"images/prin_login.gif").make_preferred().string());
    if (animation.LoadFile(gif_url, wxANIMATION_TYPE_GIF)) {
        m_animationCtrl->SetAnimation(animation);
        m_animationCtrl->Play();
    }
    Bind(wxEVT_ENTER_WINDOW, &ChatActivationButton::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &ChatActivationButton::OnMouseLeave, this);
    Bind(wxEVT_MOTION, &ChatActivationButton::OnMouseMove, this);
    m_animationCtrl->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (m_do) {
            m_do(event);
        }
        event.Skip();
    });
}

void ChatActivationButton::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    dc.Clear();

    wxSize size = GetClientSize();
    int width = size.GetWidth();
    int height = size.GetHeight();
    int radius = 12;

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
        // Clear background
        wxColour transparentColour(255, 255, 255, 0);
        gc->SetBrush(wxBrush(transparentColour));
        gc->DrawRectangle(0, 0, width, height);

#ifdef __APPLE__
        // Draw drop shadows with offset
        // First shadow (larger, more diffuse)
        gc->SetBrush(wxBrush(wxColour(10, 10, 10, 8)));
        gc->DrawRoundedRectangle(4, 6, width - 4, height - 4, radius);

        // Second shadow (smaller, more intense)
        gc->SetBrush(wxBrush(wxColour(33, 33, 33, 15)));
        gc->DrawRoundedRectangle(4, 5, width - 6, height - 5, radius);

        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(0, 0, 0, 0) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRoundedRectangle(3, 3, width-6, height-6, radius);

#else
        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(0, 0, 0, 0) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRectangle(0, 0, width-2, height-2);
#endif
        delete gc;
    }
}

void ChatActivationButton::OnMouseEnter(wxMouseEvent& event){
    m_isHovered = true;
    Refresh();
}

void ChatActivationButton::OnMouseLeave(wxMouseEvent& event)  {
    wxPoint mousePos   = ScreenToClient(wxGetMousePosition());
    wxRect  clientRect = GetClientRect();
    if (!clientRect.Contains(mousePos)) {
        m_isHovered = false;
        Refresh();
    }
}

void ChatActivationButton::OnMouseMove(wxMouseEvent& event) {
    wxPoint mousePos   = event.GetPosition();
    wxRect  clientRect = GetClientRect();
    if (!clientRect.Contains(mousePos)) {
        if (m_isHovered) {
            m_isHovered = false;
            Refresh();
        }
    } else {
        if (!m_isHovered) {
            m_isHovered = true;
            Refresh();
        }
    }
}

 void ChatActivationButton::DoSetSize(int x, int y, int width, int height, int sizeFlags){
     m_animationCtrl->SetSize(
         (width - ANIMATION_WIDTH) / 2,
         (height - ANIMATION_HEIGHT) / 2,
         ANIMATION_WIDTH,
         ANIMATION_HEIGHT,
         sizeFlags
     );
     wxPanel::DoSetSize(x, y, width, height, sizeFlags);
 }

// Implement the OnPaint method
void Slic3r::GUI::ActivationButtonNotificationBadge::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = GetClientSize();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
#ifdef __APPLE__
        // Draw with solid color, no border
        gc->SetBrush(wxBrush(m_bgColor));
        gc->SetPen(wxPen(m_bgColor)); // Changed to use background color for no visible border

        // Draw slightly smaller than the full size to ensure margins
        double margin = 1.0;
        gc->DrawEllipse(margin, margin,
                       size.GetWidth() - 2*margin,
                       size.GetHeight() - 2*margin);

        // Draw text in black for maximum contrast
        auto font = GetFont().Scale(0.8);
        gc->SetFont(font, *wxBLACK);
        double textWidth, textHeight;
        gc->GetTextExtent(m_text, &textWidth, &textHeight);

        double x = (size.GetWidth() - textWidth) / 2;
        double y = (size.GetHeight() - textHeight) / 2;
        gc->DrawText(m_text, x, y);
 #else
        int width  = size.GetWidth();
        int height = size.GetHeight();
        gc->SetPen(wxPen(m_bgColor, 1));
        gc->SetBrush(wxBrush(wxColour(*wxWHITE)));
        gc->DrawRectangle(0, 0, width-1, height-1);
        auto font = GetFont().Scale(0.8);
        gc->SetFont(font, m_bgColor);
        double textWidth, textHeight;
        gc->GetTextExtent(m_text, &textWidth, &textHeight);

        double x = (size.GetWidth() - textWidth) / 2;
        double y = (size.GetHeight() - textHeight) / 2;
        gc->DrawText(m_text, x, y);
 #endif

        delete gc;
    }
}

// Define the static configurations
static const ChatPanelConfig SMALL_CONFIG = {
    CHAT_HEIGHT_RATIO_SMALL,
    CHAT_WIDTH_RATIO_SMALL
};

static const ChatPanelConfig LARGE_CONFIG = {
    CHAT_HEIGHT_RATIO_LARGE,
    CHAT_WIDTH_RATIO_LARGE
};

JusPrinView3D::JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : View3D(parent, bed, model, config, process)
{
    // Chat panel now managed by Plater, no initialization needed here
}

JusPrinView3D::~JusPrinView3D()
{
    // Chat panel and related components are now owned by Plater
}

// Forward methods to Plater for chat panel
void JusPrinView3D::changeChatPanelView(const std::string& viewMode) {
    wxGetApp().plater()->changeChatPanelView(viewMode);
}

void JusPrinView3D::setChatPanelVisibility(bool is_visible) {
    wxGetApp().plater()->setChatPanelVisibility(is_visible);
}

void JusPrinView3D::setChatPanelNotificationBadges(int red_badge, int orange_badge, int green_badge) {
    wxGetApp().plater()->setChatPanelNotificationBadges(red_badge, orange_badge, green_badge);
}

std::string JusPrinView3D::getChatPanelViewMode() const {
    return wxGetApp().plater()->getChatPanelViewMode();
}

bool JusPrinView3D::getChatPanelVisibility() const {
    return wxGetApp().plater()->getChatPanelVisibility();
}

JusPrinChatPanel* JusPrinView3D::jusprinChatPanel() const {
    return wxGetApp().plater()->jusprinChatPanel();
}

void JusPrinView3D::OnCanvasMouseDown(SimpleEvent& evt) {
    // Still send out of focus event when canvas is clicked
    if (wxGetApp().plater()->getChatPanelVisibility()) {
        wxGetApp().plater()->jusprinChatPanel()->SendChatPanelFocusEvent("out_of_focus");
    }
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r
