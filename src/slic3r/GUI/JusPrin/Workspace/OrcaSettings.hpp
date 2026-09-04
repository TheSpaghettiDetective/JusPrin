#pragma once

#include "SettingsSupport.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/ConfigManipulation.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"

#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace Slic3r::GUI::JusPrin::Workspace {

inline std::string setting_type(ConfigOptionType type)
{
    const bool vector = (type & coVectorType) != 0;
    const char* name;
    switch (type & ~coVectorType) {
    case coNone: name = "none"; break;
    case coFloat: name = "float"; break;
    case coInt: name = "integer"; break;
    case coString: name = "string"; break;
    case coPercent: name = "percent"; break;
    case coFloatOrPercent: name = "float_or_percent"; break;
    case coPoint: name = "point"; break;
    case coPoint3: name = "point3"; break;
    case coBool: name = "boolean"; break;
    case coEnum: name = "enum"; break;
    case coPointsGroups - coVectorType: name = "point_groups"; break;
    case coIntsGroups - coVectorType: name = "integer_groups"; break;
    default: throw std::logic_error("Unknown Orca setting type");
    }
    return std::string(name) + (vector ? "[]" : "");
}

inline SettingDefinition setting_definition(const std::string& key)
{
    const auto& def = *print_config_def.get(key);
    SettingDefinition result{key, setting_type(def.type), def.full_label.empty() ? def.label : def.full_label,
        def.category, def.tooltip, def.sidetext, {}, {}, def.enum_values, def.enum_labels, writable_setting(key) && !def.readonly};
    if (def.min != -std::numeric_limits<float>::max()) result.min = def.min;
    if (def.max != std::numeric_limits<float>::max()) result.max = def.max;
    return result;
}

inline std::vector<SettingDefinition> process_definitions()
{
    std::vector<SettingDefinition> result;
    for (const auto& key : Preset::print_options())
        if (print_config_def.get(key)) result.push_back(setting_definition(key));
    return result;
}

inline SettingIssue missing_process_setting(const std::string& key)
{
    return print_config_def.get(key) ? SettingIssue{key, "unsupported_scope", "This key is not a process setting."} :
        SettingIssue{key, "unknown_setting", "Unknown setting.", {}, setting_suggestions(key, process_definitions())};
}

inline bool has_process_setting(const std::string& key)
{
    const auto& keys = Preset::print_options();
    return print_config_def.get(key) && std::find(keys.begin(), keys.end(), key) != keys.end();
}

inline bool process_settings_available()
{
    auto* bundle = wxGetApp().preset_bundle;
    return bundle && bundle->printers.get_edited_preset().printer_technology() == ptFFF &&
           !bundle->prints.get_edited_preset().name.empty() && wxGetApp().get_tab(Preset::TYPE_PRINT);
}

// Orca's scalar deserializers accept a numeric prefix (e.g. "4bad"). Check
// complete consumption before calling the authoritative parser, so malformed
// input cannot silently become a different approved value.
inline bool complete_setting_number(const std::string& text, ConfigOptionType type)
{
    if (type != coFloat && type != coInt && type != coPercent) return true;
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double number;
    if (!(input >> number) || !std::isfinite(number)) return false;
    input >> std::ws;
    if (type == coPercent && input.peek() == '%') { input.get(); input >> std::ws; }
    if (!input.eof()) return false;
    if (type == coInt) {
        std::istringstream integer_input(text);
        int integer;
        if (!(integer_input >> integer)) return false;
        integer_input >> std::ws;
        return integer_input.eof();
    }
    return true;
}

