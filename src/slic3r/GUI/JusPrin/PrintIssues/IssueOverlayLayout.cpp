#include "IssueOverlayLayout.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::PrintIssues {

bool overlap(const OverlayRect& a, const OverlayRect& b)
{
    return !a.empty() && !b.empty() && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

StatusLayout layout_status(float canvas_w, float canvas_h, float inset, float gap, float pill_w, float pill_h, float list_w,
                           float list_height)
{
    StatusLayout layout;
    layout.pill            = {canvas_w - inset - pill_w, canvas_h - inset - pill_h, pill_w, pill_h};
    layout.list_max_height = std::max(0.f, layout.pill.y - gap - inset);
    layout.dock_right      = canvas_w - inset;
    layout.dock_bottom     = layout.pill.y - gap;
    if (list_height > 0) {
        const float height = std::min(list_height, layout.list_max_height);
        layout.list        = {canvas_w - inset - list_w, layout.pill.y - gap - height, list_w, height};
        layout.dock_bottom = layout.list.y - gap;
    }
    return layout;
}

const char* side_name(BubbleSide side)
{
    switch (side) {
    case BubbleSide::Above: return "above";
    case BubbleSide::Right: return "right";
    case BubbleSide::Left:  return "left";
    case BubbleSide::Below: return "below";
    case BubbleSide::Docked: break;
    }
    return "docked";
}

BubblePlacement place_bubble(const BubbleRequest& r)
{
    BubblePlacement placement;
    const auto clamp_x = [&r](float x) { return std::clamp(x, r.inset, std::max(r.inset, r.canvas_w - r.inset - r.bubble_w)); };
    const auto clamp_y = [&r](float y) { return std::clamp(y, r.inset, std::max(r.inset, r.canvas_h - r.inset - r.bubble_h)); };

    if (r.has_target && !r.target.empty()) {
        const OverlayRect& t = r.target;
        const float mid_x = t.x + t.w / 2, mid_y = t.y + t.h / 2;
        struct Candidate { BubbleSide side; float x, y; bool fits; };
        const Candidate candidates[] = {
            {BubbleSide::Above, clamp_x(r.anchor_x - r.bubble_w / 2), t.y - r.leader - r.bubble_h, t.y - r.leader - r.bubble_h >= r.inset},
            {BubbleSide::Right, t.x + t.w + r.leader, clamp_y(mid_y - r.bubble_h / 2), t.x + t.w + r.leader + r.bubble_w <= r.canvas_w - r.inset},
            {BubbleSide::Left, t.x - r.leader - r.bubble_w, clamp_y(mid_y - r.bubble_h / 2), t.x - r.leader - r.bubble_w >= r.inset},
            {BubbleSide::Below, clamp_x(mid_x - r.bubble_w / 2), t.y + t.h + r.leader, t.y + t.h + r.leader + r.bubble_h <= r.canvas_h - r.inset},
        };
        for (const Candidate& candidate : candidates) {
            const OverlayRect rect{candidate.x, candidate.y, r.bubble_w, r.bubble_h};
            if (!candidate.fits || overlap(rect, t) ||
                std::any_of(r.taken.begin(), r.taken.end(), [&rect](const OverlayRect& other) { return overlap(rect, other); }))
                continue;
            placement.rect = rect;
            placement.side = candidate.side;
            const float foot_x = std::clamp(candidate.side == BubbleSide::Above ? r.anchor_x : mid_x, rect.x + r.pad, rect.x + rect.w - r.pad);
            const float foot_y = std::clamp(mid_y, rect.y + r.pad, rect.y + rect.h - r.pad);
            switch (candidate.side) {
            case BubbleSide::Above:
                placement.leader_from_x = foot_x;      placement.leader_from_y = rect.y + rect.h;
                placement.leader_to_x   = r.anchor_x;  placement.leader_to_y   = std::max(r.anchor_y, rect.y + rect.h);
                break;
            case BubbleSide::Below:
                placement.leader_from_x = foot_x;      placement.leader_from_y = rect.y;
                placement.leader_to_x   = mid_x;       placement.leader_to_y   = t.y + t.h;
                break;
            case BubbleSide::Right:
                placement.leader_from_x = rect.x;      placement.leader_from_y = foot_y;
                placement.leader_to_x   = t.x + t.w;   placement.leader_to_y   = mid_y;
                break;
            case BubbleSide::Left:
                placement.leader_from_x = rect.x + rect.w; placement.leader_from_y = foot_y;
                placement.leader_to_x   = t.x;             placement.leader_to_y   = mid_y;
                break;
            case BubbleSide::Docked: break;
            }
            return placement;
        }
    }

    placement.side = BubbleSide::Docked;
    placement.rect = {std::max(r.inset, r.dock_right - r.bubble_w), std::max(r.inset, r.dock_bottom - r.bubble_h), r.bubble_w, r.bubble_h};
    return placement;
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
