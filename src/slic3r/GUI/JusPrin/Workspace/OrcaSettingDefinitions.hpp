#pragma once

#include "SettingsSupport.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <limits>

// What OrcaSlicer defines about each setting, read from libslic3r alone so
// that code and tests with no GUI can ask.
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

inline SettingDefinition setting_definition(const std::string& key, SettingsScope scope = SettingsScope::Process)
{
    const auto& def = *print_config_def.get(key);
    SettingDefinition result{key, setting_type(def.type), def.full_label.empty() ? def.label : def.full_label,
        def.category, def.tooltip, def.sidetext, {}, {}, def.enum_values, def.enum_labels, writable_setting(key, scope) && !def.readonly};
    // An open enum (the interface layer counts) takes any number; its list is
    // a set of shortcuts, not the allowed values.
    if (def.gui_type == ConfigOptionDef::GUIType::i_enum_open || def.gui_type == ConfigOptionDef::GUIType::f_enum_open) {
        result.enum_values.clear();
        result.enum_labels.clear();
    }
    if (def.min != -std::numeric_limits<float>::max()) result.min = def.min;
    if (def.max != std::numeric_limits<float>::max()) result.max = def.max;
    return result;
}

// The keys an object can override: ObjectList's get_options for a whole
// object, region options plus object options.
inline bool object_setting(const std::string& key)
{
    static const PrintRegionConfig region;
    static const PrintObjectConfig object;
    return region.optptr(key) != nullptr || object.optptr(key) != nullptr;
}

// The keys a scope's preset holds, as Orca lists them for that preset type.
inline const std::vector<std::string>& scope_options(SettingsScope scope)
{
    return scope == SettingsScope::Filament ? Preset::filament_options() :
           scope == SettingsScope::Printer  ? Preset::printer_options() :
                                              Preset::print_options();
}

inline bool has_scope_setting(const std::string& key, SettingsScope scope)
{
    const auto& keys = scope_options(scope);
    return print_config_def.get(key) && std::find(keys.begin(), keys.end(), key) != keys.end() &&
           (scope != SettingsScope::Object || object_setting(key));
}

inline std::vector<SettingDefinition> scope_definitions(SettingsScope scope)
{
    std::vector<SettingDefinition> result;
    for (const auto& key : scope_options(scope))
        if (has_scope_setting(key, scope)) result.push_back(setting_definition(key, scope));
    return result;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
