#pragma once

// The print issues on the Prepare canvas: a status pill in the lower right
// corner, the list it opens, and a bubble beside the object the selected
// issue is about, with the two buttons that hand the issue to the chat. Above
// the pill, the outcome of an action OrcaSlicer has just reported.
//
// The pill is there while OrcaSlicer reports an issue, ignored ones included,
// and gone when it reports none: an empty canvas corner is the all-clear.
// When an issue arrives that was not there before, a ring spreads from the
// pill twice, in the colour of its dot. The list and the bubble are opened
// and closed, and only one of them is open at a time. Closing either hides
// it and changes nothing about the issue.
//
// - The list closes on a pick, on a second press of the pill, on Escape, and
//   on a press anywhere outside it.
// - The bubble belongs to the selection. Opening it selects its object, and
//   it closes when that object stops being selected, which is what a click on
//   the empty plate, Escape and a click on another object already do in
//   OrcaSlicer. Turning, moving and zooming the view leave it open, and so
//   does dragging the object. Its close button closes it and leaves the
//   selection alone; a plate-wide issue has no object, so there it and
//   Escape are the ways out. A press on Ask AI or Resolve with AI that
//   reaches the chat closes it too.
// - With one issue the pill opens and closes the bubble itself: a list of one
//   would say the same thing twice.
//
// Drawn with ImGui inside the canvas's own frame through the overlay renderer
// the tool strip uses, so it needs no window over the GL surface and ImGui
// gives it the pointer before the canvas does. Positions are recomputed from
// the camera and the scene every frame; nothing about position is kept.
// Where each piece goes is decided in IssueOverlayLayout.

#include "IssueOverlayLayout.hpp"
#include "PrintIssueMonitor.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
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
    // Empty, with no text, while there is no issue.
    OverlayRect pill;
    std::string pill_text;
    // The ring that announces a new issue was drawn in this frame.
    bool        attention{false};
    // More than one issue: a press opens the list, and the pill says so.
    bool        pill_opens_list{false};
    bool        list_open{false};
    OverlayRect list;
    std::vector<std::pair<std::string, OverlayRect>> rows; // issue id, row
    bool        bubble{false};
    BubbleSide  side{BubbleSide::Docked};
    bool        target_in_view{false};
    float       anchor_x{0}, anchor_y{0};
    OverlayRect bubble_rect, ask, resolve, close;
    // Empty unless drawn: the way to the other issues, and "Ignore this
    // warning" or its undo.
    OverlayRect previous, next, ignore;
    // The confirmations drawn above the pill: OrcaSlicer's notification kind,
    // the panel, and its close button.
    struct NoticeFrame { int kind{0}; OverlayRect rect, close; };
    std::vector<NoticeFrame> notices;
    bool        actions_enabled{false};
    std::string issue_id;
    // Where the bubble's issue is among the ones that count, as "2 of 5".
    int         pager_index{0}, pager_size{0};

    bool docked() const { return side == BubbleSide::Docked; }
};

class IssueOverlay final
{
public:
    struct Actions
    {
        // Highlight the issue's object through OrcaSlicer's own selection.
        std::function<void(const PrintIssue&)> highlight;
        // Sends the issue with the pressed button's request sentence. True
        // when the message reached the chat, which is where the person looks next.
        std::function<bool(const std::string& issue_id, const std::string& request)> submit;
        // A reply is being written; the page disables its own send the same way.
        std::function<bool()> chat_busy;
    };

    IssueOverlay(GLCanvas3D& canvas, PrintIssueMonitor& monitor, Actions actions);
    ~IssueOverlay();

    IssueOverlay(const IssueOverlay&) = delete;
    IssueOverlay& operator=(const IssueOverlay&) = delete;

    // Called by the canvas every frame, inside its ImGui frame.
    void render();

    // Opens the issue's bubble and selects its object.
    void select(const std::string& issue_id);
    void close_bubble();
    void set_list_open(bool open);
    // What a press on the pill does.
    void press_pill();
    const std::string& selected() const { return m_selected; }
    // How often a new issue has been announced, for tests to watch.
    std::uint64_t announcements() const { return m_announcements; }
    const IssueOverlayFrame& last_frame() const { return m_frame; }

private:
    struct Target
    {
        OverlayRect rect;                 // what the object covers on screen
        float       anchor_x{0}, anchor_y{0}; // the projected top centre of its box
    };
    struct Style;
    // Hears the canvas's Escape and its presses ahead of the canvas itself.
    class Input;

    // Whether the selection has the issue's object, or the copy it names.
    bool selection_holds(const PrintIssue& issue) const;
    // Starts the pill's ring when the snapshot holds an issue the last one
    // did not.
    void watch_for_arrivals(const PrintIssueSnapshot& snapshot);
    // True when Escape closed something here and is spent.
    bool on_escape();
    // A press on the canvas, in the overlay's own coordinates.
    void on_pointer_down(float x, float y);

    // Where the issue's object is on screen this frame. Empty when it has
    // none, the object is gone, or any of it is behind the camera.
    std::optional<Target> target_of(const PrintIssue& issue) const;
    void draw_pill(const Style& style, const StatusLayout& layout, const PrintIssueSnapshot& snapshot);
    void draw_list(const Style& style, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, float row_height);
    void draw_bubble(const Style& style, const StatusLayout& layout, const PrintIssueSnapshot& snapshot, const PrintIssue& issue);
    void draw_notices(const Style& style, StatusLayout& layout, const PrintIssueSnapshot& snapshot);
    void after_frame(std::function<void(IssueOverlay&)> action);
    void request_frame();

    GLCanvas3D&        m_canvas;
    PrintIssueMonitor& m_monitor;
    Actions            m_actions;
    std::string        m_selected;
    // The selection has held the bubble's object since the bubble opened.
    // Until it has, losing the selection is not a reason to close.
    bool               m_bound{false};
    bool               m_list_open{false};
    std::unique_ptr<Input> m_input;
    // The issues of the snapshot last looked at, and which one that was.
    std::set<std::string> m_known;
    std::uint64_t         m_known_session{0}, m_known_generation{0};
    std::optional<std::chrono::steady_clock::time_point> m_announced;
    std::uint64_t         m_announcements{0};
    IssueOverlayFrame  m_frame;
    // Expires with the overlay, so an action queued by the last frame is dropped.
    std::shared_ptr<IssueOverlay*> m_self;
};

} // namespace Slic3r::GUI::JusPrin::PrintIssues
