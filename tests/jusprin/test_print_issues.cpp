// The print-issue overlay's placement rules and the message a press of
// "Ask AI" or "Resolve with AI" sends, both free of wx, ImGui and OrcaSlicer.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrintIssues/IssueContext.hpp"
#include "slic3r/GUI/JusPrin/PrintIssues/IssueOverlayLayout.hpp"

#include <nlohmann/json.hpp>

using namespace Slic3r::GUI::JusPrin::PrintIssues;
using nlohmann::json;

namespace {

// A 1000 x 800 canvas with a 280 x 120 bubble, the sizes close to the app's.
BubbleRequest request_for(const OverlayRect& target)
{
    BubbleRequest request;
    request.canvas_w    = 1000;
    request.canvas_h    = 800;
    request.inset       = 12;
    request.leader      = 16;
    request.pad         = 12;
    request.bubble_w    = 280;
    request.bubble_h    = 120;
    request.target      = target;
    request.anchor_x    = target.x + target.w / 2;
    request.anchor_y    = target.y + 4;
    request.has_target  = true;
    request.dock_right  = 988;
    request.dock_bottom = 740;
    return request;
}

bool inside_canvas(const BubbleRequest& request, const OverlayRect& rect)
{
    return rect.x >= request.inset && rect.y >= request.inset && rect.x + rect.w <= request.canvas_w - request.inset &&
           rect.y + rect.h <= request.canvas_h - request.inset;
}

PrintIssue overlap_warning()
{
    PrintIssue issue;
    issue.id          = "validation:warning::158:167";
    issue.source      = IssueSource::Validation;
    issue.severity    = IssueSeverity::Warning;
    issue.code        = "object_collision_in_layer_print";
    issue.message     = "Cube is too close to others, and collisions may be caused.\n";
    issue.plate       = 114;
    issue.object      = 158;
    issue.instance    = 167;
    issue.object_name = "Cube";
    return issue;
}

PrintIssueSnapshot snapshot_with(const PrintIssue& issue)
{
    PrintIssueSnapshot snapshot;
    snapshot.session    = 4;
    snapshot.generation = 8;
    snapshot.plate      = issue.plate;
    snapshot.checked    = true;
    snapshot.issues     = {issue};
    return snapshot;
}

IssueMessageRequest message_request(IssueIntent intent)
{
    IssueMessageRequest request;
    request.intent             = intent;
    request.request_sentence   = intent == IssueIntent::Explain ? "Explain this issue and my options." : "Help fix this issue.";
    request.workspace_session  = 4;
    request.workspace_revision = 29;
    request.conversation_id    = "c-1";
    return request;
}

} // namespace

TEST_CASE("the bubble sits above its object when there is room", "[print-issues][layout]")
{
    const BubbleRequest request = request_for({460, 400, 80, 80});
    const BubblePlacement placement = place_bubble(request);
    CHECK(placement.side == BubbleSide::Above);
    CHECK_THAT(placement.rect.y + placement.rect.h + request.leader, Catch::Matchers::WithinAbs(request.target.y, 0.5));
    CHECK_FALSE(overlap(placement.rect, request.target));
    CHECK(inside_canvas(request, placement.rect));
    // The leader ends on the object's projected top, below the bubble.
    CHECK(placement.leader_to_x == request.anchor_x);
    CHECK(placement.leader_from_y == placement.rect.y + placement.rect.h);
}

TEST_CASE("an object near the top edge never ends up under its bubble", "[print-issues][layout]")
{
    // Kenneth's screenshot of 2026-10-09: the first version slid the bubble
    // down to fit and covered the object.
    const BubbleRequest request = request_for({460, 40, 60, 70});
    const BubblePlacement placement = place_bubble(request);
    CHECK(placement.side == BubbleSide::Right);
    CHECK_FALSE(overlap(placement.rect, request.target));
    CHECK(inside_canvas(request, placement.rect));
}

TEST_CASE("the bubble takes the next free side in order: above, right, left, below", "[print-issues][layout]")
{
    SECTION("no room above or to the right") {
        const BubbleRequest request = request_for({900, 40, 60, 70});
        const BubblePlacement placement = place_bubble(request);
        CHECK(placement.side == BubbleSide::Left);
        CHECK_FALSE(overlap(placement.rect, request.target));
        CHECK(inside_canvas(request, placement.rect));
    }
    SECTION("the sides are taken by other controls") {
        BubbleRequest request = request_for({460, 40, 60, 70});
        request.taken = {{540, 12, 448, 60},  // a pill across the upper right
                         {12, 12, 440, 60}};  // a tool strip across the upper left
        const BubblePlacement placement = place_bubble(request);
        CHECK(placement.side == BubbleSide::Below);
        CHECK_FALSE(overlap(placement.rect, request.target));
        for (const OverlayRect& taken : request.taken)
            CHECK_FALSE(overlap(placement.rect, taken));
        CHECK(placement.leader_to_y == request.target.y + request.target.h);
    }
    SECTION("a bubble above is moved sideways, not onto the object, to stay in the canvas") {
        const BubbleRequest request = request_for({20, 400, 60, 60});
        const BubblePlacement placement = place_bubble(request);
        CHECK(placement.side == BubbleSide::Above);
        CHECK(placement.rect.x == request.inset);
        CHECK_FALSE(overlap(placement.rect, request.target));
    }
}

TEST_CASE("with no side free, or no object, the bubble docks beside the status", "[print-issues][layout]")
{
    SECTION("the object fills the view") {
        const BubbleRequest request = request_for({20, 20, 960, 760});
        const BubblePlacement placement = place_bubble(request);
        CHECK(placement.side == BubbleSide::Docked);
        CHECK(placement.rect.x + placement.rect.w == request.dock_right);
        CHECK(placement.rect.y + placement.rect.h == request.dock_bottom);
    }
    SECTION("a plate-wide issue has no object to sit beside") {
        BubbleRequest request = request_for({});
        request.has_target = false;
        CHECK(place_bubble(request).side == BubbleSide::Docked);
    }
}

