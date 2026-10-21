#include "SetupOptionsLoader.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/algorithm/string/case_conv.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

std::string first_of(const Preset& preset, const char* key)
{
    const auto* values = preset.config.option<ConfigOptionStrings>(key);
    return values != nullptr && !values->values.empty() ? values->values.front() : std::string();
}

// The plates OrcaSlicer's sidebar lists for this printer, in its order:
// every BedType after Default, less the model's not_support_bed_type, which
// names them by label (Sidebar::reset_bed_type_combox_choices). The sidebar
// shows the list only for a Bambu Lab printer or one whose profile turns on
// support_multi_bed_types (Sidebar::update_presets); for any other printer it
// offers no choice, and neither does the form.
std::vector<PlateOption> plates_of(PresetBundle& bundle, const Preset& printer)
{
    std::vector<PlateOption> plates;
    const auto* multi = printer.config.option<ConfigOptionBool>("support_multi_bed_types");
    if (!bundle.is_bbl_vendor() && !(multi != nullptr && multi->value))
        return plates;
    const ConfigOptionDef*              definition = print_config_def.get("curr_bed_type");
    const VendorProfile::PrinterModel* model      = PresetUtils::system_printer_model(printer);
    int                                 type       = 0;
    for (const std::string& label : definition->enum_labels) {
        ++type;
        if (model != nullptr &&
            std::find(model->not_support_bed_types.begin(), model->not_support_bed_types.end(), label) !=
                model->not_support_bed_types.end())
            continue;
        plates.push_back(PlateOption{type, label});
    }
    return plates;
}

// The printers a filament's profile names, as a phrase for "Made for ...":
// each by its model's name where the profiles know it, by the name the
// profile gives otherwise (a person's own printer, say).
std::string made_for(const PresetBundle& bundle, const Preset& filament)
{
    std::vector<std::string> printers;
    if (const auto* names = filament.config.option<ConfigOptionStrings>("compatible_printers"))
        for (const std::string& name : names->values) {
            std::string shown = name;
            if (const Preset* printer = bundle.printers.find_preset(name, false); printer != nullptr && printer->name == name)
                if (const VendorProfile::PrinterModel* model = PresetUtils::system_printer_model(*printer))
                    shown = model->name;
            if (std::find(printers.begin(), printers.end(), shown) == printers.end())
                printers.push_back(shown);
        }
    if (printers.empty())
        return "other printers";
    if (printers.size() == 1)
        return "the " + printers[0];
    if (printers.size() == 2)
        return "the " + printers[0] + " and " + printers[1];
    return "the " + printers[0] + ", " + printers[1] + " and " + std::to_string(printers.size() - 2) + " other printers";
}

} // namespace

SetupOptions load_setup_options(const std::string& profiles_directory, const std::string& vendor_id,
                                const std::string& printer_preset, const std::vector<Preset>& own_presets)
{
    SetupOptions options;
    PresetBundle bundle;
    try {
        bundle.load_system_vendor_from_dir(profiles_directory, vendor_id, ForwardCompatibilitySubstitutionRule::EnableSilent);
    } catch (const std::runtime_error& error) {
        options.error = std::string("The printer profiles could not be read: ") + error.what();
        return options;
    }

    const Preset* printer = bundle.printers.find_preset(printer_preset, false);
    if (printer == nullptr || printer->name != printer_preset)
        throw std::logic_error("The packaged profiles hold no printer profile \"" + printer_preset + "\"");
    // Selected as the sidebar's would be, so the vendor checks below read
    // this printer.
    bundle.printers.select_preset_by_name(printer_preset, true);
    Preset& selected = bundle.printers.get_edited_preset();

    options.plates        = plates_of(bundle, selected);
    options.default_plate = int(selected.get_default_bed_type(&bundle));

    // OrcaSlicer's own test, as its filament list applies it to the selected
    // printer: compatible_printers, the condition, and the library's
    // exclusions. One preset per shown name, as the list shows them; a name
    // that fits is not offered again among the ones that do not.
    const PresetWithVendorProfile active = bundle.printers.get_edited_preset_with_vendor_profile();
    const auto fits = [&bundle, &active](const Preset& filament) {
        return is_compatible_with_printer(bundle.filaments.get_preset_with_vendor_profile(filament), active);
    };
    const auto label_of = [](const Preset& filament) { return filament.alias.empty() ? filament.name : filament.alias; };
    std::set<std::string> labels;
    for (const Preset& filament : bundle.filaments)
        if (!filament.is_default && filament.is_system && fits(filament) && labels.insert(label_of(filament)).second)
            options.filaments.push_back(FilamentOption{filament.name, label_of(filament), first_of(filament, "filament_vendor"),
                                                       first_of(filament, "filament_type")});
    for (const Preset& filament : bundle.filaments)
        if (!filament.is_default && filament.is_system && !fits(filament) && labels.insert(label_of(filament)).second)
            options.others.push_back(FilamentOption{filament.name, label_of(filament), first_of(filament, "filament_vendor"),
                                                    first_of(filament, "filament_type"), made_for(bundle, filament)});
    // The person's own presets, tested the same way against the printer the
    // form would add. The new printer is saved under a name of its own and
    // inherits this profile, so a preset made for the profile fits it too.
    for (const Preset& preset : own_presets) {
        FilamentOption option{preset.name, preset.name, first_of(preset, "filament_vendor"), first_of(preset, "filament_type")};
        if (!is_compatible_with_printer(PresetWithVendorProfile(preset, nullptr), active))
            option.made_for = made_for(bundle, preset);
        if (const std::string& parent = preset.inherits(); !parent.empty()) {
            const Preset* base = bundle.filaments.find_preset(parent, false);
            option.base        = base != nullptr && base->name == parent ? label_of(*base) : parent;
        }
        options.own.push_back(std::move(option));
    }
    const auto by_label = [](const FilamentOption& a, const FilamentOption& b) {
        return boost::algorithm::to_lower_copy(a.label) < boost::algorithm::to_lower_copy(b.label);
    };
    std::sort(options.filaments.begin(), options.filaments.end(), by_label);
    std::sort(options.others.begin(), options.others.end(), by_label);
    std::sort(options.own.begin(), options.own.end(), by_label);

    // A filament the profile names, as the compatible preset of the same
    // shown name: profiles often name one variant ("@BBL X1C") that the list
    // shows under its alias for every printer it fits.
    const auto compatible = [&bundle, &options](const std::string& name) -> std::string {
        const Preset* named = bundle.filaments.find_preset(name, false);
        if (named == nullptr)
            return {};
        const std::string label = named->alias.empty() ? named->name : named->alias;
        const auto        found = std::find_if(options.filaments.begin(), options.filaments.end(),
                                               [&label](const FilamentOption& option) { return option.label == label; });
        return found == options.filaments.end() ? std::string() : found->name;
    };
    if (const VendorProfile::PrinterModel* model = PresetUtils::system_printer_model(selected))
        for (const std::string& material : model->default_materials)
            if (const std::string name = compatible(material);
                !name.empty() && std::find(options.suggested.begin(), options.suggested.end(), name) == options.suggested.end())
                options.suggested.push_back(name);
    options.default_filament = compatible(first_of(selected, "default_filament_profile"));
    // The profile's own default leads the suggestions: it is what the
    // printer starts with when the person picks nothing.
    if (!options.default_filament.empty()) {
        options.suggested.erase(std::remove(options.suggested.begin(), options.suggested.end(), options.default_filament),
                                options.suggested.end());
        options.suggested.insert(options.suggested.begin(), options.default_filament);
    }
    return options;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
