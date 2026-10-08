#pragma once

#include "Workspace.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

// How a setting's value reads to a person, worked out from the setting's own
// definition -- its type, unit and enum labels -- and from nothing else. No
// config key is named here, so a setting OrcaSlicer adds tomorrow reads by
// the same rules as the ones it has today. Values are OrcaSlicer's own
// serialization (ConfigOption::serialize).

// Text longer than this, or holding a line break, is described, not quoted.
constexpr std::size_t kQuotableSettingText = 32;

namespace setting_display {

// The strings in a serialized ConfigOptionStrings: joined with ';', and quoted
// when one holds a space or an escape. A single ConfigOptionString is the same
// escapes with no quotes and no separator.
inline std::vector<std::string> unescaped(std::string_view text, bool list)
{
    std::vector<std::string> result(1);
    bool quoted = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '\\' && index + 1 < text.size()) {
            const char escaped = text[++index];
            result.back() += escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped;
        } else if (list && c == '"') {
            quoted = !quoted;
        } else if (list && c == ';' && !quoted) {
            result.emplace_back();
        } else {
            result.back() += c;
        }
    }
    return result;
}

// G-code and notes are not values to read in a row: they are said to exist.
inline std::string text(std::string value)
{
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), value.end());
    if (value.empty()) return "Empty";
    const auto lines = 1 + std::count(value.begin(), value.end(), '\n');
    if (lines > 1) return std::to_string(lines) + " lines";
    return value.size() <= kQuotableSettingText ? value : std::to_string(value.size()) + " characters";
}

// The unit a number carries. "mm or %" names both forms a value may take;
// a value that reaches here is not the percentage.
inline std::string_view unit_of(const SettingDefinition& definition)
{
    std::string_view unit = definition.unit;
    if (const auto either = unit.rfind(" or %"); either != std::string_view::npos) unit = unit.substr(0, either);
    return unit;
}

inline std::string with_unit(const std::string& number, std::string_view unit)
{
    if (unit.empty()) return number;
    const bool attached = unit == "%" || unit == "\xC2\xB0"; // the degree sign
    return number + (attached ? "" : " ") + std::string(unit);
}

inline std::string element(const SettingDefinition& definition, std::string_view type, const std::string& value)
{
    // A nullable entry that defers to another preset's value.
    if (value == "nil") return "Not set";
    if (type == "boolean") return value == "1" ? "On" : "Off";
    if (type == "enum") {
        const auto found = std::find(definition.enum_values.begin(), definition.enum_values.end(), value);
        const std::size_t index = found - definition.enum_values.begin();
        if (index < definition.enum_labels.size()) return definition.enum_labels[index];
        std::string words = value;
        std::replace(words.begin(), words.end(), '_', ' ');
        return words;
    }
    const bool percentage = !value.empty() && value.back() == '%';
    if (type == "percent" || (type == "float_or_percent" && percentage)) return percentage ? value : value + "%";
    if (type == "float" || type == "integer" || type == "float_or_percent") return with_unit(value, unit_of(definition));
    return value;
}

} // namespace setting_display

inline std::string display_value(const SettingDefinition& definition, const std::string& value)
{
    using namespace setting_display;
    std::string_view type = definition.type;
    const bool vector = type.size() > 2 && type.substr(type.size() - 2) == "[]";
    if (vector) type.remove_suffix(2);
    if (value.empty()) return "Empty";

    if (type == "string") {
        const std::vector<std::string> items = unescaped(value, vector);
        if (items.size() == 1) return text(items.front());
        std::string joined;
        for (const std::string& item : items) joined += (joined.empty() ? "" : ", ") + item;
        const bool quotable = joined.size() <= kQuotableSettingText && joined.find('\n') == std::string::npos;
        return quotable ? joined : std::to_string(items.size()) + " entries";
    }
    if (!vector) return element(definition, type, value);

    std::vector<std::string> parts(1);
    for (const char c : value)
        if (c == ',') parts.emplace_back(); else parts.back() += c;
    // A shape is not a value to read in a row either.
    if (type == "point" || type == "point_groups" || type == "integer_groups")
        return std::to_string(parts.size()) + (parts.size() == 1 ? " point" : " points");
    // One value per filament or extruder. When they agree it is one fact.
    if (std::all_of(parts.begin(), parts.end(), [&parts](const std::string& part) { return part == parts.front(); }))
        return element(definition, type, parts.front());
    std::string joined;
    for (const std::string& part : parts) joined += (joined.empty() ? "" : ", ") + element(definition, type, part);
    return joined;
}

// "0 → 8 mm": a change between two values of one setting. The unit is said
// once when both sides carry it.
inline std::string display_change(const SettingDefinition& definition, const std::string& from, const std::string& to)
{
    std::string before = display_value(definition, from);
    const std::string after = display_value(definition, to);
    const std::string unit = " " + std::string(setting_display::unit_of(definition));
    const auto ends_with_unit = [&unit](const std::string& text) {
        return text.size() > unit.size() && text.compare(text.size() - unit.size(), unit.size(), unit) == 0;
    };
    if (unit.size() > 1 && before.find(',') == std::string::npos && ends_with_unit(before) && ends_with_unit(after))
        before.erase(before.size() - unit.size());
    return before + " \xE2\x86\x92 " + after;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
