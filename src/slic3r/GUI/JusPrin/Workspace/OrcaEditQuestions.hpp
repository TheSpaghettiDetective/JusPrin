#pragma once

// The questions OrcaSlicer asks a person who changes a setting by hand.
//
// Tab::on_value_change (Tab.cpp) asks them, one field at a time. A settings
// patch goes through Tab::load_config, which never runs it, so preview hands
// each question to the caller as a warning: the words the person would read
// as the message and, under `suggestions`, the values OrcaSlicer's Yes would
// set. The patch applies as sent. What to do about the question is the
// caller's decision.
//
// The conditions and the message ids are copied from Tab.cpp, which upstream
// changes. Each id is looked up in the catalog as Tab.cpp looks it up, piece
// by piece, because the catalog rewords them, in English too. The workspace
// adapter harness holds the copy to the original: it makes each change the
// way a field does, lets OrcaSlicer's handler run, and compares the dialogs
// it opens with the questions here, for the cases below and for every
// writable setting (verify_edit_questions and verify_every_writable_setting
// in tests/workspace/real_adapter_harness.cpp).

#include "OrcaSettingDefinitions.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::Workspace {

inline constexpr const char* kEditQuestion = "orca_question";

// `changes` are the patch's settings that take another value; `before` and
// `after` are the scope's whole configuration around the patch as written
// (for an object, the process with the object's overrides). OrcaSlicer asks
// about the field being changed, and reads its condition from the values the
// change leaves, before it normalizes any of them.
inline void forward_edit_questions(SettingsScope scope, const std::vector<SettingChange>& changes, const DynamicPrintConfig& before,
                                   const DynamicPrintConfig& after, std::vector<SettingIssue>& warnings)
{
    const auto changed = [&](const char* key) {
        return std::any_of(changes.begin(), changes.end(), [key](const SettingChange& change) { return change.key == key; });
    };
    // A switch held once per extruder, turned on for one of them.
    const auto turned_on = [&](const char* key) {
        const auto* was = dynamic_cast<const ConfigOptionVector<unsigned char>*>(before.option(key));
        const auto* now = dynamic_cast<const ConfigOptionVector<unsigned char>*>(after.option(key));
        if (!changed(key) || was == nullptr || now == nullptr)
            return false;
        for (std::size_t index = 0; index < now->values.size(); ++index)
            if (now->values[index] == 1 && (index >= was->values.size() || was->values[index] != 1))
                return true;
        return false;
    };
    const auto ask = [&](const char* key, std::string words, std::vector<std::string> yes = {}) {
        warnings.push_back({key, kEditQuestion, std::move(words), {}, std::move(yes)});
    };

    if (scope == SettingsScope::Filament) {
        if (turned_on("filament_long_retractions_when_cut"))
            ask("filament_long_retractions_when_cut",
                _u8L("Experimental feature: Retracting and cutting off the filament at a greater distance during filament changes to minimize flush. "
                     "Although it can notably reduce flush, it may also elevate the risk of nozzle clogs or other printing complications. "
                     "Please use with the latest printer firmware."));
        return;
    }
    if (scope == SettingsScope::Printer) {
        if (turned_on("long_retractions_when_cut"))
            ask("long_retractions_when_cut",
                _u8L("Experimental feature: Retracting and cutting off the filament at a greater distance during filament changes to minimize flush. "
                     "Although it can notably reduce flush, it may also elevate the risk of nozzle clogs or other printing complications."));
        return;
    }

    PresetBundle&             bundle  = *wxGetApp().preset_bundle;
    const DynamicPrintConfig& printer = bundle.printers.get_edited_preset().config;
    const bool                tower   = after.opt_bool("enable_prime_tower");
    const bool                smooth  = after.opt_enum<TimelapseType>("timelapse_type") == tlSmooth;

    if (changed("enable_prime_tower") && !tower && smooth) {
        ask("enable_prime_tower", _u8L("A prime tower is required for smooth timelapse. There may be flaws on the model without prime tower. Are you sure you want to disable prime tower?"));
        // Asked of a person who went on without the tower.
        if (after.opt_bool("enable_wrapping_detection"))
            ask("enable_prime_tower", _u8L("A prime tower is required for clumping detection. There may be flaws on the model without prime tower. Are you sure you want to disable prime tower?"));
    }
    if (changed("enable_prime_tower") && tower && after.opt_bool("precise_z_height"))
        ask("enable_prime_tower", _u8L("Enabling both precise Z height and the prime tower may cause slicing errors. Do you still want to enable?"));
    if (changed("enable_wrapping_detection") && after.opt_bool("enable_wrapping_detection") && !tower)
        ask("enable_wrapping_detection", _u8L("A prime tower is required for clumping detection. There may be flaws on the model without prime tower. Do you still want to enable clumping detection?"));
    if (changed("precise_z_height") && after.opt_bool("precise_z_height") && tower)
        ask("precise_z_height", _u8L("Enabling both precise Z height and the prime tower may cause slicing errors. Do you still want to enable precise Z height?"));
    if (changed("timelapse_type") && smooth && !tower)
        ask("timelapse_type", _u8L("A prime tower is required for smooth timelapse. There may be flaws on the model without prime tower. Do you want to enable prime tower?"),
            {"enable_prime_tower=1"});

    if (changed("print_sequence") && after.opt_enum<PrintSequence>("print_sequence") == PrintSequence::ByObject) {
        const auto* structure = printer.option<ConfigOptionEnum<PrinterStructure>>("printer_structure");
        if (structure != nullptr && structure->value == PrinterStructure::psI3)
            ask("print_sequence", _u8L("The current printer does not support timelapse in Traditional Mode when printing By-Object.") +
                                      "\n\n" + _u8L("Still print by object?"));
    }

    // Filament numbers are shown from 1 and held from 0; 0 is "default".
    const int base = after.opt_int("support_filament") - 1, contact = after.opt_int("support_interface_filament") - 1;
    if (changed("support_filament") && is_support_filament(base, false) && !is_soluble_filament(base) &&
        !has_filaments({"TPU", "TPU-AMS"}))
        ask("support_filament", _u8L("Non-soluble support materials are not recommended for support base.\n"
                                     "Are you sure to use them for support base?\n"));
    if (changed("support_interface_filament")) {
        const std::string interlaced  = ConfigOptionEnum<SupportMaterialInterfacePattern>(smipRectilinearInterlaced).serialize();
        const bool        recommended = after.opt_float("support_top_z_distance") == 0 && after.opt_float("support_interface_spacing") == 0 &&
                                 after.option("support_interface_pattern")->serialize() == interlaced;
        const bool soluble = is_soluble_filament(contact), soluble_alone = soluble && !is_soluble_filament(base);
        if ((is_support_filament(contact, false) && !recommended) || soluble_alone) {
            // What Yes sets, leaving out what already holds.
            std::vector<std::string> yes;
            const auto set = [&](const char* key, const std::string& value) {
                if (after.option(key)->serialize() != value)
                    yes.push_back(std::string(key) + "=" + value);
            };
            set("support_top_z_distance", "0");
            set("support_interface_spacing", "0");
            set("support_interface_pattern", interlaced);
            set("independent_support_layer_height", "0");
            const Preset* filament = bundle.filaments.find_preset(bundle.filament_presets[contact]);
            const auto*   type     = filament ? filament->config.option<ConfigOptionStrings>("filament_type") : nullptr;
            if (soluble_alone || (type != nullptr && !type->values.empty() && type->values.front() == "PLA" &&
                                  has_filaments({"TPU", "TPU-AMS"})))
                set("support_filament", std::to_string(contact + 1));
            ask("support_interface_filament",
                (soluble ? _u8L("When using soluble material for the support interface, we recommend the following settings:\n"
                                "0 top Z distance, 0 interface spacing, interlaced rectilinear pattern, disable independent support layer height\n"
                                "and use soluble materials for both support interface and support base.") :
                           _u8L("When using support material for the support interface, we recommend the following settings:\n"
                                "0 top Z distance, 0 interface spacing, interlaced rectilinear pattern and disable independent support layer height.")) +
                    "\n\n" + _u8L("Change these settings automatically?\n"
                                  "Yes - Change these settings automatically\n"
                                  "No  - Do not change these settings for me"),
                std::move(yes));
        }
    }

    if (changed("make_overhang_printable") && after.opt_bool("make_overhang_printable"))
        ask("make_overhang_printable",
            _u8L("Enabling this option will modify the model's shape. If your print requires precise dimensions or is part of an "
                 "assembly, it's important to double-check whether this change in geometry impacts the functionality of your print.") +
                "\n\n" + _u8L("Are you sure you want to enable this option?"));

    // Asked when a template is first given, not when one is edited.
    if (changed("sparse_infill_rotate_template") && before.opt_string("sparse_infill_rotate_template").empty()) {
        const auto pattern = after.opt_enum<InfillPattern>("sparse_infill_pattern");
        const bool rotates = pattern == ipRectilinear || pattern == ipLine || pattern == ipZigZag || pattern == ipCrossZag ||
                             pattern == ipLockedZag;
        if (!rotates)
            ask("sparse_infill_rotate_template",
                _u8L("Infill patterns are typically designed to handle rotation automatically to ensure proper printing and achieve their "
                     "intended effects (e.g., Gyroid, Cubic). Rotating the current sparse infill pattern may lead to insufficient support. "
                     "Please proceed with caution and thoroughly check for any potential printing issues. "
                     "Are you sure you want to enable this option?") +
                    "\n\n" + _u8L("Are you sure you want to enable this option?"));
    }

    // The limits of the printer's nozzles. A height at zero, or above the
    // first nozzle's limit when that is over 0.2, is refused before this.
    if (changed("layer_height")) {
        const auto*  lowest  = printer.option<ConfigOptionFloats>("min_layer_height");
        const auto*  highest = printer.option<ConfigOptionFloats>("max_layer_height");
        const double height  = after.opt_float("layer_height");
        if (lowest != nullptr && highest != nullptr && !lowest->values.empty() && !highest->values.empty()) {
            const double floor = *std::min_element(lowest->values.begin(), lowest->values.end());
            const double ceil  = *std::max_element(highest->values.begin(), highest->values.end());
            if (height < floor || height > ceil)
                warnings.push_back({"layer_height", kEditQuestion,
                                    _u8L("Layer height exceeds the limit in Printer Settings -> Extruder -> Layer height limits, "
                                         "this may cause printing quality issues.") +
                                        "\n\n" + _u8L("Adjust to the set range automatically?\n"),
                                    {}, {}, floor, ceil});
        }
    }
}

} // namespace Slic3r::GUI::JusPrin::Workspace
