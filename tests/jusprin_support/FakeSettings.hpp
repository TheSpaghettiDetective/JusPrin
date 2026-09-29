#pragma once

#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace Slic3r::GUI::JusPrin::Workspace {

// Deliberately small test fixture, not production setting metadata. The real
// adapter reads all definitions and canonical values from Orca.
class FakeSettings
{
public:
    FakeSettings()
    {
        add("layer_height", "float", "Layer height", "0.2", 0.001, 0.3, "mm");
        add("wall_loops", "integer", "Wall loops", "2", 0, 1000);
        add("sparse_infill_density", "percent", "Sparse infill density", "15%", 0, 100, "%");
        add("sparse_infill_pattern", "enum", "Sparse infill pattern", "grid");
        definitions.back().enum_values = {"grid", "gyroid", "line"};
        definitions.back().enum_labels = {"Grid", "Gyroid", "Line"};
        add("top_shell_layers", "integer", "Top shell layers", "5", 0, 1000);
        add("bottom_shell_layers", "integer", "Bottom shell layers", "3", 0, 1000);
        add("brim_width", "float", "Brim width", "5", 0, 100, "mm");
        add("spiral_mode", "boolean", "Spiral vase", "0");
        add("notes", "string", "Notes", "Fixture process");
        add("fill_multiline", "integer", "Multiline infill", "1", 1, 10);
        add("support_top_z_distance", "float", "Top support gap", "0.25", 0, 10, "mm");
        add("skirt_loops", "integer", "Skirt loops", "0", 0, 10);
        preset_values = values;

        filament_definitions = {definition_of("nozzle_temperature", "integer", "Nozzle temperature", SettingsScope::Filament, 0, 500, "°C"),
                                definition_of("filament_type", "string", "Type", SettingsScope::Filament)};
        printer_definitions  = {definition_of("machine_start_gcode", "string", "Start G-code", SettingsScope::Printer),
                                definition_of("nozzle_diameter", "float", "Nozzle diameter", SettingsScope::Printer, 0, 10, "mm")};
        filaments["Fixture PLA"]  = preset_of({{"nozzle_temperature", "210"}, {"filament_type", "PLA"}}, false);
        filaments["Fixture PETG"] = preset_of({{"nozzle_temperature", "240"}, {"filament_type", "PETG"}}, true);
        printers["Fixture printer"] = preset_of({{"machine_start_gcode", "G28"}, {"nozzle_diameter", "0.4"}}, true);
        printers["Garage printer"]  = preset_of({{"machine_start_gcode", "G28 ; garage"}, {"nozzle_diameter", "0.4"}}, false);
    }

    // A filament or printer preset. `values` are what Orca's tab holds for
    // the selected one, unsaved edits included; `saved` is its file.
    struct FakePreset
    {
        std::map<std::string, std::string> values, saved;
        bool system{false};
    };

    // The fixture's print-wide keys: an object cannot override them.
    static bool object_setting(const std::string& key) { return key != "spiral_mode" && key != "skirt_loops"; }

    // The values a target prints with: the process values, then the object's
    // own overrides.
    std::map<std::string, std::string> effective(const SettingsTarget& target) const
    {
        auto result = values;
        if (target.object)
            if (const auto found = overrides.find(target.object->value()); found != overrides.end())
                for (const auto& [key, value] : found->second)
                    result[key] = value;
        return result;
    }

    static bool preset_scope(const SettingsTarget& target)
    {
        return target.scope == SettingsScope::Filament || target.scope == SettingsScope::Printer;
    }

    const FakePreset* preset(const SettingsTarget& target) const
    {
        const auto& all   = target.scope == SettingsScope::Filament ? filaments : printers;
        const auto  found = all.find(target.preset);
        return found == all.end() ? nullptr : &found->second;
    }

    bool selected(const SettingsTarget& target) const
    {
        return target.preset == (target.scope == SettingsScope::Filament ? selected_filament : selected_printer);
    }

