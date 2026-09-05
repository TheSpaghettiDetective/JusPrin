#include <catch2/catch_all.hpp>
#include "slic3r/GUI/JusPrin/Shell/PrimaryPrintAction.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceReview.hpp"
#include <algorithm>

using namespace Slic3r::GUI::JusPrin;

TEST_CASE("Header offers only the next valid manufacturing action", "[header]")
{
    for (int bits = 0; bits < 256; ++bits) {
        PrintActionState state;
        state.slicing = bits & 1;
        state.sliced = bits & 2;
        state.can_slice = state.can_slice_all = bits & 4;
        state.needs_review = bits & 8;
        state.can_print = bits & 16;
        state.can_print_all = bits & 32;
        state.can_export = bits & 64;
        state.plate_count = bits & 128 ? 2 : 1;
        const auto actions = primary_print_action(state);
        CAPTURE(bits);
        const auto offers = [&](PrintAction action) {
            return (actions.primary.action == action && actions.primary.enabled) ||
                std::any_of(actions.menu.begin(), actions.menu.end(), [action](const auto& item) { return item.action == action && item.enabled; });
        };
        CHECK_FALSE((offers(PrintAction::Slice) && offers(PrintAction::Print)));
        if (state.slicing) {
            CHECK(actions.primary.action == PrintAction::Cancel);
            CHECK_FALSE(actions.primary.enabled);
            REQUIRE(actions.menu.size() == 1);
            CHECK(offers(PrintAction::Cancel));
        } else if (!state.sliced) {
            CHECK(actions.primary.action == PrintAction::Slice);
            CHECK_FALSE(offers(PrintAction::Print));
            CHECK_FALSE(offers(PrintAction::CheckPrint));
            CHECK(offers(PrintAction::SliceAll) == (state.plate_count > 1 && state.can_slice_all));
            // A single plate has nothing to put in the menu, so the header hides its chevron half.
            CHECK(actions.menu.empty() == (state.plate_count == 1));
        } else {
            CHECK_FALSE(actions.menu.empty());
            CHECK(actions.primary.action == (state.needs_review ? PrintAction::CheckPrint : PrintAction::Print));
            CHECK_FALSE(offers(PrintAction::Slice));
            CHECK_FALSE(offers(PrintAction::SliceAll));
            CHECK(offers(PrintAction::PrintAll) == (state.plate_count > 1 && state.can_print_all));
            CHECK(offers(PrintAction::Export) == state.can_export);
            CHECK(offers(PrintAction::CheckPrint));
        }
    }
}

TEST_CASE("Preview navigation does not replace Print", "[header]")
{
    PrintActionState state;
    state.sliced = state.can_print = state.preview = true;
    const auto actions = primary_print_action(state);
    CHECK(actions.primary.action == PrintAction::Print);
    CHECK(std::none_of(actions.menu.begin(),actions.menu.end(),[](const auto& item) { return item.action == PrintAction::Prepare; }));
}

TEST_CASE("Slice all can start from an empty active plate", "[header]")
{
    PrintActionState state;
    state.plate_count = 2;
    state.can_slice_all = true;
    const auto actions = primary_print_action(state);
    CHECK_FALSE(actions.primary.enabled);
    REQUIRE(actions.menu.size() == 1);
    CHECK(actions.menu.front().action == PrintAction::SliceAll);
    CHECK(actions.menu.front().enabled);
}

TEST_CASE("Slice review belongs to the exact plate result and session", "[header][review]")
{
    Workspace::SliceReviews reviews;
    const Workspace::SliceIdentity first{1, 10, 100}, second{1, 20, 200}, resliced{1, 10, 101}, reopened{2, 10, 100};
    int notifications = 0;
    reviews.set_listener([&] { ++notifications; });
    REQUIRE(reviews.report(first, first, {"Check opening"}));
    CHECK(reviews.needs_review(first));
    CHECK_FALSE(reviews.needs_review(second));
    CHECK_FALSE(reviews.needs_review(resliced));
    CHECK_FALSE(reviews.needs_review(reopened));
    CHECK_FALSE(reviews.report(first, resliced, {"Stale"}));
    CHECK_FALSE(reviews.report(first, {}, {"Invalidated"}));
    CHECK_FALSE(reviews.report(first, reopened, {"Old project"}));
    CHECK(notifications == 1);
    reviews.acknowledge(second);
    CHECK(reviews.needs_review(first));
    reviews.acknowledge(first);
    CHECK_FALSE(reviews.needs_review(first));
    REQUIRE(reviews.report(first, first, {"Check opening"}));
    CHECK_FALSE(reviews.needs_review(first)); // identical late retry
    REQUIRE(reviews.report(first, first, {"Different concern"}));
    CHECK(reviews.needs_review(first));
    reviews.acknowledge_displayed(first,{"Check opening"});
    CHECK(reviews.needs_review(first));
    CHECK(reviews.findings(first) == std::vector<std::string>{"Different concern"});
    reviews.acknowledge_displayed(first,{"Different concern"});
    CHECK_FALSE(reviews.needs_review(first));
    REQUIRE(reviews.report(resliced, resliced, {}));
    CHECK_FALSE(reviews.needs_review(resliced));
    CHECK_FALSE(reviews.needs_review(first));
}
