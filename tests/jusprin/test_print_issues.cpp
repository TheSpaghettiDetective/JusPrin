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

namespace {

// Two notification kinds as OrcaSlicer numbers them; which is which does not
// matter to the rule, only that they differ.
constexpr int kValidateWarning = 9, kPlateWarning = 17, kExportFinished = 1;

Notice held_notice(int kind, const char* name, NoticeRoute route, NoticeLevel level, std::string body, bool several = false)
{
    Notice notice;
    notice.kind      = kind;
    notice.kind_name = name;
    notice.route     = route;
    notice.level     = level;
    notice.body      = body;
    notice.text      = (level == NoticeLevel::Error ? "Error:\n" : "Warning:\n") + body;
    notice.several   = several;
    return notice;
}

} // namespace

TEST_CASE("an object lookup's entry exists only while OrcaSlicer holds its notification", "[print-issues][notices]")
{
    PrintIssue found = overlap_warning();
    found.answers = kValidateWarning;

    SECTION("held: the entry with its object stands, and the notification adds nothing")
    {
        const auto issues = issues_from({held_notice(kValidateWarning, "ValidateWarning", NoticeRoute::Issue, NoticeLevel::Warning, found.message)},
                                        {found}, found.plate);
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().id == found.id);
        CHECK(issues.front().object == found.object);
    }
    SECTION("not held: OrcaSlicer no longer says it, so neither does the list")
    {
        CHECK(issues_from({}, {found}, found.plate).empty());
    }
}

TEST_CASE("a print-problem notification no lookup answers for is a plate-wide issue in OrcaSlicer's words", "[print-issues][notices]")
{
    const Notice flushing = held_notice(48, "BBLFlushingVolumeZero", NoticeRoute::Issue, NoticeLevel::Warning, "Partial flushing volume set to 0.");
    const Notice blocked  = held_notice(53, "BBLFilamentPrintableError", NoticeRoute::Issue, NoticeLevel::Error, "The left nozzle can not print TPU.");
    const auto issues = issues_from({flushing, blocked}, {}, 7);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].source == IssueSource::Notice);
    CHECK(issues[0].severity == IssueSeverity::Warning);
    CHECK(issues[0].message == "Partial flushing volume set to 0.");
    CHECK(issues[0].code == "BBLFlushingVolumeZero");
    CHECK(issues[0].id == "notice:BBLFlushingVolumeZero");
    CHECK(issues[0].object == 0);
    CHECK(issues[0].plate == 7);
    CHECK(issues[1].severity == IssueSeverity::Blocker);
}

TEST_CASE("several notifications of one kind are told apart by OrcaSlicer's own words", "[print-issues][notices]")
{
    const auto issues = issues_from({held_notice(kPlateWarning, "PlaterWarning", NoticeRoute::Issue, NoticeLevel::Warning, "The prime tower extends beyond the plate boundary.", true),
                                     held_notice(kPlateWarning, "PlaterWarning", NoticeRoute::Issue, NoticeLevel::Warning, "Mixing PVA with PETG in printing is not recommended.", true)},
                                    {}, 1);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].id != issues[1].id);
    CHECK(issues[0].id.rfind("notice:PlaterWarning:", 0) == 0);
}

TEST_CASE("what is read from the slice result or a failed slice needs no notification, and is not said twice", "[print-issues][notices]")
{
    PrintIssue outside;
    outside.id         = "slice_result:toolpath_outside_plate";
    outside.source     = IssueSource::SliceResult;
    outside.severity   = IssueSeverity::Blocker;
    outside.message    = "A G-code path goes beyond the plate boundaries.";
    outside.answers    = 10;
    outside.unprompted = true;

    SECTION("right after slicing OrcaSlicer holds no notification for it")
    {
        const auto issues = issues_from({}, {outside}, 1);
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().id == outside.id);
    }
    SECTION("once Check print has loaded the slice OrcaSlicer holds one, and the list still has one entry")
    {
        const auto issues = issues_from({held_notice(10, "SlicingError", NoticeRoute::Issue, NoticeLevel::Error, outside.message)}, {outside}, 1);
        REQUIRE(issues.size() == 1);
        CHECK(issues.front().source == IssueSource::SliceResult);
    }
}

TEST_CASE("a notification that is not a print problem never enters the issue list", "[print-issues][notices]")
{
    CHECK(issues_from({held_notice(kExportFinished, "ExportFinished", NoticeRoute::Result, NoticeLevel::Info, "Exported successfully"),
                       held_notice(0, "CustomNotification", NoticeRoute::Result, NoticeLevel::Error, "Download failed."),
                       held_notice(40, "ArrangeOngoing", NoticeRoute::Progress, NoticeLevel::Info, "Arranging..."),
                       held_notice(6, "PresetUpdateAvailable", NoticeRoute::App, NoticeLevel::Info, "Configuration can update now.")},
                      {}, 1).empty());
}

TEST_CASE("a snapshot differs when the confirmations differ", "[print-issues][notices]")
{
    PrintIssueSnapshot a = snapshot_with(overlap_warning()), b = a;
    b.notices.push_back(held_notice(kExportFinished, "ExportFinished", NoticeRoute::Result, NoticeLevel::Info, "Exported successfully"));
    CHECK_FALSE(a.same_content(b));
}