    SettingIssue unknown_preset(const SettingsTarget& target) const
    {
        return {"", "unknown_preset", std::string("No ") + scope_name(target.scope) + " preset is named \"" + target.preset + "\"."};
    }

    SettingsSearchResult search(const SettingsQuery& query, const std::vector<std::string>& process_changed) const
    {
        if (!preset_scope(query.target)) {
            auto result   = search_setting_definitions(definitions, query, process_changed);
            result.preset = "Fixture process";
            return result;
        }
        const FakePreset* found = preset(query.target);
        if (found == nullptr) {
            SettingsSearchResult result;
            result.error = unknown_preset(query.target);
            return result;
        }
        std::vector<std::string> changed;
        if (selected(query.target))
            for (const auto& [key, value] : found->values)
                if (found->saved.at(key) != value) changed.push_back(key);
        auto result   = search_setting_definitions(scope_definitions(query.target.scope), query, changed);
        result.preset = query.target.preset;
        return result;
    }

    SettingsReadResult read(const std::vector<std::string>& keys, const SettingsTarget& target = {}) const
    {
        SettingsReadResult result;
        if (keys.empty() || keys.size() > 32) {
            result.error = SettingIssue{"", "invalid_arguments", "Read 1 to 32 setting keys."};
            return result;
        }
        if (preset_scope(target)) {
            const FakePreset* found = preset(target);
            if (found == nullptr) {
                result.error = unknown_preset(target);
                return result;
            }
            result.preset = target.preset;
            for (const auto& key : keys) {
                const auto* def = definition(key, target.scope);
                if (def == nullptr) {
                    result.unknown_keys.push_back(key);
                    result.issues.push_back(unknown(key, target.scope));
                    continue;
                }
                const auto& values = selected(target) ? found->values : found->saved;
                const bool  dirty  = selected(target) && found->values.at(key) != found->saved.at(key);
                result.items.push_back({key, values.at(key), dirty, false, *def});
            }
            return result;
        }
        result.preset = "Fixture process";
        for (const auto& key : keys) {
            const auto* def = definition(key);
            if (!def) {
                result.unknown_keys.push_back(key);
                result.issues.push_back(unknown(key));
            } else {
                const auto current = effective(target);
                const auto& value = current.at(key);
                const bool dirty = values.at(key) != preset_values.at(key);
                result.items.push_back({key, value, dirty, dirty, *def});
                if (target.object) {
                    const auto found = overrides.find(target.object->value());
                    result.items.back().overridden = found != overrides.end() && found->second.count(key) > 0;
                }
            }
        }
        return result;
    }

