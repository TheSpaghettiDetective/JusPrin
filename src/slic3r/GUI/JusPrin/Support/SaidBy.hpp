#pragma once

// The one line that shows another side's own words, such as an AI service's
// or the operating system's, after its name. It only formats; the code that
// calls it has already decided that the failure is the person's.

#include <string>

#include "libslic3r/format.hpp"

// Last: it defines the L marker.
#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

namespace Slic3r::GUI::JusPrin {

// The operating system, as said_by names it.
#if defined(__APPLE__)
inline constexpr const char* kSystemName = "macOS";
#elif defined(_WIN32)
inline constexpr const char* kSystemName = "Windows";
#else
inline constexpr const char* kSystemName = "Linux";
#endif

// `words` stay in the language they came in.
inline std::string said_by(const std::string& name, const std::string& words)
{
    // TRN %1% is an AI provider or the operating system, such as OpenAI or macOS. %2% is what it answered, in its own words and language.
    return Slic3r::format(translated(L("%1% says: “%2%”")), name, words);
}

} // namespace Slic3r::GUI::JusPrin
