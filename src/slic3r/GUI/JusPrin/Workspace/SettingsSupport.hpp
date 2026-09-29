#pragma once

#include "Workspace.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <cctype>
#include <numeric>
#include <string_view>
#include <tuple>

namespace Slic3r::GUI::JusPrin::Workspace {

// The process keys the tools may write. Each one was traced through
// ConfigManipulation's normalizer and toggles and Tab's handlers; the hazard
// table in the tool extension guide says what each can rewrite or ask.
inline bool writable_setting(const std::string& key)
{
    constexpr std::string_view keys[] = {
        // Layers, walls, shells, infill.
        "layer_height", "wall_loops", "sparse_infill_density", "sparse_infill_pattern", "top_shell_layers",
        "bottom_shell_layers", "top_shell_thickness", "bottom_shell_thickness", "wall_generator", "detect_thin_wall",
        "only_one_wall_top", "top_surface_pattern", "bottom_surface_pattern", "internal_solid_infill_pattern",
        "infill_direction",
        // Supports.
        "enable_support", "support_type", "support_style", "support_threshold_angle", "support_on_build_plate_only",
        "support_interface_top_layers", "support_interface_bottom_layers", "support_interface_pattern",
        "support_top_z_distance", "support_bottom_z_distance",
        // Adhesion and seam.
        "brim_type", "brim_width", "skirt_loops", "skirt_distance", "seam_position",
        // Speeds.
        "outer_wall_speed", "inner_wall_speed", "sparse_infill_speed", "internal_solid_infill_speed", "top_surface_speed",
        "initial_layer_speed", "travel_speed", "bridge_speed", "gap_infill_speed", "support_speed"};
    return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
}

// The filament keys the tools may write: temperatures, cooling, flow and
// custom g-code. Tab::load_config on the filament tab opens no dialog for
// them except the two OrcaSettings refuses first (volumetric speed below 0.5,
// and a first-layer temperature too far from the other layers' at save).
inline bool writable_filament_setting(const std::string& key)
{
    constexpr std::string_view keys[] = {
        "nozzle_temperature", "nozzle_temperature_initial_layer", "nozzle_temperature_range_low",
        "nozzle_temperature_range_high", "cool_plate_temp", "cool_plate_temp_initial_layer", "eng_plate_temp",
        "eng_plate_temp_initial_layer", "hot_plate_temp", "hot_plate_temp_initial_layer", "textured_plate_temp",
        "textured_plate_temp_initial_layer", "supertack_plate_temp", "supertack_plate_temp_initial_layer",
        "textured_cool_plate_temp", "textured_cool_plate_temp_initial_layer", "fan_min_speed", "fan_max_speed",
        "fan_cooling_layer_time", "slow_down_layer_time", "slow_down_min_speed", "close_fan_the_first_x_layers",
        "overhang_fan_speed", "overhang_fan_threshold", "additional_cooling_fan_speed", "filament_flow_ratio",
        "enable_pressure_advance", "pressure_advance", "filament_max_volumetric_speed", "filament_density",
        "filament_cost", "filament_start_gcode", "filament_end_gcode", "filament_notes"};
    return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
}

// The printer keys the tools may write: custom g-code, height, layer-height
// limits, retraction and machine limits. Not the keys that decide which
// filament and process fit the printer or how many extruders it has -- bed
// shape, nozzle size and type, extruder count, printer model -- which change
// with a different printer or nozzle, through printer_setup or printer_change.
inline bool writable_printer_setting(const std::string& key)
{
    constexpr std::string_view keys[] = {
        "machine_start_gcode", "machine_end_gcode", "before_layer_change_gcode", "layer_change_gcode",
        "change_filament_gcode", "machine_pause_gcode", "printable_height", "max_layer_height", "min_layer_height",
        "z_offset", "printer_notes", "retraction_length", "retraction_speed", "deretraction_speed", "z_hop",
        "retract_restart_extra", "wipe", "retract_before_wipe", "wipe_distance", "use_firmware_retraction",
        "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z",
        "machine_max_acceleration_e", "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
        "machine_max_acceleration_travel", "machine_max_speed_x", "machine_max_speed_y", "machine_max_speed_z",
        "machine_max_speed_e", "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e"};
    return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
}

// An object's writable keys are the process ones it may override; the caller
// checks that the key is one an object can hold.
inline bool writable_setting(const std::string& key, SettingsScope scope)
{
    switch (scope) {
    case SettingsScope::Process:
    case SettingsScope::Object: return writable_setting(key);
    case SettingsScope::Filament: return writable_filament_setting(key);
    case SettingsScope::Printer: return writable_printer_setting(key);
    }
    return false;
}

inline const char* scope_name(SettingsScope scope)
{
    switch (scope) {
    case SettingsScope::Process: return "process";
    case SettingsScope::Object: return "object";
    case SettingsScope::Filament: return "filament";
    case SettingsScope::Printer: return "printer";
    }
    return "process";
}

// Case-folded for a substring search over ASCII keys and labels. Not a
// locale-aware fold: Orca's setting keys are ASCII, and a preset name that is
// not still matches on the part that is.
inline std::string ascii_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return text;
}

