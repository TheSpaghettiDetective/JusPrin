#pragma once

#include "SettingsSupport.hpp"

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
        preset_values = values;
    }

    SettingsReadResult read(const std::vector<std::string>& keys) const
    {
        SettingsReadResult result;
        if (keys.empty() || keys.size() > 32) {
            result.error = SettingIssue{"", "invalid_arguments", "Read 1 to 32 setting keys."};
            return result;
        }
        for (const auto& key : keys) {
            const auto* def = definition(key);
            if (!def) {
                result.unknown_keys.push_back(key);
                result.issues.push_back(unknown(key));
            } else {
                const auto& value = values.at(key);
                const bool dirty = value != preset_values.at(key);
                result.items.push_back({key, value, dirty, dirty, *def});
            }
        }
        return result;
    }

    SettingsPreview preview(const SettingsPatch& patch) const
    {
        SettingsPreview result;
        result.process_preset = "Fixture process";
        if (patch.changes.empty() || patch.changes.size() > 32) {
            result.issues.push_back({"", "invalid_arguments", "A patch must contain 1 to 32 settings."});
            return result;
        }
        auto next = values;
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
        if (next.at("sparse_infill_density") != "0%" && next.at("sparse_infill_pattern") == "line" && next.at("fill_multiline") != "1") {
            result.dependencies.push_back({"fill_multiline", values.at("fill_multiline"), "1"});
            result.warnings.push_back({"fill_multiline", "normalized_dependency", "Orca resets multiline infill to 1 for this pattern."});
        }
        for (const auto& [key, text] : patch.changes) {
            if (!definition(key) || !writable_setting(key))
                continue;
            if (next.at(key) != values.at(key))
                result.changes.push_back({key, values.at(key), next.at(key)});
            else
                result.warnings.push_back({key, "unchanged", "The setting already has this value."});
        }
        result.valid = result.issues.empty();
        return result;
    }

    CommandResult apply(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed, SettingsPreview& applied)
    {
        applied = preview(patch);
        for (const auto& change : confirmed)
            if (values.count(change.key) == 0 || values.at(change.key) != change.before)
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
            values[change.key] = change.after;
        for (const auto& change : applied.dependencies)
            applied.warnings.push_back({change.key, "normalized", "Orca normalized this dependent setting to " + change.after + "."});
        return CommandResult::success();
    }

    std::vector<SettingDefinition> definitions;
    std::map<std::string, std::string> values, preset_values;

private:
    void add(std::string key, std::string type, std::string label, std::string value,
             std::optional<double> min = {}, std::optional<double> max = {}, std::string unit = {})
    {
        values[key] = std::move(value);
        definitions.push_back({key, type, label, "Process", label, unit, min, max, {}, {}, writable_setting(key)});
    }

    const SettingDefinition* definition(const std::string& key) const
    {
        const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto& def) { return def.key == key; });
        return found == definitions.end() ? nullptr : &*found;
    }

    SettingIssue unknown(const std::string& key) const
    {
        if (key == "nozzle_diameter")
            return {key, "unsupported_scope", "This is a printer setting, not a process setting."};
        return {key, "unknown_setting", "Unknown setting.", {}, setting_suggestions(key, definitions)};
    }
};

} // namespace Slic3r::GUI::JusPrin::Workspace
