#pragma once

// Translation for the JusPrin files that also build without wxWidgets. The
// GUI-free tests compile AgentHost, the provider agents and the cloud account
// straight into a test binary, so those files cannot use
// slic3r/GUI/I18N.hpp. Such a file marks the text a person reads with
// L("...") and translates it where it is shown with translated(). The app
// installs the translation when it starts (Agent/AgentWebView.cpp); until
// then, and in the tests, the English comes back unchanged.
//
// libslic3r's I18N::translate_fn is the same idea, but it needs libslic3r
// linked, which the GUI-free tests are not, and slic3r files may not include
// it.
//
// A sentence whose words follow a count is marked and translated at once with
// translated_plural("%1% file", "%1% files", n), which the catalog script
// extracts as a plural entry.
//
// Include this header after the file's other includes: L is a macro.

#include <string>

#ifndef L
#define L(s) s
#endif

namespace Slic3r::GUI::JusPrin {

using Translator = std::string (*)(const char* english);

inline Translator& installed_translator()
{
    static Translator translator = nullptr;
    return translator;
}

inline void install_translator(Translator translator) { installed_translator() = translator; }

using PluralTranslator = std::string (*)(const char* singular, const char* plural, unsigned n);

inline PluralTranslator& installed_plural_translator()
{
    static PluralTranslator translator = nullptr;
    return translator;
}

inline void install_plural_translator(PluralTranslator translator) { installed_plural_translator() = translator; }

// The person's language for English text marked with L("..."), or the English
// itself while no translation is installed.
inline std::string translated(const char* english)
{
    const Translator translator = installed_translator();
    return translator != nullptr ? translator(english) : std::string(english);
}

// The form of a sentence for the count n, in the person's language, or the
// English singular for 1 and plural otherwise while no translation is
// installed.
inline std::string translated_plural(const char* singular, const char* plural, unsigned n)
{
    const PluralTranslator translator = installed_plural_translator();
    return translator != nullptr ? translator(singular, plural, n) : std::string(n == 1 ? singular : plural);
}

} // namespace Slic3r::GUI::JusPrin
