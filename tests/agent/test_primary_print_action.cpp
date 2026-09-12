#include <catch2/catch_all.hpp>
#include "slic3r/GUI/JusPrin/Shell/PrimaryPrintAction.hpp"
#include <algorithm>

using namespace Slic3r::GUI::JusPrin;

TEST_CASE("Header offers only the next valid manufacturing action", "[header]")
{
    for (int bits = 0; bits < 128; ++bits) {
        PrintActionState state;
        state.slicing = bits & 1;
        state.sliced = bits & 2;
        state.can_slice = state.can_slice_all = bits & 4;
        state.can_print = bits & 8;
        state.can_print_all = bits & 16;
        state.can_export = bits & 32;
        state.plate_count = bits & 64 ? 2 : 1;
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
            CHECK(offers(PrintAction::SliceAll) == (state.plate_count > 1 && state.can_slice_all));
            // A single plate has nothing to put in the menu, so the header hides its chevron half.
            CHECK(actions.menu.empty() == (state.plate_count == 1));
        } else {
            CHECK_FALSE(actions.menu.empty());
            CHECK(actions.primary.action == PrintAction::Print);
            CHECK_FALSE(offers(PrintAction::Slice));
            CHECK_FALSE(offers(PrintAction::SliceAll));
            CHECK(offers(PrintAction::PrintAll) == (state.plate_count > 1 && state.can_print_all));
            CHECK(offers(PrintAction::Export) == state.can_export);
        }
    }
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
