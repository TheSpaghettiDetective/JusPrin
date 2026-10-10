#pragma once

// Where the print-issue overlay puts its pieces on the Prepare canvas, as
// plain geometry: no ImGui, wx or OrcaSlicer types, so the rules can be tested
// without a window. All values are device pixels in the canvas's ImGui frame.
//
// The status pill sits in the canvas's lower right corner, where OrcaSlicer
// drew its notices. The list opens upward from it. The bubble goes beside the
// object its issue is about and never on top of it.

#include <vector>

namespace Slic3r::GUI::JusPrin::PrintIssues {

struct OverlayRect
{
    float x{0}, y{0}, w{0}, h{0};
    bool  empty() const { return w <= 0 || h <= 0; }
};

bool overlap(const OverlayRect& a, const OverlayRect& b);

// The pill, the list above it, and the slot a docked bubble takes above both.
struct StatusLayout
{
    OverlayRect pill;
    OverlayRect list; // empty while the list is closed
    // The height a list may take before it has to scroll.
    float       list_max_height{0};
    // The lower right corner a docked bubble grows up and left from.
    float       dock_right{0}, dock_bottom{0};
};

// `list_height` is what the rows need; zero means the list is closed.
StatusLayout layout_status(float canvas_w, float canvas_h, float inset, float gap, float pill_w, float pill_h, float list_w,
                           float list_height);

enum class BubbleSide { Docked, Above, Right, Left, Below };

const char* side_name(BubbleSide side);

struct BubbleRequest
{
    float canvas_w{0}, canvas_h{0};
    float inset{0};  // kept clear along the canvas edge
    float leader{0}; // the gap between the bubble and the object
    float pad{0};    // how far the leader's foot stays from a bubble corner
    float bubble_w{0}, bubble_h{0};
    // The screen rectangle the object covers, and the projected top centre of
    // its box, which is where a bubble above it points.
    OverlayRect target;
    float       anchor_x{0}, anchor_y{0};
    // False when the issue has no object, or its object is not in view: the
    // bubble docks without looking for a side.
    bool        has_target{false};
    // What the bubble must not cover besides the object: the pill, the open
    // list, the tool strip.
    std::vector<OverlayRect> taken;
    float       dock_right{0}, dock_bottom{0};
};

struct BubblePlacement
{
    OverlayRect rect;
    BubbleSide  side{BubbleSide::Docked};
    // From the bubble's edge to the object. Unused while docked.
    float       leader_from_x{0}, leader_from_y{0}, leader_to_x{0}, leader_to_y{0};
};

// Above the object when there is room; otherwise to its right, its left, or
// below it. A side is taken only if the whole bubble fits inside the canvas
// there and covers neither the object nor anything in `taken`. With no side
// free the bubble docks beside the status pill.
BubblePlacement place_bubble(const BubbleRequest& request);

} // namespace Slic3r::GUI::JusPrin::PrintIssues
