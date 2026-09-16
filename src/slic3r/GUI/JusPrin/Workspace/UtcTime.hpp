#pragma once

// One spelling for a moment in time wherever JusPrin writes one down: saved
// state, the spool store, and tool results. ISO 8601 in UTC, to the second,
// because a reader -- a person or a model -- can read it without converting
// anything, and a raw epoch count it cannot.

#include <chrono>
#include <ctime>
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

} // namespace Slic3r::GUI::JusPrin::Workspace