    SettingsPreview preview(const SettingsPatch& patch) const
    {
        if (preset_scope(patch.target))
            return preview_preset(patch);
        SettingsPreview result;
        result.preset = "Fixture process";
        if (patch.changes.empty() || patch.changes.size() > 32) {
            result.issues.push_back({"", "invalid_arguments", "A patch must contain 1 to 32 settings."});
            return result;
        }
        const auto current = effective(patch.target);
        auto next = current;
        for (const auto& [key, text] : patch.changes) {
            const auto* def = definition(key);
            if (!def) {
                result.issues.push_back(unknown(key));
                continue;
            }
            if (!def->writable) {
                result.issues.push_back({key, "unsupported_setting_mutation", "This process setting is read-only."});
                continue;
            }
            if (patch.target.object && !object_setting(key)) {
                result.issues.push_back({key, "unsupported_scope", "This setting applies to the whole print and cannot differ per object."});
                continue;
            }
            std::string canonical = text;
            bool valid = true;
            if (def->type == "enum") {
                valid = std::find(def->enum_values.begin(), def->enum_values.end(), text) != def->enum_values.end();
            } else {
                std::string number = text;
                if (def->type == "percent" && !number.empty() && number.back() == '%')
                    number.pop_back();
                std::size_t consumed = 0;
                double value = 0;
                try {
                    value = std::stod(number, &consumed);
                    valid = consumed == number.size() && std::isfinite(value);
                } catch (const std::invalid_argument&) { valid = false; }
                  catch (const std::out_of_range&) { valid = false; }
                valid = valid && (!def->min || value >= *def->min) && (!def->max || value <= *def->max) &&
                        (def->type != "integer" || std::trunc(value) == value);
                if (valid && def->type == "integer") {
                    std::istringstream integer_input(number);
                    int integer;
                    valid = static_cast<bool>(integer_input >> integer);
                    integer_input >> std::ws;
                    valid = valid && integer_input.eof();
                }
                if (valid) {
                    std::ostringstream out;
                    out << std::setprecision(12) << value;
                    canonical = out.str() + (def->type == "percent" ? "%" : "");
                }
            }
            if (!valid) {
                result.issues.push_back({key, "invalid_setting_value", "Value is outside the setting's type or bounds.",
                                         def->enum_values, {}, def->min, def->max});
                continue;
            }
            next[key] = canonical;
        }
        if (next.at("spiral_mode") == "1" &&
            (next.at("wall_loops") != "1" || next.at("top_shell_layers") != "0" || next.at("sparse_infill_density") != "0%"))
            result.issues.push_back({"spiral_mode", "incompatible_settings", "Spiral mode requires one wall, no top layers, and no infill."});

        // Active Orca normalization: with infill, line does not support multiline.
        // Support-gap rounding is compiled out in Orca; do not simulate a write
        // that the production owner does not perform.
        if (!patch.target.object && next.at("sparse_infill_density") != "0%" && next.at("sparse_infill_pattern") == "line" &&
            next.at("fill_multiline") != "1") {
            result.dependencies.push_back({"fill_multiline", current.at("fill_multiline"), "1"});
            result.warnings.push_back({"fill_multiline", "normalized_dependency", "Orca resets multiline infill to 1 for this pattern."});
        }
        for (const auto& [key, text] : patch.changes) {
            if (!definition(key) || !writable_setting(key))
                continue;
            if (next.at(key) != current.at(key))
                result.changes.push_back({key, current.at(key), next.at(key)});
            else
                result.warnings.push_back({key, "unchanged", "The setting already has this value."});
        }
        result.valid = result.issues.empty();
        return result;
    }

    CommandResult apply(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed, SettingsPreview& applied)
    {
        if (preset_scope(patch.target))
            return apply_preset(patch, confirmed, applied);
        applied = preview(patch);
        const auto current = effective(patch.target);
        for (const auto& change : confirmed)
            if (current.count(change.key) == 0 || current.at(change.key) != change.before)
                return CommandResult::failure(WorkspaceError::StaleSettings, "A confirmed setting changed. Read and preview again.");
        if (!applied.valid)
            return CommandResult::failure(WorkspaceError::InvalidSettings, "The settings patch is invalid.");
        const auto actual = settings_confirmation(applied);
        if (actual.size() != confirmed.size() || !std::equal(actual.begin(), actual.end(), confirmed.begin(),
            [](const auto& a, const auto& b) { return a.key == b.key && a.before == b.before && a.after == b.after; }))
            return CommandResult::failure(WorkspaceError::StaleSettings, "The patch no longer matches the approved preview.");
        if (actual.empty())
            return CommandResult::failure(WorkspaceError::NoChange, "All requested values are unchanged.");
        for (const auto& change : actual)
            (patch.target.object ? overrides[patch.target.object->value()] : values)[change.key] = change.after;
        for (const auto& change : applied.dependencies)
            applied.warnings.push_back({change.key, "normalized", "Orca normalized this dependent setting to " + change.after + "."});
        if (patch.persist_as) {
            preset_values    = values;
            applied.saved_as = *patch.persist_as;
        }
        applied.preset_dirty = values != preset_values;
        return CommandResult::success();
    }

    std::vector<SettingDefinition> definitions, filament_definitions, printer_definitions;
    std::map<std::string, std::string> values, preset_values;
    std::map<std::uint64_t, std::map<std::string, std::string>> overrides;
    std::map<std::string, FakePreset> filaments, printers;
    std::string selected_filament{"Fixture PLA"}, selected_printer{"Fixture printer"};
    std::vector<std::string> filament_slots{"Fixture PLA", "Fixture PETG"};

private:
    const std::vector<SettingDefinition>& scope_definitions(SettingsScope scope) const
    {
        return scope == SettingsScope::Filament ? filament_definitions : scope == SettingsScope::Printer ? printer_definitions : definitions;
    }