// `changed` lists the keys whose value differs from the saved preset; only
// read when the query asks for changed settings.
inline SettingsSearchResult search_setting_definitions(const std::vector<SettingDefinition>& definitions,
                                                       const SettingsQuery& query,
                                                       const std::vector<std::string>& changed = {})
{
    SettingsSearchResult result;
    std::size_t offset = 0;
    if (query.limit == 0 || query.limit > 25) {
        result.error = SettingIssue{"", "invalid_arguments", "Search limit must be between 1 and 25."};
        return result;
    }
    if (!query.cursor.empty()) {
        const std::string_view cursor(query.cursor);
        const auto digits = cursor.substr(std::min<std::size_t>(7, cursor.size()));
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), offset);
        if (cursor.substr(0, 7) != "offset:" || digits.empty() || parsed.ec != std::errc() ||
            parsed.ptr != digits.data() + digits.size()) {
            result.error = SettingIssue{"", "invalid_arguments", "Invalid settings cursor. Search again without a cursor."};
            return result;
        }
    }
    // Several words or keys search for each of them; a setting that matches
    // more of them ranks first, then by how well its best term matched.
    std::vector<std::string> terms;
    const std::string text = ascii_lower(query.text);
    for (std::size_t start = 0; start <= text.size();) {
        const std::size_t end = std::min(text.find_first_of(" ,;", start), text.size());
        if (end > start)
            terms.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (terms.empty())
        terms.push_back({});
    std::vector<std::pair<std::size_t, const SettingDefinition*>> matches;
    for (const auto& def : definitions) {
        if (query.writable_only && !def.writable)
            continue;
        if (query.changed_only && std::find(changed.begin(), changed.end(), def.key) == changed.end())
            continue;
        const std::string key = ascii_lower(def.key), label = ascii_lower(def.label), description = ascii_lower(def.description);
        std::size_t matched = 0, best = 4;
        for (const std::string& term : terms) {
            // A key that holds the term ranks with the keys that start with it:
            // "support" finds enable_support among the support_ keys.
            const std::size_t rank = key == term ? 0 : key.find(term) != std::string::npos ? 1 : label.find(term) != std::string::npos ? 2 :
                                     description.find(term) != std::string::npos ? 3 : 4;
            if (rank < 4) {
                ++matched;
                best = std::min(best, rank);
            }
        }
        if (matched > 0)
            matches.emplace_back((terms.size() - matched) * 4 + best, &def);
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        return a.first < b.first || (a.first == b.first && a.second->key < b.second->key);
    });
    if (offset > matches.size()) {
        result.error = SettingIssue{"", "invalid_arguments", "Settings cursor is past the last match. Search again."};
        return result;
    }
    const std::size_t end = offset + std::min(query.limit, matches.size() - offset);
    for (std::size_t i = offset; i < end; ++i)
        result.items.push_back(*matches[i].second);
    result.truncated = end < matches.size();
    if (result.truncated)
        result.next_cursor = "offset:" + std::to_string(end);
    return result;
}

inline std::vector<std::string> setting_words(const std::string& key)
{
    std::vector<std::string> words;
    std::size_t start = 0;
    for (std::size_t end; (end = key.find('_', start)) != std::string::npos; start = end + 1)
        words.push_back(key.substr(start, end - start));
    words.push_back(key.substr(start));
    return words;
}

inline std::vector<std::string> setting_suggestions(const std::string& key,
                                                    const std::vector<SettingDefinition>& definitions)
{
    // Edit distance handles misspellings, and shared words handle guessed
    // names ("enable_brim" for "brim_type"), without a second alias catalog.
    // More shared words first, writable settings before read-only ones, then
    // the closer spelling.
    using Rank = std::tuple<std::size_t, bool, std::size_t, std::string>;
    std::vector<Rank> ranked;
    const std::string needle = ascii_lower(key);
    const auto words = setting_words(needle);
    for (const auto& def : definitions) {
        std::vector<std::size_t> row(def.key.size() + 1);
        std::iota(row.begin(), row.end(), 0);
        for (std::size_t i = 0; i < needle.size(); ++i) {
            std::size_t diagonal = row[0];
            row[0] = i + 1;
            for (std::size_t j = 0; j < def.key.size(); ++j) {
                const auto old = row[j + 1];
                row[j + 1] = std::min({row[j] + 1, old + 1, diagonal + (needle[i] != def.key[j])});
                diagonal = old;
            }
        }
        // "enable" alone says nothing about which setting was meant; with a
        // word that does ("support_enable"), it counts.
        std::size_t shared = 0;
        bool        telling = false;
        for (const auto& word : words)
            if (word.size() >= 4 && ("_" + def.key + "_").find("_" + word + "_") != std::string::npos) {
                ++shared;
                telling = telling || word != "enable";
            }
        if (!telling)
            shared = 0;
        if (shared > 0 || row.back() <= std::max<std::size_t>(2, needle.size() / 3))
            ranked.emplace_back(std::numeric_limits<std::size_t>::max() - shared, !def.writable, row.back(), def.key);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<std::string> result;
    for (std::size_t i = 0; i < std::min<std::size_t>(3, ranked.size()); ++i)
        result.push_back(std::get<3>(ranked[i]));
    return result;
}

inline std::vector<SettingChange> settings_confirmation(const SettingsPreview& preview)
{
    auto result = preview.changes;
    result.insert(result.end(), preview.dependencies.begin(), preview.dependencies.end());
    return result;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
