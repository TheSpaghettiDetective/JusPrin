#pragma once

// The print issues on the Prepare canvas: a status pill in the lower right
// corner, the list it opens, and a bubble beside the object the selected
// issue is about, with the two buttons that hand the issue to the chat.
//
// Drawn with ImGui inside the canvas's own frame through the overlay renderer
// the tool strip uses, so it needs no window over the GL surface and ImGui
// gives it the pointer before the canvas does. Positions are recomputed from
// the camera and the scene every frame; nothing about position is kept.
// Where each piece goes is decided in IssueOverlayLayout.

#include "IssueContext.hpp"
#include "IssueOverlayLayout.hpp"
#include "PrintIssueMonitor.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI {
class GLCanvas3D;
}

namespace Slic3r::GUI::JusPrin {
struct ShellMetrics;
struct ShellPalette;
} // namespace Slic3r::GUI::JusPrin

namespace Slic3r::GUI::JusPrin::PrintIssues {

// What the last frame drew, in the canvas's ImGui coordinates. The harness
// presses these rectangles and compares the anchor with its own projection.
struct IssueOverlayFrame
{
    OverlayRect pill;
    std::string pill_text;
    bool        list_open{false};
    std::vector<std::pair<std::string, OverlayRect>> rows; // issue id, row
    bool        bubble{false};
    BubbleSide  side{BubbleSide::Docked};
    bool        target_in_view{false};
    float       anchor_x{0}, anchor_y{0};
    OverlayRect bubble_rect, ask, resolve, next;
    bool        actions_enabled{false};
    std::string issue_id;
    int         group_index{0}, group_size{0};

    bool docked() const { return side == BubbleSide::Docked; }
};

class IssueOverlay final
{
public:
    struct Actions
    {
        // Highlight the issue's object through OrcaSlicer's own selection.
        std::function<void(const PrintIssue&)> highlight;
        std::function<void(const std::string& issue_id, IssueIntent intent)> submit;
        // A reply is being written; the page disables its own send the same way.
        std::function<bool()> chat_busy;
    };

    IssueOverlay(GLCanvas3D& canvas, PrintIssueMonitor& monitor, Actions actions);
    ~IssueOverlay();

    IssueOverlay(const IssueOverlay&) = delete;
    IssueOverlay& operator=(const IssueOverlay&) = delete;

    // Called by the canvas every frame, inside its ImGui frame.
    void render();

    void select(const std::string& issue_id);
    void set_list_open(bool open);
    const std::string& selected() const { return m_selected; }
    const IssueOverlayFrame& last_frame() const { return m_frame; }

private:
    struct Target
    {
        OverlayRect rect;                 // what the object covers on screen
        float       anchor_x{0}, anchor_y{0}; // the projected top centre of its box
    };
    struct Style;

    // Where the issue's object is on screen this frame. Empty when it has
    // none, the object is gone, or any of it is behind the camera.
    std::optional<Target> target_of(const PrintIssue& issue) const;
    void draw_pill(const Style& style, const StatusLayout& layout, const PrintIssueSnapshot& snapshot);
    void draw_list(const Style& style, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, float row_height);
    void draw_bubble(const Style& style, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, const PrintIssue& issue);
    void after_frame(std::function<void(IssueOverlay&)> action);
    void request_frame();

    GLCanvas3D&        m_canvas;
    PrintIssueMonitor& m_monitor;
    Actions            m_actions;
    std::string        m_selected;
    bool               m_list_open{false};
    IssueOverlayFrame  m_frame;
    // Expires with the overlay, so an action queued by the last frame is dropped.
    std::shared_ptr<IssueOverlay*> m_self;
};

} // namespace Slic3r::GUI::JusPrin::PrintIssues