    static SettingDefinition definition_of(std::string key, std::string type, std::string label, SettingsScope scope,
                                           std::optional<double> min = {}, std::optional<double> max = {}, std::string unit = {})
    {
        const bool writable = writable_setting(key, scope);
        return {std::move(key), std::move(type), label, scope_name(scope), label, std::move(unit), min, max, {}, {}, writable};
    }

    static FakePreset preset_of(std::map<std::string, std::string> values, bool system) { return {values, values, system}; }

    // What SettingsPreview says of a filament or printer patch, by the rules
    // the adapter applies: a preset not being edited changes only by being
    // saved, OrcaSlicer's own preset only into a copy, and a copy only of the
    // printer in use or of a filament in the project with no unsaved edits
    // lost on the way.
    SettingsPreview preview_preset(const SettingsPatch& patch) const
    {
        SettingsPreview result;
        const FakePreset* found = preset(patch.target);
        if (found == nullptr) {
            result.issues.push_back(unknown_preset(patch.target));
            return result;
        }
        result.preset = patch.target.preset;
        if (patch.changes.empty() || patch.changes.size() > 32) {
            result.issues.push_back({"", "invalid_arguments", "A patch must contain 1 to 32 settings."});
            return result;
        }
        const bool is_selected = selected(patch.target);
        const auto& current    = is_selected ? found->values : found->saved;
        auto next = current;
        for (const auto& [key, text] : patch.changes) {
            const auto* def = definition(key, patch.target.scope);
            if (def == nullptr) {
                result.issues.push_back(unknown(key, patch.target.scope));
                continue;
            }
            if (!def->writable) {
                result.issues.push_back({key, "unsupported_setting_mutation", std::string("This ") + scope_name(patch.target.scope) +
                                                                                  " setting is read-only."});
                continue;
            }
            if (def->type == "integer" &&
                (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || std::stod(text) > *def->max)) {
                result.issues.push_back({key, "invalid_setting_value", "Value is outside the setting's type or bounds.", {}, {}, def->min, def->max});
                continue;
            }
            next[key] = text;
        }
        if (!result.issues.empty())
            return result;
        const auto& all = patch.target.scope == SettingsScope::Filament ? filaments : printers;
        if (!patch.persist_as) {
            if (!is_selected)
                result.issues.push_back({"", "not_selected", "\"" + patch.target.preset + "\" is not the preset being edited, so a "
                                                                                            "change to it is saved: pass persistAs."});
        } else if (*patch.persist_as == patch.target.preset) {
            if (found->system)
                result.issues.push_back({"", "read_only_preset", "\"" + patch.target.preset + "\" comes with OrcaSlicer and cannot be "
                                                                                                "overwritten. Save the change as a copy: "
                                                                                                "pass persistAs \"" + patch.target.preset + " - Copy\".",
                                         {}, {patch.target.preset + " - Copy"}});
        } else if (all.count(*patch.persist_as)) {
            result.issues.push_back({"", "name_taken", "Another preset is already named \"" + *patch.persist_as + "\".", {},
                                     {patch.target.preset + " - Copy (2)"}});
        }
        if (patch.persist_as && !is_selected) {
            if (patch.target.scope == SettingsScope::Printer && *patch.persist_as != patch.target.preset)
                result.issues.push_back({"", "not_selected", "A copy is saved only of the printer in use."});
            if (patch.target.scope == SettingsScope::Filament) {
                const FakePreset& editing = filaments.at(selected_filament);
                if (std::find(filament_slots.begin(), filament_slots.end(), patch.target.preset) == filament_slots.end())
                    result.issues.push_back({"", "not_in_project", "\"" + patch.target.preset + "\" is not one of the project's filaments."});
                else if (editing.values != editing.saved)
                    result.issues.push_back({"", "unsaved_edits", "The filament being edited has unsaved changes."});
            }
        }
        for (const auto& [key, text] : patch.changes) {
            if (next.at(key) != current.at(key))
                result.changes.push_back({key, current.at(key), next.at(key)});
            else
                result.warnings.push_back({key, "unchanged", "The setting already has this value."});
        }
        result.valid = result.issues.empty();
        return result;
    }

