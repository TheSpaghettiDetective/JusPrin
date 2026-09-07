#pragma once

// The ISO 4217 currency of the machine's *regional* settings, which is not the
// same thing as its display language: a person can read English and be paid in
// euros. OrcaSlicer has no currency concept of its own -- filament_cost's unit
// is literally "money/kg" -- so this is the only way to say what a cost is
// denominated in, and it comes from the one place that knows: the OS.
//
// Empty means the OS did not tell us. Callers must treat that as "no currency"
// and drop the money rather than guess one.

#include <string>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <clocale>
#include <langinfo.h>
#endif

namespace Slic3r::GUI::JusPrin::Workspace {

inline std::string host_currency_code()
{
#if defined(__APPLE__)
    CFLocaleRef locale = CFLocaleCopyCurrent();
    if (locale == nullptr)
        return {};
    std::string result;
    if (const auto code = static_cast<CFStringRef>(CFLocaleGetValue(locale, kCFLocaleCurrencyCode))) {
        char buffer[8] = {0};
        if (CFStringGetCString(code, buffer, sizeof buffer, kCFStringEncodingUTF8))
            result = buffer;
    }
    CFRelease(locale);
    return result;

#elif defined(_WIN32)
    wchar_t buffer[8] = {0};
    // LOCALE_SINTLSYMBOL is the ISO 4217 code; LOCALE_SCURRENCY would be the
    // symbol, which the page can derive from the code and its own locale.
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SINTLSYMBOL, buffer,
                        static_cast<int>(std::size(buffer))) <= 0)
        return {};
    std::string result;
    for (wchar_t c : buffer) {
        if (c == L'\0') break;
        if (c < 128) result.push_back(static_cast<char>(c));
    }
    return result;

#else
    // nl_langinfo reads the process's LC_MONETARY, which is "C" until something
    // sets it from the environment. Ask for the environment's setting for this
    // one category only, so the query cannot disturb number or date parsing
    // anywhere else in the process.
    const char* previous = std::setlocale(LC_MONETARY, nullptr);
    const std::string saved = previous != nullptr ? previous : "C";
    std::string result;
    if (std::setlocale(LC_MONETARY, "") != nullptr) {
        // INT_CURR_SYMBOL is "USD " -- the ISO code plus a separator character.
        if (const char* symbol = nl_langinfo(INT_CURR_SYMBOL); symbol != nullptr)
            for (const char* c = symbol; *c != '\0' && result.size() < 3; ++c)
                if (*c >= 'A' && *c <= 'Z') result.push_back(*c);
    }
    std::setlocale(LC_MONETARY, saved.c_str());
    return result;
#endif
}

} // namespace Slic3r::GUI::JusPrin::Workspace
