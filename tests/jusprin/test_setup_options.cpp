// What the printer form offers for a model and nozzle, read from the packaged
// profiles as OrcaSlicer loads them. These run against the real profiles, but
// each expectation is read from those profiles too (a second PresetBundle
// loaded the same way), so a profile update upstream does not fail them: they
// check the rules the loader applies, not which presets ship today.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrinterSetup/SetupOptionsLoader.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

using namespace Slic3r::GUI::JusPrin::PrinterSetup;

namespace {

const std::string kProfiles = std::string(JUSPRIN_SOURCE_DIR) + "/resources/profiles";

bool has_label(const std::vector<FilamentOption>& list, const std::string& label)
{
    return std::any_of(list.begin(), list.end(), [&label](const FilamentOption& option) { return option.label == label; });
}

bool has_label(const SetupOptions& options, const std::string& label) { return has_label(options.filaments, label); }

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

std::vector<std::string> plate_labels(const SetupOptions& options)
{
    std::vector<std::string> labels;
    for (const PlateOption& plate : options.plates)
        labels.push_back(plate.label);
    return labels;
}

std::string first_value(const Slic3r::Preset& preset, const char* key)
{
    const auto* values = preset.config.option<Slic3r::ConfigOptionStrings>(key);
    return values != nullptr && !values->values.empty() ? values->values.front() : std::string();
}

// The printers a filament's profile names; empty when it names none.
std::vector<std::string> named_printers(const Slic3r::Preset& filament)
{
    const auto* values = filament.config.option<Slic3r::ConfigOptionStrings>("compatible_printers");
    return values != nullptr ? values->values : std::vector<std::string>();
}

// The packaged profiles of one vendor, read with OrcaSlicer's own loader the
// way load_setup_options() reads them, so the tests take what the profiles
// say from the profiles.
struct Profiles
{
    Profiles(const std::string& vendor, const std::string& printer_name)
    {
        bundle.load_system_vendor_from_dir(kProfiles, vendor, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
        printer = bundle.printers.find_preset(printer_name, false);
        REQUIRE(printer != nullptr);
        REQUIRE(printer->name == printer_name);
        model = Slic3r::PresetUtils::system_printer_model(*printer);
        REQUIRE(model != nullptr);
    }

    // The name OrcaSlicer's lists show for the filament preset a profile
    // names: its alias, if it has one.
    std::string label_of(const std::string& name) const
    {
        const Slic3r::Preset* preset = bundle.filaments.find_preset(name, false);
        if (preset == nullptr)
            return {};
        return preset->alias.empty() ? preset->name : preset->alias;
    }

    // Whether the filament's profile names printers, none of them this one.
    bool made_for_others_only(const Slic3r::Preset& filament) const
    {
        const std::vector<std::string> printers = named_printers(filament);
        return !printers.empty() && std::find(printers.begin(), printers.end(), printer->name) == printers.end();
    }

    // A printer a filament's profile names, as "Made for ..." shows it: its
    // model's name where the profiles know the printer.
    std::string shown_printer(const std::string& name) const
    {
        const Slic3r::Preset* named = bundle.printers.find_preset(name, false);
        if (named != nullptr && named->name == name)
            if (const Slic3r::VendorProfile::PrinterModel* named_model = Slic3r::PresetUtils::system_printer_model(*named))
                return named_model->name;
        return name;
    }

    Slic3r::PresetBundle                       bundle;
    const Slic3r::Preset*                      printer{nullptr};
    const Slic3r::VendorProfile::PrinterModel* model{nullptr};
};

// The plates OrcaSlicer defines, in BedType order after Default, less the
// ones `model` names in its not_support_bed_type.
std::vector<std::string> plates_for(const Slic3r::VendorProfile::PrinterModel& model)
{
    std::vector<std::string> labels;
    for (const std::string& label : Slic3r::print_config_def.get("curr_bed_type")->enum_labels)
        if (std::find(model.not_support_bed_types.begin(), model.not_support_bed_types.end(), label) ==
            model.not_support_bed_types.end())
            labels.push_back(label);
    return labels;
}

// Each plate's type is OrcaSlicer's BedType for that plate, as the bed-type
// option maps its label to a value.
void check_plate_types(const SetupOptions& options)
{
    const Slic3r::ConfigOptionDef* definition = Slic3r::print_config_def.get("curr_bed_type");
    for (const PlateOption& plate : options.plates) {
        INFO(plate.label);
        const auto label = std::find(definition->enum_labels.begin(), definition->enum_labels.end(), plate.label);
        REQUIRE(label != definition->enum_labels.end());
        const std::string& key = definition->enum_values[size_t(label - definition->enum_labels.begin())];
        CHECK(plate.type == definition->enum_keys_map->at(key));
        CHECK(plate.type > int(Slic3r::btDefault));
        CHECK(plate.type < int(Slic3r::btCount));
    }
}

} // namespace

TEST_CASE("a printer OrcaSlicer offers no plate choice for gets none", "[setup-options]")
{
    const auto          started = std::chrono::steady_clock::now();
    const SetupOptions options = load_setup_options(kProfiles, "Prusa", "Prusa MK4 0.4 nozzle");
    INFO("loaded in " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
                      << " ms");
    REQUIRE(options.error.empty());
    const Profiles profiles("Prusa", "Prusa MK4 0.4 nozzle");
    // The fixture: not a Bambu Lab printer, and its profile does not turn on
    // support_multi_bed_types.
    const auto* multi = profiles.printer->config.option<Slic3r::ConfigOptionBool>("support_multi_bed_types");
    REQUIRE_FALSE((multi != nullptr && multi->value));
    CHECK(options.plates.empty());

    // The profile's own default, as the compatible preset of the name it gives.
    const std::string named_default = first_value(*profiles.printer, "default_filament_profile");
    REQUIRE_FALSE(named_default.empty());
    REQUIRE(is_compatible(options, options.default_filament));
    CHECK(profiles.label_of(options.default_filament) == profiles.label_of(named_default));
    // The default leads the suggestions, once.
    REQUIRE_FALSE(options.suggested.empty());
    CHECK(options.suggested.front() == options.default_filament);
    CHECK(std::count(options.suggested.begin(), options.suggested.end(), options.default_filament) == 1);
    {
        // The fixture: the model's own list starts with another filament, so
        // the check above sees the default moved to the front.
        INFO("the MK4's default_materials now starts with its default filament; pick a printer whose list does not");
        REQUIRE_FALSE(profiles.model->default_materials.empty());
        CHECK(profiles.label_of(profiles.model->default_materials.front()) != profiles.label_of(named_default));
    }
    // The rest are the model's wizard ticks that fit the printer.
    for (const std::string& name : options.suggested)
        CHECK(is_compatible(options, name));
    for (const std::string& material : profiles.model->default_materials) {
        INFO(material);
        const std::string label = profiles.label_of(material);
        if (!label.empty() && has_label(options, label))
            CHECK(std::any_of(options.suggested.begin(), options.suggested.end(),
                              [&](const std::string& name) { return profiles.label_of(name) == label; }));
    }

    // OrcaSlicer's compatibility rule, read off the profiles: a filament whose
    // profile names printers fits only those, and a library filament that
    // names none, sets no condition and is not excluded for this printer
    // fits every printer.
    size_t library = 0;
    size_t elsewhere = 0;
    for (const Slic3r::Preset& filament : profiles.bundle.filaments) {
        if (filament.is_default || !filament.is_system)
            continue;
        INFO(filament.name);
        if (profiles.made_for_others_only(filament)) {
            ++elsewhere;
            CHECK_FALSE(is_compatible(options, filament.name));
        } else if (named_printers(filament).empty() && filament.compatible_printers_condition().empty() &&
                   filament.vendor != nullptr && filament.vendor->name == Slic3r::PresetBundle::ORCA_FILAMENT_LIBRARY &&
                   filament.m_excluded_from.count(profiles.printer->name) == 0 &&
                   filament.m_excluded_from.count(profiles.printer->inherits()) == 0) {
            ++library;
            CHECK(has_label(options, filament.alias.empty() ? filament.name : filament.alias));
        }
    }
    // The fixture holds both kinds, so both rules were checked.
    CHECK(library > 0);
    CHECK(elsewhere > 0);
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
    const Profiles p1s_profiles("BBL", "Bambu Lab P1S 0.4 nozzle");
    CHECK(plate_labels(p1s) == plates_for(*p1s_profiles.model));
    check_plate_types(p1s);
    const std::vector<int> p1s_types = plate_types(p1s);
    CHECK(std::find(p1s_types.begin(), p1s_types.end(), p1s.default_plate) != p1s_types.end());
    // Bambu Lab's filaments made for this printer are offered for it.
    size_t made_for_p1s = 0;
    for (const Slic3r::Preset& filament : p1s_profiles.bundle.filaments) {
        const std::vector<std::string> printers = named_printers(filament);
        if (filament.is_default || !filament.is_system ||
            std::find(printers.begin(), printers.end(), p1s_profiles.printer->name) == printers.end())
            continue;
        INFO(filament.name);
        ++made_for_p1s;
        CHECK(has_label(p1s, filament.alias.empty() ? filament.name : filament.alias));
    }
    CHECK(made_for_p1s > 0);
    CHECK(is_compatible(p1s, p1s.default_filament));

    const SetupOptions h2d = load_setup_options(kProfiles, "BBL", "Bambu Lab H2D 0.4 nozzle");
    REQUIRE(h2d.error.empty());
    const Profiles h2d_profiles("BBL", "Bambu Lab H2D 0.4 nozzle");
    // The fixture: the H2D's model excludes some plates.
    REQUIRE_FALSE(h2d_profiles.model->not_support_bed_types.empty());
    CHECK(plate_labels(h2d) == plates_for(*h2d_profiles.model));
    check_plate_types(h2d);
    for (const std::string& excluded : h2d_profiles.model->not_support_bed_types) {
        const std::vector<std::string> labels = plate_labels(h2d);
        CHECK(std::find(labels.begin(), labels.end(), excluded) == labels.end());
    }
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
    const Profiles profiles("Prusa", "Prusa MK4 0.4 nozzle");
    // Every filament whose profile names only other printers is offered: as
    // fitting, when another preset of its name fits, otherwise apart.
    size_t elsewhere = 0;
    for (const Slic3r::Preset& filament : profiles.bundle.filaments) {
        if (filament.is_default || !filament.is_system || !profiles.made_for_others_only(filament))
            continue;
        INFO(filament.name);
        ++elsewhere;
        const std::string label = filament.alias.empty() ? filament.name : filament.alias;
        CHECK((has_label(options.filaments, label) || has_label(options.others, label)));
    }
    CHECK(elsewhere > 0);
    // Each one apart says which printers its profile names, the first of them
    // by its model's name.
    REQUIRE_FALSE(options.others.empty());
    for (const FilamentOption& other : options.others) {
        INFO(other.name);
        const Slic3r::Preset* filament = profiles.bundle.filaments.find_preset(other.name, false);
        REQUIRE(filament != nullptr);
        const std::vector<std::string> printers = named_printers(*filament);
        if (printers.empty())
            continue;
        CHECK_THAT(other.made_for, Catch::Matchers::StartsWith("the "));
        CHECK_THAT(other.made_for, Catch::Matchers::ContainsSubstring(profiles.shown_printer(printers.front())));
    }
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
    const FilamentOption* anywhere = find_label(options.own, "Any printer PLA");
    REQUIRE(anywhere != nullptr);
    CHECK(anywhere->made_for.empty());
}
