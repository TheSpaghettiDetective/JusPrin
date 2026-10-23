#include <catch2/catch_all.hpp>
#include "slic3r/GUI/JusPrin/Shell/AutoSlicePolicy.hpp"

using namespace Slic3r::GUI::JusPrin;

TEST_CASE("The shell slices on its own only when nothing is in the way", "[auto_slice]")
{
    for (int bits = 0; bits < 64; ++bits) {
        AutoSliceFacts facts;
        facts.allowed   = bits & 1;
        facts.busy      = bits & 2;
        facts.sliceable = bits & 4;
        facts.sliced    = bits & 8;
        facts.tried     = bits & 16;
        facts.quiet     = bits & 32 ? kAutoSliceQuietPeriod : kAutoSliceQuietPeriod - std::chrono::milliseconds(1);
        CAPTURE(bits);
        const bool clear = facts.allowed && !facts.busy && facts.sliceable && !facts.sliced && !facts.tried;
        CHECK(should_auto_slice(facts) == (clear && (bits & 32) != 0));
    }
}

TEST_CASE("An Agent turn holds the slice however long the project has been quiet", "[auto_slice]")
{
    AutoSliceFacts facts;
    facts.sliceable = true;
    facts.quiet     = std::chrono::hours(1);
    CHECK_FALSE(should_auto_slice(facts));
    facts.allowed = true;
    CHECK(should_auto_slice(facts));
}

TEST_CASE("A cancelled or failed slice is not retried until the project changes", "[auto_slice]")
{
    AutoSliceFacts facts;
    facts.allowed   = true;
    facts.sliceable = true;
    facts.tried     = true;
    facts.quiet     = std::chrono::hours(1);
    CHECK_FALSE(should_auto_slice(facts));
}
