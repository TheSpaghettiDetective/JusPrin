#pragma once

#include "Workspace.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <numeric>
#include <string_view>

namespace Slic3r::GUI::JusPrin::Workspace {

inline bool writable_setting(const std::string& key)
{
    constexpr std::string_view keys[] = {"layer_height", "wall_loops", "sparse_infill_density", "sparse_infill_pattern",
                                         "top_shell_layers", "bottom_shell_layers", "brim_width"};
    return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
}

inline std::string settings_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return text;
}

inline SettingsSearchResult search_setting_definitions(const std::vector<SettingDefinition>& definitions,
                                                       const SettingsQuery& query)
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
    const std::string text = settings_lower(query.text);
    std::vector<std::pair<int, const SettingDefinition*>> matches;
    for (const auto& def : definitions) {
        const std::string key = settings_lower(def.key);
        int rank = key == text ? 0 : key.find(text) == 0 ? 1 :
                   settings_lower(def.label).find(text) != std::string::npos ? 2 :
                   settings_lower(def.description).find(text) != std::string::npos ? 3 : 4;
        if (rank < 4)
            matches.emplace_back(rank, &def);
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

inline std::vector<std::string> setting_suggestions(const std::string& key,
                                                    const std::vector<SettingDefinition>& definitions)
{
    // Edit distance handles misspellings without maintaining a second alias catalog.
    std::vector<std::pair<std::size_t, std::string>> ranked;
    const std::string needle = settings_lower(key);
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
        if (row.back() <= std::max<std::size_t>(2, needle.size() / 3))
            ranked.emplace_back(row.back(), def.key);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<std::string> result;
    for (std::size_t i = 0; i < std::min<std::size_t>(3, ranked.size()); ++i)
        result.push_back(ranked[i].second);
    return result;
}

inline std::vector<SettingChange> settings_confirmation(const SettingsPreview& preview)
{
    auto result = preview.changes;
    result.insert(result.end(), preview.dependencies.begin(), preview.dependencies.end());
    return result;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