TEST_CASE("the status sits in the lower right corner and its list opens upward", "[print-issues][layout]")
{
    const StatusLayout closed = layout_status(1000, 800, 12, 8, 150, 26, 320, 0);
    CHECK(closed.pill.x + closed.pill.w == 988);
    CHECK(closed.pill.y + closed.pill.h == 788);
    CHECK(closed.list.empty());
    CHECK(closed.dock_bottom == closed.pill.y - 8);

    const StatusLayout open = layout_status(1000, 800, 12, 8, 150, 26, 320, 200);
    CHECK(open.list.x + open.list.w == 988);
    CHECK(open.list.y + open.list.h == open.pill.y - 8);
    CHECK(open.list.h == 200);
    // A docked bubble goes above the list, not over it.
    CHECK(open.dock_bottom == open.list.y - 8);

    // More rows than the canvas has room for: the list stops at the top
    // inset and scrolls; it never leaves the canvas.
    const StatusLayout tall = layout_status(1000, 800, 12, 8, 150, 26, 320, 5000);
    CHECK(tall.list.y == 12);
    CHECK(tall.list.h == tall.list_max_height);
}

TEST_CASE("an issue message says what was asked and carries the issue as data", "[print-issues][context]")
{
    const PrintIssue         issue    = overlap_warning();
    const PrintIssueSnapshot snapshot = snapshot_with(issue);

    const IssueMessage ask = make_issue_message(issue, snapshot, message_request(IssueIntent::Explain));
    // The person's request, then OrcaSlicer's own words, quoted and trimmed.
    CHECK(ask.text == "Explain this issue and my options.\n\n> Cube is too close to others, and collisions may be caused.");
    const json context = json::parse(ask.context_json);
    CHECK(context["kind"] == "print_issue");
    CHECK(context["intent"] == "explain");
    CHECK(context["issue"]["id"] == issue.id);
    CHECK(context["issue"]["source"] == "validation");
    CHECK(context["issue"]["severity"] == "warning");
    CHECK(context["issue"]["code"] == "object_collision_in_layer_print");
    CHECK(context["target"]["scope"] == "copy");
    CHECK(context["target"]["objectId"] == 158);
    CHECK(context["target"]["copyId"] == 167);
    CHECK(context["target"]["plateId"] == 114);
    CHECK(context["evidence"]["checked"] == true);
    CHECK(context["evidence"]["generation"] == 8);
    // The ids the tools take, so the model can act on the same workspace.
    CHECK(context["workspace"]["sessionId"] == "4");
    CHECK(context["workspace"]["revision"] == 29);

    const IssueMessage resolve = make_issue_message(issue, snapshot, message_request(IssueIntent::Resolve));
    CHECK(resolve.text.rfind("Help fix this issue.", 0) == 0);
    CHECK(json::parse(resolve.context_json)["intent"] == "resolve");
}

TEST_CASE("a plate-wide issue names no object and a several-line message stays one quotation", "[print-issues][context]")
{
    PrintIssue issue = overlap_warning();
    issue.object = issue.instance = 0;
    issue.object_name.clear();
    issue.setting = "spiral_mode";
    issue.message = "First line.\nSecond line.";
    const IssueMessage message = make_issue_message(issue, snapshot_with(issue), message_request(IssueIntent::Resolve));
    const json context = json::parse(message.context_json);
    CHECK(context["target"]["scope"] == "plate");
    CHECK_FALSE(context["target"].contains("objectId"));
    CHECK_FALSE(context["target"].contains("copyId"));
    CHECK(context["issue"]["setting"] == "spiral_mode");
    CHECK(message.text == "Help fix this issue.\n\n> First line.\n> Second line.");
}

TEST_CASE("a repeated press is one message until something changes", "[print-issues][context]")
{
    const PrintIssue   issue    = overlap_warning();
    PrintIssueSnapshot snapshot = snapshot_with(issue);
    const std::string  first    = make_issue_message(issue, snapshot, message_request(IssueIntent::Explain)).client_message_id;

    CHECK(make_issue_message(issue, snapshot, message_request(IssueIntent::Explain)).client_message_id == first);
    // The other button, new evidence, another chat or another project is a new message.
    CHECK(make_issue_message(issue, snapshot, message_request(IssueIntent::Resolve)).client_message_id != first);
    IssueMessageRequest other_chat = message_request(IssueIntent::Explain);
    other_chat.conversation_id = "c-2";
    CHECK(make_issue_message(issue, snapshot, other_chat).client_message_id != first);
    ++snapshot.generation;
    CHECK(make_issue_message(issue, snapshot, message_request(IssueIntent::Explain)).client_message_id != first);
    --snapshot.generation;
    ++snapshot.session;
    CHECK(make_issue_message(issue, snapshot, message_request(IssueIntent::Explain)).client_message_id != first);
}

TEST_CASE("snapshots compare by what a person would see", "[print-issues]")
{
    const PrintIssue   issue = overlap_warning();
    PrintIssueSnapshot a = snapshot_with(issue), b = snapshot_with(issue);
    b.generation = 99;
    CHECK(a.same_content(b));
    b.issues.front().stale = true;
    CHECK_FALSE(a.same_content(b));
    b = a;
    b.checked = false;
    CHECK_FALSE(a.same_content(b));
    CHECK(a.find(issue.id) != nullptr);
    CHECK(a.find("gone") == nullptr);
}