TEST_CASE("confirmations stack upward from the pill and a docked bubble starts above them", "[print-issues][layout]")
{
    StatusLayout status = layout_status(1000, 700, 12, 8, 160, 28, 320, 0);
    const float  dock   = status.dock_bottom;

    SECTION("newest nearest the pill")
    {
        const auto rects = stack_notices(status, 12, 8, 320, {40, 60});
        REQUIRE(rects.size() == 2);
        CHECK(rects[1].y + rects[1].h == status.pill.y - 8);
        CHECK(rects[0].y + rects[0].h == rects[1].y - 8);
        CHECK(rects[0].x + rects[0].w == status.dock_right);
        CHECK(status.dock_bottom == rects[0].y - 8);
        CHECK(status.dock_bottom < dock);
        CHECK_FALSE(overlap(rects[0], rects[1]));
        CHECK_FALSE(overlap(rects[1], status.pill));
    }
    SECTION("with no room the oldest is left out")
    {
        const auto rects = stack_notices(status, 12, 8, 320, {400, 400});
        CHECK(rects[0].empty());
        CHECK_FALSE(rects[1].empty());
    }
    SECTION("the open list has that place")
    {
        StatusLayout open = layout_status(1000, 700, 12, 8, 160, 28, 320, 200);
        const float  before = open.dock_bottom;
        const auto   rects  = stack_notices(open, 12, 8, 320, {40});
        CHECK(rects[0].empty());
        CHECK(open.dock_bottom == before);
    }
}

TEST_CASE("a hidden pill leaves its corner to the confirmations", "[print-issues][layout]")
{
    // No issue: the pill has no width.
    StatusLayout status = layout_status(1000, 700, 12, 8, 0, 0, 320, 0);
    CHECK(status.pill.empty());
    CHECK(status.dock_bottom == 700 - 12);
    const auto rects = stack_notices(status, 12, 8, 320, {40});
    REQUIRE(rects.size() == 1);
    CHECK(rects[0].y + rects[0].h == 700 - 12);
    CHECK(rects[0].x + rects[0].w == 1000 - 12);
}

namespace {

PrintIssue issue_with(const std::string& id, IssueSeverity severity)
{
    PrintIssue issue;
    issue.id       = id;
    issue.severity = severity;
    return issue;
}

} // namespace

TEST_CASE("an ignored warning stays in the list, marked", "[print-issues][ignore]")
{
    std::vector<PrintIssue> issues{issue_with("a", IssueSeverity::Warning), issue_with("b", IssueSeverity::Warning)};
    std::set<std::string>   ignored{"b"};
    mark_ignored(issues, ignored, true);
    REQUIRE(issues.size() == 2);
    CHECK_FALSE(issues[0].ignored);
    CHECK(issues[1].ignored);
    CHECK(ignored == std::set<std::string>{"b"});

    // The mark is part of what a person sees.
    PrintIssue unmarked = issues[1];
    unmarked.ignored = false;
    CHECK(unmarked != issues[1]);
}

TEST_CASE("a blocker cannot be ignored", "[print-issues][ignore]")
{
    // OrcaSlicer made the warning a blocker under the same id.
    std::vector<PrintIssue> issues{issue_with("a", IssueSeverity::Blocker)};
    std::set<std::string>   ignored{"a"};
    mark_ignored(issues, ignored, true);
    CHECK_FALSE(issues[0].ignored);
    CHECK(ignored.empty());
}

TEST_CASE("an ignore lasts while OrcaSlicer keeps reporting the warning", "[print-issues][ignore]")
{
    std::set<std::string> ignored{"a"};

    SECTION("an edit OrcaSlicer has not checked yet forgets nothing")
    {
        std::vector<PrintIssue> none;
        mark_ignored(none, ignored, false);
        CHECK(ignored == std::set<std::string>{"a"});
        std::vector<PrintIssue> back{issue_with("a", IssueSeverity::Warning)};
        mark_ignored(back, ignored, true);
        CHECK(back[0].ignored);
    }
    SECTION("a warning OrcaSlicer stopped reporting is shown again if it returns")
    {
        std::vector<PrintIssue> none;
        mark_ignored(none, ignored, true);
        CHECK(ignored.empty());
        std::vector<PrintIssue> back{issue_with("a", IssueSeverity::Warning)};
        mark_ignored(back, ignored, true);
        CHECK_FALSE(back[0].ignored);
    }
}

TEST_CASE("the chat is told when the issue asked about was ignored", "[print-issues][context][ignore]")
{
    PrintIssueSnapshot snapshot;
    snapshot.issues.push_back(issue_with("a", IssueSeverity::Warning));
    CHECK_FALSE(json::parse(make_issue_message(snapshot.issues[0], snapshot, {}).context_json)["issue"].contains("ignored"));
    snapshot.issues[0].ignored = true;
    CHECK(json::parse(make_issue_message(snapshot.issues[0], snapshot, {}).context_json)["issue"].value("ignored", false));
}
