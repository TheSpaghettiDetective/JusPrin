#pragma once

// Checks of a sliced plate against the print intent: the answers whose words
// carry a limit a slice can be measured against (a time, a weight, a cost).
// The intent's field names are the agent's own, so a limit is recognised by
// the value's words, and the field name only decides which way it points.
// GUI-free.

#include "ProjectStateDocument.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct IntentCheck
{
    std::string field;
    std::string value;
    std::string kind;   // time, weight, or cost
    double      limit{0};
    double      actual{0};
    bool        within{false};
};

inline std::string intent_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

inline std::optional<double> intent_number(const std::string& word)
{
    static const std::vector<std::string> words{"zero", "one", "two",   "three", "four",  "five",  "six",
                                                "seven", "eight", "nine", "ten", "eleven", "twelve"};
    if (word.empty())
        return std::nullopt;
    if (std::isdigit(static_cast<unsigned char>(word.front())) || word.front() == '.')
        return std::stod(word);
    if (word == "a" || word == "an")
        return 1.0;
    if (word == "half a" || word == "half an")
        return 0.5;
    const auto found = std::find(words.begin(), words.end(), word);
    return found == words.end() ? std::nullopt : std::optional<double>(double(found - words.begin()));
}

// "under five hours", "2 h 30 min", "90 minutes", "an hour and a half".
inline std::optional<double> intent_seconds(const std::string& text)
{
    static const std::regex part(
        R"((\d+(?:\.\d+)?|\.\d+|half an?|an?|zero|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve)\s*(hours?|hrs?|h|minutes?|mins?|m)\b)");
    const std::string lower = intent_lower(text);
    double            total = 0;
    bool              found = false;
    for (std::sregex_iterator match(lower.begin(), lower.end(), part), end; match != end; ++match) {
        const auto number = intent_number((*match)[1].str());
        if (!number)
            continue;
        const char unit = (*match)[2].str().front();
        total += *number * (unit == 'h' ? 3600 : 60);
        found = true;
    }
    if (found && lower.find("and a half") != std::string::npos)
        total += 1800;
    return found ? std::optional<double>(total) : std::nullopt;
}

inline std::optional<double> intent_grams(const std::string& text)
{
    static const std::regex weight(R"((\d+(?:\.\d+)?)\s*(kg|kilograms?|g|grams?)\b)");
    const std::string lower = intent_lower(text);
    std::smatch       match;
    if (!std::regex_search(lower, match, weight))
        return std::nullopt;
    return std::stod(match[1].str()) * (match[2].str().front() == 'k' ? 1000 : 1);
}

inline std::optional<double> intent_money(const std::string& text)
{
    // $, and the euro and pound signs as their UTF-8 bytes.
    static const std::regex before(std::string(R"((?:\$|)") + "\xE2\x82\xAC|\xC2\xA3" + R"()\s*(\d+(?:\.\d+)?))");
    static const std::regex after(R"((\d+(?:\.\d+)?)\s*(dollars?|usd|euros?|eur|pounds?|gbp|cents?)\b)");
    const std::string lower = intent_lower(text);
    std::smatch       match;
    if (std::regex_search(lower, match, after))
        return std::stod(match[1].str()) / (match[2].str().rfind("cent", 0) == 0 ? 100 : 1);
    if (std::regex_search(lower, match, before))
        return std::stod(match[1].str());
    return std::nullopt;
}

// A limit reads as a ceiling unless it says otherwise ("at least").
inline bool intent_is_floor(const std::string& text)
{
    const std::string lower = intent_lower(text);
    return lower.find("at least") != std::string::npos || lower.find("minimum") != std::string::npos ||
           lower.find("no less than") != std::string::npos;
}

// Each answered field with a measurable limit, measured; the others by name.
inline std::vector<IntentCheck> check_intent(const std::vector<IntentField>& fields, double print_seconds, double grams,
                                             std::optional<double> cost, std::vector<std::string>& unchecked)
{
    std::vector<IntentCheck> checks;
    for (const IntentField& field : fields) {
        if (!field.answered())
            continue;
        const bool        floor = intent_is_floor(field.value);
        const auto measure = [&](const char* kind, double limit, double actual) {
            checks.push_back({field.field, field.value, kind, limit, actual, floor ? actual >= limit : actual <= limit});
        };
        if (const auto seconds = intent_seconds(field.value))
            measure("time", *seconds, print_seconds);
        else if (const auto limit = intent_grams(field.value))
            measure("weight", *limit, grams);
        else if (const auto money = intent_money(field.value); money && cost)
            measure("cost", *money, *cost);
        else
            unchecked.push_back(field.field);
    }
    return checks;
}

} // namespace Slic3r::GUI::JusPrin::Agent