// Audit against ConfigManipulation::update_print_fff_config. These are every
// active modal predicate, including unrelated pre-existing invalid values:
// update() evaluates the entire config after any allowed batch. Keep refusal
// here; the live normalizer remains unchanged and owns its UI behavior.
inline void check_process_dialogs(const DynamicPrintConfig& config, SettingsPreview& result)
{
    const auto block = [&](bool condition, const std::string& key, const std::string& message) {
        if (condition) result.issues.push_back({key, "incompatible_settings", message});
    };
    const double height = config.opt_float("layer_height");
    const auto& printer = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const double maximum = printer.opt_float("max_layer_height", 0);
    if (height < EPSILON)
        result.issues.push_back({"layer_height", "invalid_setting_value", "Layer height is below Orca's minimum.", {}, {}, EPSILON, {}});
    if (maximum > 0.2 && height > maximum + EPSILON)
        result.issues.push_back({"layer_height", "invalid_setting_value", "Layer height exceeds the printer maximum.", {}, {}, {}, maximum});
    for (const auto* key : {"ironing_spacing", "support_ironing_spacing"})
        block(config.opt_float(key) < 0.05, key, "Ironing spacing would open Orca's correction dialog; correct it first.");
    block(config.opt_float("initial_layer_print_height") < EPSILON, "initial_layer_print_height", "Initial layer height would open Orca's correction dialog.");
    for (const auto* key : {"xy_hole_compensation", "xy_contour_compensation"})
        block(std::abs(config.opt_float(key)) > 2, key, "XY compensation would open Orca's correction dialog.");
    block(config.opt_float("elefant_foot_compensation") > 1, "elefant_foot_compensation", "Elephant foot compensation would open Orca's correction dialog.");
    block(config.opt_bool("spiral_mode") && !(config.opt_int("wall_loops") == 1 && config.opt_int("top_shell_layers") == 0 &&
        config.option<ConfigOptionPercent>("sparse_infill_density")->value == 0 && !config.opt_bool("enable_support") &&
        config.opt_int("enforce_support_layers") == 0 && !config.opt_bool("detect_thin_wall") && !config.opt_bool("overhang_reverse") &&
        config.opt_enum<TimelapseType>("timelapse_type") == tlTraditional && !config.opt_bool("enable_wrapping_detection")),
        "spiral_mode", "Spiral mode conflicts with wall_loops, top_shell_layers, infill, support, thin walls, overhang reversal, timelapse, or wrapping detection.");
    block(config.opt_bool("alternate_extra_wall") && config.opt_enum<EnsureVerticalShellThickness>("ensure_vertical_shell_thickness") == evstAll,
        "alternate_extra_wall", "Alternate extra wall conflicts with ensure_vertical_shell_thickness=all.");
    block(config.opt_enum<SeamScarfType>("seam_slope_type") != SeamScarfType::None && config.get_abs_value("seam_slope_start_height") >= height,
        "layer_height", "Layer height conflicts with seam_slope_type and seam_slope_start_height; scarf start height must be below layer height.");
    block(config.opt_float("infill_lock_depth") > config.opt_float("skin_infill_depth"), "infill_lock_depth", "Infill lock depth exceeds skin_infill_depth.");
    block(config.opt_enum<FuzzySkinMode>("fuzzy_skin_mode") != FuzzySkinMode::Displacement &&
        config.opt_enum<PerimeterGeneratorType>("wall_generator") != PerimeterGeneratorType::Arachne,
        "fuzzy_skin_mode", "This fuzzy skin mode requires wall_generator=arachne.");
}

inline bool process_tab_toggles_options(Tab& tab)
{
    // Public field lookup identifies the active page without adding an Orca
    // accessor. TabPrint::update skips toggles on an absent/Dependencies page.
    for (const auto& key : Preset::print_options()) {
        if (tab.get_field(key)) {
            Page* page = nullptr;
            tab.get_field(key, &page);
            return page && page->title() != "Dependencies";
        }
    }
    return false;
}

inline void predict_process_normalization(DynamicPrintConfig& config)
{
    // The modal predicates above were checked first. Execute Orca's own
    // normalization on the clone with no presentation callbacks; don't keep
    // a duplicate table of dependent values or infill-pattern capabilities.
    ConfigManipulation normalizer([] {}, [](const std::string&, bool, int) {}, {}, {});
    auto& bundle = *wxGetApp().preset_bundle;
    const auto& selected = bundle.prints.get_selected_preset();
    normalizer.initialize_support_material_overhangs_queried(!selected.is_system && !selected.is_dirty &&
        config.opt_bool("enable_support") && !config.opt_bool("detect_overhang_wall"));
    normalizer.set_is_BBL_Printer(bundle.is_bbl_vendor());
    normalizer.update_print_fff_config(&config, true, false);
    if (process_tab_toggles_options(*wxGetApp().get_tab(Preset::TYPE_PRINT)))
        normalizer.toggle_print_fff_options(&config, true);
}

} // namespace Slic3r::GUI::JusPrin::Workspace
