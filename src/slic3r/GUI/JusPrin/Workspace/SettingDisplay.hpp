#pragma once

#include "Workspace.hpp"

#include "libslic3r/format.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

namespace Slic3r::GUI::JusPrin::Workspace {

// How a setting's value reads to a person, worked out from the setting's own
// definition -- its type, unit and enum labels -- and from nothing else. No
// config key is named here, so a setting OrcaSlicer adds tomorrow reads by
// the same rules as the ones it has today. Values are OrcaSlicer's own
// serialization (ConfigOption::serialize).

// Text longer than this, or holding a line break, is described, not quoted.
constexpr std::size_t kQuotableSettingText = 32;

// Whose words a value is written in: English, which the model reads, or the
// person's language, which the app's own rows show. Enum labels are
// OrcaSlicer's, which its catalog translates.
enum class ValueWords { English, Shown };

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

// A fixed word, such as "Empty", in `words`.
inline std::string word(const char* english, ValueWords words)
{
    return words == ValueWords::Shown ? translated(english) : std::string(english);
}

// "3 lines": a count and the words that follow it, in `words`. The catalog
// script extracts the two forms as one plural entry.
inline std::string plural_words(const char* singular, const char* plural, std::size_t n, ValueWords words)
{
    const unsigned count = static_cast<unsigned>(n);
    return Slic3r::format(words == ValueWords::Shown ? translated_plural(singular, plural, count) : count == 1 ? singular : plural, count);
}

// G-code and notes are not values to read in a row: they are said to exist.
inline std::string text(std::string value, ValueWords words)
{
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), value.end());
    // TRN A setting's value: it holds no text.
    if (value.empty()) return word(L("Empty"), words);
    const auto lines = 1 + std::count(value.begin(), value.end(), '\n');
    // TRN A setting's text value, such as custom G-code, described by its length.
    if (lines > 1) return plural_words("%1% line", "%1% lines", static_cast<std::size_t>(lines), words);
    // TRN A setting's text value, described by its length.
    return value.size() <= kQuotableSettingText ? value : plural_words("%1% character", "%1% characters", value.size(), words);
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

inline std::string element(const SettingDefinition& definition, std::string_view type, const std::string& value, ValueWords words)
{
    // A nullable entry that defers to another preset's value.
    // TRN A setting's value: it takes the value of another preset.
    if (value == "nil") return word(L("Not set"), words);
    if (type == "boolean") return value == "1" ? word(L("On"), words) : word(L("Off"), words);
    if (type == "enum") {
        const auto found = std::find(definition.enum_values.begin(), definition.enum_values.end(), value);
        const std::size_t index = found - definition.enum_values.begin();
        if (index < definition.enum_labels.size()) return word(definition.enum_labels[index].c_str(), words);
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

inline std::string display_value(const SettingDefinition& definition, const std::string& value,
                                 ValueWords words = ValueWords::English)
{
    using namespace setting_display;
    std::string_view type = definition.type;
    const bool vector = type.size() > 2 && type.substr(type.size() - 2) == "[]";
    if (vector) type.remove_suffix(2);
    if (value.empty()) return word(L("Empty"), words);

    if (type == "string") {
        const std::vector<std::string> items = unescaped(value, vector);
        if (items.size() == 1) return text(items.front(), words);
        std::string joined;
        for (const std::string& item : items) joined += (joined.empty() ? "" : ", ") + item;
        const bool quotable = joined.size() <= kQuotableSettingText && joined.find('\n') == std::string::npos;
        // TRN A setting that holds a list of texts, described by how many.
        return quotable ? joined : plural_words("%1% entry", "%1% entries", items.size(), words);
    }
    if (!vector) return element(definition, type, value, words);

    std::vector<std::string> parts(1);
    for (const char c : value)
        if (c == ',') parts.emplace_back(); else parts.back() += c;
    // A shape is not a value to read in a row either.
    if (type == "point" || type == "point_groups" || type == "integer_groups")
        // TRN A setting that holds a shape, such as the bed's outline, described by its points.
        return plural_words("%1% point", "%1% points", parts.size(), words);
    // One value per filament or extruder. When they agree it is one fact.
    if (std::all_of(parts.begin(), parts.end(), [&parts](const std::string& part) { return part == parts.front(); }))
        return element(definition, type, parts.front(), words);
    std::string joined;
    for (const std::string& part : parts) joined += (joined.empty() ? "" : ", ") + element(definition, type, part, words);
    return joined;
}

// "0 → 8 mm": a change between two values of one setting. The unit is said
// once when both sides carry it.
inline std::string display_change(const SettingDefinition& definition, const std::string& from, const std::string& to,
                                  ValueWords words = ValueWords::English)
{
    std::string before = display_value(definition, from, words);
    const std::string after = display_value(definition, to, words);
    const std::string unit = " " + std::string(setting_display::unit_of(definition));
    const auto ends_with_unit = [&unit](const std::string& text) {
        return text.size() > unit.size() && text.compare(text.size() - unit.size(), unit.size(), unit) == 0;
    };
    if (unit.size() > 1 && before.find(',') == std::string::npos && ends_with_unit(before) && ends_with_unit(after))
        before.erase(before.size() - unit.size());
    return before + " \xE2\x86\x92 " + after;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
