#pragma once

// One spelling for a moment in time wherever JusPrin writes one down: saved
// state, printer facts, and tool results. ISO 8601 in UTC, to the second,
// because a reader -- a person or a model -- can read it without converting
// anything, and a raw epoch count it cannot.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Workspace {

inline std::string utc_timestamp(std::chrono::system_clock::time_point when)
{
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

inline std::string utc_now() { return utc_timestamp(std::chrono::system_clock::now()); }

// The inverse of utc_timestamp: a timestamp it wrote, back to a time point.
// Anything else -- another format, a local-time suffix, garbage -- is none.
inline std::optional<std::chrono::system_clock::time_point> parse_utc_timestamp(const std::string& text)
{
    std::tm utc{};
    char    tail = '\0';
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &utc.tm_year, &utc.tm_mon, &utc.tm_mday, &utc.tm_hour,
                    &utc.tm_min, &utc.tm_sec, &tail) != 7 ||
        tail != 'Z')
        return std::nullopt;
    utc.tm_year -= 1900;
    utc.tm_mon -= 1;
#ifdef _WIN32
    const std::time_t seconds = _mkgmtime(&utc);
#else
    const std::time_t seconds = timegm(&utc);
#endif
    if (seconds == std::time_t(-1))
        return std::nullopt;
    return std::chrono::system_clock::from_time_t(seconds);
}

} // namespace Slic3r::GUI::JusPrin::Workspace
