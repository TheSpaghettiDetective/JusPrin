// What the printer form offers for a model and nozzle, read from the packaged
// profiles as OrcaSlicer loads them: these run against the real profiles, so
// they pin what a person is shown for printers they own.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrinterSetup/SetupOptionsLoader.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

using namespace Slic3r::GUI::JusPrin::PrinterSetup;

namespace {

const std::string kProfiles = std::string(JUSPRIN_SOURCE_DIR) + "/resources/profiles";

bool has_label(const SetupOptions& options, const std::string& label)
{
    return std::any_of(options.filaments.begin(), options.filaments.end(),
                       [&label](const FilamentOption& option) { return option.label == label; });
}

bool is_compatible(const SetupOptions& options, const std::string& name)
{
    return std::any_of(options.filaments.begin(), options.filaments.end(),
                       [&name](const FilamentOption& option) { return option.name == name; });
}

std::vector<int> plate_types(const SetupOptions& options)
{
    std::vector<int> types;
    for (const PlateOption& plate : options.plates)
        types.push_back(plate.type);
    return types;
}

} // namespace

TEST_CASE("a printer OrcaSlicer offers no plate choice for gets none", "[setup-options]")
{
    const auto          started = std::chrono::steady_clock::now();
    const SetupOptions options = load_setup_options(kProfiles, "Prusa", "Prusa MK4 0.4 nozzle");
    INFO("loaded in " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
                      << " ms");
    REQUIRE(options.error.empty());
    CHECK(options.plates.empty());
    // The profile's own default, and the model's wizard ticks that fit it.
    CHECK(options.default_filament == "Prusa Generic PLA @MK4");
    // The default leads the suggestions, though the model's list starts elsewhere.
    REQUIRE_FALSE(options.suggested.empty());
    CHECK(options.suggested.front() == "Prusa Generic PLA @MK4");
    CHECK(std::count(options.suggested.begin(), options.suggested.end(), "Prusa Generic PLA @MK4") == 1);
    for (const std::string& name : options.suggested)
        CHECK(is_compatible(options, name));
    // The library fits every printer; Prusament is made for the CORE One and
    // XL only, so it is not offered here.
    CHECK(has_label(options, "eSUN PLA+"));
    CHECK_FALSE(has_label(options, "Prusament PLA"));
    // One preset per name, as OrcaSlicer's list shows them.
    std::vector<std::string> labels;
    for (const FilamentOption& option : options.filaments)
        labels.push_back(option.label);
    std::sort(labels.begin(), labels.end());
    CHECK(std::adjacent_find(labels.begin(), labels.end()) == labels.end());
}

TEST_CASE("a Bambu Lab printer offers OrcaSlicer's plates, less the ones its model excludes", "[setup-options]")
{
    const SetupOptions p1s = load_setup_options(kProfiles, "BBL", "Bambu Lab P1S 0.4 nozzle");
    REQUIRE(p1s.error.empty());
    CHECK(plate_types(p1s) == std::vector<int>{1, 2, 3, 4, 5, 6});
    CHECK(p1s.plates[3].label == "Textured PEI Plate");
    CHECK(std::find(plate_types(p1s).begin(), plate_types(p1s).end(), p1s.default_plate) != plate_types(p1s).end());
    CHECK(has_label(p1s, "Bambu PLA Basic"));
    CHECK(is_compatible(p1s, p1s.default_filament));

    // "not_support_bed_type": "Smooth Cool Plate;Textured Cool Plate"
    const SetupOptions h2d = load_setup_options(kProfiles, "BBL", "Bambu Lab H2D 0.4 nozzle");
    REQUIRE(h2d.error.empty());
    CHECK(plate_types(h2d) == std::vector<int>{2, 3, 4, 6});
}

TEST_CASE("a printer whose profile turns on several plate types offers them", "[setup-options]")
{
    const SetupOptions options = load_setup_options(kProfiles, "Creality", "Creality Ender-3 V3 0.4 nozzle");
    REQUIRE(options.error.empty());
    CHECK_FALSE(options.plates.empty());
}

TEST_CASE("a folder without the profiles is reported, and an unknown profile is a bug", "[setup-options]")
{
    const SetupOptions missing = load_setup_options(kProfiles + "/nowhere", "Prusa", "Prusa MK4 0.4 nozzle");
    CHECK_FALSE(missing.error.empty());
    CHECK(missing.filaments.empty());
    CHECK_THROWS_AS(load_setup_options(kProfiles, "Prusa", "Prusa MK99 0.4 nozzle"), std::logic_error);
}

namespace {

// A preset the person saved, made for `printers` (none: for any printer).
Slic3r::Preset own_preset(const std::string& name, std::vector<std::string> printers, const std::string& parent = {})
{
    Slic3r::Preset preset(Slic3r::Preset::TYPE_FILAMENT, name);
    preset.config = Slic3r::DynamicPrintConfig::full_print_config();
    preset.config.set_key_value("compatible_printers", new Slic3r::ConfigOptionStrings(std::move(printers)));
    preset.config.set_key_value("filament_type", new Slic3r::ConfigOptionStrings({"PETG"}));
    preset.config.set_key_value("inherits", new Slic3r::ConfigOptionString(parent));
    return preset;
}

const FilamentOption* find_label(const std::vector<FilamentOption>& list, const std::string& label)
{
    const auto found = std::find_if(list.begin(), list.end(), [&label](const FilamentOption& option) { return option.label == label; });
    return found == list.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("filaments made for other printers are offered apart, saying which printers", "[setup-options]")
{
    const SetupOptions options = load_setup_options(kProfiles, "Prusa", "Prusa MK4 0.4 nozzle");
    REQUIRE(options.error.empty());
    // Prusament profiles name only the CORE One and the XL.
    const FilamentOption* prusament = find_label(options.others, "Prusament PLA");
    REQUIRE(prusament != nullptr);
    CHECK_THAT(prusament->made_for, Catch::Matchers::StartsWith("the "));
    CHECK_THAT(prusament->made_for, Catch::Matchers::ContainsSubstring("CORE One"));
    // A name that fits is never offered again among the ones that do not.
    for (const FilamentOption& other : options.others) {
        CHECK_FALSE(other.made_for.empty());
        CHECK(find_label(options.filaments, other.label) == nullptr);
    }
}

TEST_CASE("the person's own presets are tested against the printer the form would add", "[setup-options]")
{
    const std::vector<Slic3r::Preset> own{own_preset("PETG for the MK4", {"Prusa MK4 0.4 nozzle"}, "Prusa Generic PETG @MK4"),
                                          own_preset("Matte PLA tuned", {"Bambu Lab X1 Carbon"}),
                                          own_preset("Any printer PLA", {})};
    const SetupOptions options = load_setup_options(kProfiles, "Prusa", "Prusa MK4 0.4 nozzle", own);
    REQUIRE(options.error.empty());
    REQUIRE(options.own.size() == 3);
    const FilamentOption* fits = find_label(options.own, "PETG for the MK4");
    REQUIRE(fits != nullptr);
    CHECK(fits->made_for.empty());
    CHECK_THAT(fits->base, Catch::Matchers::StartsWith("Prusa Generic PETG"));
    const FilamentOption* elsewhere = find_label(options.own, "Matte PLA tuned");
    REQUIRE(elsewhere != nullptr);
    // A printer the profiles do not hold keeps the name the preset gives it.
    CHECK(elsewhere->made_for == "the Bambu Lab X1 Carbon");
    CHECK(find_label(options.own, "Any printer PLA")->made_for.empty());
}
