#pragma once

// Building an action's ActivityTitle (ToolExecution.hpp). Each title is one
// marked sentence with placeholders, so a translator sees it whole. GUI-free.

#include "ToolExecution.hpp"
#include "ToolResults.hpp"

#include "libslic3r/format.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

namespace title_detail {
// A placeholder's value in each text: an ActivityTitle gives its own English
// or shown text, anything else is the same in both.
inline const std::string& english_of(const ActivityTitle& title) { return title.english; }
inline const std::string& shown_of(const ActivityTitle& title) { return title.shown; }
template<typename T> const T& english_of(const T& value) { return value; }
template<typename T> const T& shown_of(const T& value) { return value; }
} // namespace title_detail

// One sentence marked with L("..."), its placeholders filled from `args`.
template<typename... Args> ActivityTitle title_of(const char* english, const Args&... args)
{
    return {Slic3r::format(english, title_detail::english_of(args)...),
            Slic3r::format(translated(english), title_detail::shown_of(args)...)};
}

// A sentence whose words follow the count `n`, which is its %1%. The catalog
// script extracts the two forms as one plural entry.
template<typename... Args>
ActivityTitle plural_title(const char* singular, const char* plural, std::size_t n, const Args&... args)
{
    const unsigned count = static_cast<unsigned>(n);
    return {Slic3r::format(count == 1 ? singular : plural, count, title_detail::english_of(args)...),
            Slic3r::format(translated_plural(singular, plural, count), count, title_detail::shown_of(args)...)};
}

// Text that is the same in both, such as a file path or an object's name.
inline ActivityTitle same_title(const std::string& text) { return {text, text}; }

// The parts of a list, each already a title, joined by commas.
inline ActivityTitle joined_titles(const std::vector<ActivityTitle>& parts, const char* separator = ", ")
{
    ActivityTitle result;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        result.english += (index == 0 ? "" : separator) + parts[index].english;
        result.shown += (index == 0 ? "" : separator) + parts[index].shown;
    }
    return result;
}

// Cut to the label limit with "..." at the end, never inside a UTF-8 code
// point: a translated title runs longer in bytes than the English.
inline std::string bounded_title(std::string text)
{
    if (text.size() <= kToolLabelLimit)
        return text;
    std::size_t end = kToolLabelLimit - 3;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    text.resize(end);
    return text + "...";
}

inline void set_title(ToolActivity& activity, const ActivityTitle& title)
{
    activity.title       = bounded_title(title.english);
    activity.shown_title = bounded_title(title.shown);
}

} // namespace Slic3r::GUI::JusPrin::Agent