    CommandResult apply_preset(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed, SettingsPreview& applied)
    {
        applied = preview_preset(patch);
        if (const FakePreset* found = preset(patch.target)) {
            const auto& current = selected(patch.target) ? found->values : found->saved;
            for (const auto& change : confirmed)
                if (current.count(change.key) == 0 || current.at(change.key) != change.before)
                    return CommandResult::failure(WorkspaceError::StaleSettings, "A confirmed setting changed. Read and preview again.");
        }
        if (!applied.valid)
            return CommandResult::failure(WorkspaceError::InvalidSettings, "The settings patch is invalid.");
        const auto actual = settings_confirmation(applied);
        if (actual.size() != confirmed.size() || !std::equal(actual.begin(), actual.end(), confirmed.begin(),
            [](const auto& a, const auto& b) { return a.key == b.key && a.before == b.before && a.after == b.after; }))
            return CommandResult::failure(WorkspaceError::StaleSettings, "The patch no longer matches the approved preview.");
        if (actual.empty())
            return CommandResult::failure(WorkspaceError::NoChange, "All requested values are unchanged.");
        const bool   filament = patch.target.scope == SettingsScope::Filament;
        auto&        all      = filament ? filaments : printers;
        std::string& editing  = filament ? selected_filament : selected_printer;
        FakePreset&  target   = all.at(patch.target.preset);
        if (!selected(patch.target) && !filament) {
            // A printer not in use, saved in place.
            for (const auto& change : actual)
                target.values[change.key] = target.saved[change.key] = change.after;
            applied.saved_as = patch.target.preset;
            return CommandResult::success();
        }
        editing = patch.target.preset; // a filament in the project is opened in its tab first
        for (const auto& change : actual)
            target.values[change.key] = change.after;
        if (patch.persist_as) {
            if (*patch.persist_as == patch.target.preset) {
                target.saved = target.values;
            } else {
                // Save as: the copy holds the edits and takes the original's
                // place, which goes back to what it was saved as.
                all[*patch.persist_as] = {target.values, target.values, false};
                target.values          = target.saved;
                if (filament)
                    std::replace(filament_slots.begin(), filament_slots.end(), patch.target.preset, *patch.persist_as);
                editing = *patch.persist_as;
            }
            applied.saved_as = *patch.persist_as;
        }
        const FakePreset& now = all.at(editing);
        applied.preset_dirty  = now.values != now.saved;
        return CommandResult::success();
    }

    void add(std::string key, std::string type, std::string label, std::string value,
             std::optional<double> min = {}, std::optional<double> max = {}, std::string unit = {})
    {
        values[key] = std::move(value);
        definitions.push_back({key, type, label, "Process", label, unit, min, max, {}, {}, writable_setting(key)});
    }

    const SettingDefinition* definition(const std::string& key, SettingsScope scope = SettingsScope::Process) const
    {
        const auto& all   = scope_definitions(scope);
        const auto  found = std::find_if(all.begin(), all.end(), [&](const auto& def) { return def.key == key; });
        return found == all.end() ? nullptr : &*found;
    }

    SettingIssue unknown(const std::string& key, SettingsScope scope = SettingsScope::Process) const
    {
        if (scope != SettingsScope::Printer && key == "nozzle_diameter")
            return {key, "unsupported_scope", std::string("This key is not a ") + scope_name(scope) + " setting."};
        return {key, "unknown_setting", "Unknown setting.", {}, setting_suggestions(key, scope_definitions(scope))};
    }
};

} // namespace Slic3r::GUI::JusPrin::Workspace
