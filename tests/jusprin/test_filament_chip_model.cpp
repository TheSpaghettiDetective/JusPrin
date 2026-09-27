#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/JusPrin/Shell/FilamentChipModel.hpp"

using namespace Slic3r::GUI::JusPrin;

namespace {

ChipSlot slot(const std::string& preset, const std::string& colour, bool used)
{
    return ChipSlot{preset, preset + " short", colour, used};
}

} // namespace

TEST_CASE("one slot names its filament and draws one full dot", "[filament_chip]")
{
    const auto model = describe_filament_chip({slot("Prusament PLA", "#F2754E", true)});
    CHECK(model.name == "Prusament PLA short");
    CHECK(model.filaments == 1);
    REQUIRE(model.dots.size() == 1);
    CHECK_FALSE(model.dots[0].faded);
    CHECK(model.dots[0].colour == "#F2754E");
    CHECK(model.folded == 0);
    CHECK_FALSE(model.shrunk);
}

TEST_CASE("slots the plate does not use keep their dot, faded", "[filament_chip]")
{
    const auto model = describe_filament_chip({slot("PLA", "#111111", true), slot("PLA", "#222222", false),
                                               slot("PLA", "#333333", false), slot("PETG", "#444444", false)});
    // One filament in use: the label names it, whatever the unused slots hold.
    CHECK(model.name == "PLA short");
    CHECK(model.filaments == 1);
    REQUIRE(model.dots.size() == 4);
    CHECK_FALSE(model.dots[0].faded);
    CHECK(model.dots[1].faded);
    CHECK(model.dots[3].faded);
}

TEST_CASE("used slots that share one filament name it; different ones count", "[filament_chip]")
{
    SECTION("three used, one filament")
    {
        const auto model = describe_filament_chip({slot("PLA", "#1", true), slot("PLA", "#2", true),
                                                   slot("PLA", "#3", true), slot("PETG", "#4", false)});
        CHECK(model.name == "PLA short");
        CHECK(model.filaments == 1);
    }
    SECTION("three used, three filaments")
    {
        const auto model = describe_filament_chip({slot("PLA", "#1", true), slot("PETG", "#2", true),
                                                   slot("ABS", "#3", true), slot("TPU", "#4", false)});
        CHECK(model.name.empty());
        CHECK(model.filaments == 3);
    }
    SECTION("two slots of one filament and one of another count filaments, not slots")
    {
        const auto model = describe_filament_chip({slot("PLA", "#1", true), slot("PLA", "#2", true),
                                                   slot("PETG", "#3", true)});
        CHECK(model.name.empty());
        CHECK(model.filaments == 2);
    }
}

TEST_CASE("an empty plate reads every slot as in use", "[filament_chip]")
{
    const auto model = describe_filament_chip({slot("PLA", "#1", false), slot("PETG", "#2", false)});
    CHECK(model.filaments == 2);
    REQUIRE(model.dots.size() == 2);
    CHECK_FALSE(model.dots[0].faded);
    CHECK_FALSE(model.dots[1].faded);
}

TEST_CASE("past eight slots unused ones fold into +n and used ones keep their dots", "[filament_chip]")
{
    std::vector<ChipSlot> slots;
    for (int i = 0; i < 12; ++i)
        slots.push_back(slot(i % 2 ? "PETG" : "PLA", "#" + std::to_string(i), i == 1 || i == 4 || i == 9));
    const auto model = describe_filament_chip(slots);
    CHECK(model.folded == 9);
    REQUIRE(model.dots.size() == 3);
    CHECK(model.dots[0].slot == 1);
    CHECK(model.dots[1].slot == 4);
    CHECK(model.dots[2].slot == 9);
    CHECK_FALSE(model.shrunk);
}

TEST_CASE("more than eight used slots all keep a dot, and the dots shrink", "[filament_chip]")
{
    std::vector<ChipSlot> slots;
    for (int i = 0; i < 12; ++i)
        slots.push_back(slot("PLA" + std::to_string(i), "#" + std::to_string(i), true));
    const auto model = describe_filament_chip(slots);
    CHECK(model.dots.size() == 12);
    CHECK(model.folded == 0);
    CHECK(model.shrunk);
    CHECK(model.filaments == 12);
}

TEST_CASE("exactly eight slots draw every dot at full size", "[filament_chip]")
{
    std::vector<ChipSlot> slots;
    for (int i = 0; i < 8; ++i)
        slots.push_back(slot("PLA", "#" + std::to_string(i), i == 0));
    const auto model = describe_filament_chip(slots);
    CHECK(model.dots.size() == 8);
    CHECK(model.folded == 0);
    CHECK_FALSE(model.shrunk);
}
