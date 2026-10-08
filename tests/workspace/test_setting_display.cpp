#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Workspace/OrcaSettingDefinitions.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingDisplay.hpp"

#include <iostream>
#include <random>

using namespace Slic3r;
using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

SettingDefinition definition(std::string type, std::string unit = {})
{
    SettingDefinition result;
    result.type = std::move(type);
    result.unit = std::move(unit);
    return result;
}

struct RealSetting
{
    SettingsScope     scope;
    SettingDefinition definition;
    std::string       value;
};

// Every setting the three preset types hold, at the value OrcaSlicer gives it
// by default.
std::vector<RealSetting> real_settings()
{
    std::vector<RealSetting> result;
    for (const SettingsScope scope : {SettingsScope::Process, SettingsScope::Filament, SettingsScope::Printer})
        for (SettingDefinition& setting : scope_definitions(scope)) {
            const ConfigOption* value = print_config_def.get(setting.key)->default_value.get();
            REQUIRE(value != nullptr);
            result.push_back({scope, std::move(setting), value->serialize()});
        }
    return result;
}

} // namespace

TEST_CASE("A number carries its unit", "[setting_display]")
{
    CHECK(display_value(definition("float", "mm"), "0.28") == "0.28 mm");
    CHECK(display_value(definition("integer"), "3") == "3");
    CHECK(display_value(definition("float", "\xC2\xB0"), "45") == "45\xC2\xB0");
    CHECK(display_value(definition("percent", "%"), "15%") == "15%");
    // "mm or %" is the two forms a value may take, not a unit to print whole.
    CHECK(display_value(definition("float_or_percent", "mm or %"), "0.42") == "0.42 mm");
    CHECK(display_value(definition("float_or_percent", "mm or %"), "50%") == "50%");
}

TEST_CASE("A switch and a choice read as words", "[setting_display]")
{
    CHECK(display_value(definition("boolean"), "1") == "On");
    CHECK(display_value(definition("boolean"), "0") == "Off");
    SettingDefinition brim = definition("enum");
    brim.enum_values = {"auto_brim", "outer_only"};
    brim.enum_labels = {"Auto", "Outer brim only"};
    CHECK(display_value(brim, "outer_only") == "Outer brim only");
    // A value OrcaSlicer gave no label is still words, not a config token.
    CHECK(display_value(brim, "brim_ears") == "brim ears");
}

TEST_CASE("One value per filament is one fact when they agree", "[setting_display]")
{
    CHECK(display_value(definition("integer[]", "\xE2\x84\x83"), "220,220") == "220 \xE2\x84\x83");
    CHECK(display_value(definition("float[]", "mm"), "0.4,0.6") == "0.4 mm, 0.6 mm");
    CHECK(display_value(definition("float[]", "mm"), "nil") == "Not set");
    CHECK(display_value(definition("boolean[]"), "1,0") == "On, Off");
}

TEST_CASE("Long text is described, not quoted", "[setting_display]")
{
    CHECK(display_value(definition("string"), "") == "Empty");
    CHECK(display_value(definition("string"), "PLA") == "PLA");
    CHECK(display_value(definition("string"), "G28\\nG1 Z5 F3000\\n") == "2 lines");
    CHECK(display_value(definition("string"), std::string(40, 'x')) == "40 characters");
    CHECK(display_value(definition("string[]"), "PLA;\"PLA Silk\"") == "PLA, PLA Silk");
    CHECK(display_value(definition("string[]"), "\"G1 X0\\nG1 Y0\"") == "2 lines");
    CHECK(display_value(definition("point[]"), "0x0,235x0,235x235,0x235") == "4 points");
}

TEST_CASE("A change says the unit once", "[setting_display]")
{
    CHECK(display_change(definition("float", "mm"), "0", "8") == "0 \xE2\x86\x92 8 mm");
    CHECK(display_change(definition("percent", "%"), "15%", "30%") == "15% \xE2\x86\x92 30%");
    CHECK(display_change(definition("boolean"), "0", "1") == "Off \xE2\x86\x92 On");
    CHECK(display_change(definition("float_or_percent", "mm or %"), "0.42", "110%") == "0.42 mm \xE2\x86\x92 110%");
    CHECK(display_change(definition("float[]", "mm"), "0.4,0.6", "0.4,0.8") == "0.4 mm, 0.6 mm \xE2\x86\x92 0.4 mm, 0.8 mm");
}

// The rules above are written against types. This is what stops them from
// quietly covering only the settings someone thought of: every setting in
// every preset type has to come out as one short line.
TEST_CASE("Every OrcaSlicer setting reads as one short line", "[setting_display]")
{
    const std::vector<RealSetting> settings = real_settings();
    REQUIRE(settings.size() > 500);
    for (const RealSetting& setting : settings) {
        INFO(setting.definition.key << " = " << setting.value);
        const std::string shown = display_value(setting.definition, setting.value);
        CHECK_FALSE(shown.empty());
        CHECK(shown.find('\n') == std::string::npos);
        CHECK(shown.size() <= 48);
        // A change from a value to itself is still one line with both ends.
        const std::string change = display_change(setting.definition, setting.value, setting.value);
        CHECK(change.find(shown) != std::string::npos);
        CHECK(change.find(" \xE2\x86\x92 ") != std::string::npos);
        CHECK(change.find('\n') == std::string::npos);
    }
}

// A table to read, not a check: run with "[.sample]" and, to draw a different
// sample, --rng-seed.
TEST_CASE("Sample of displayed settings", "[.sample]")
{
    std::vector<RealSetting> settings = real_settings();
    std::mt19937 random(Catch::getSeed());
    std::shuffle(settings.begin(), settings.end(), random);
    const std::size_t count = (settings.size() * 5 + 99) / 100;
    std::cout << "seed " << Catch::getSeed() << ", " << count << " of " << settings.size() << " settings\n"
              << "| Preset | Key | Label | Category | Type | Raw value | Shown as |\n|---|---|---|---|---|---|---|\n";
    for (std::size_t index = 0; index < count; ++index) {
        const RealSetting& setting = settings[index];
        std::string raw = setting.value.size() > 40 ? setting.value.substr(0, 40) + "..." : setting.value;
        std::replace(raw.begin(), raw.end(), '|', '/');
        std::cout << "| " << scope_name(setting.scope) << " | " << setting.definition.key << " | " << setting.definition.label
                  << " | " << setting.definition.category << " | " << setting.definition.type << " | `" << raw << "` | "
                  << display_value(setting.definition, setting.value) << " |\n";
    }
}
