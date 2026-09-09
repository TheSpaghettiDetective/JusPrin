// host_currency_code() is the fork's only piece of genuinely per-platform
// code: CoreFoundation on macOS, GetLocaleInfoEx on Windows, nl_langinfo
// elsewhere. Its value is whatever the machine's regional settings say, so a
// test cannot pin it -- but the contract the callers rely on is pinnable, and
// it is the part each platform branch can get wrong in its own way.
//
// The snapshot hands this straight to the Agent page as the denomination of a
// cost (OrcaWorkspaceAdapter::snapshot). A branch that returned the currency
// *symbol*, a code with the separator still attached, or a half-filled buffer
// would produce a plausible-looking string that the page cannot interpret, and
// nothing else in the system would object.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Workspace/HostLocale.hpp"

using Slic3r::GUI::JusPrin::Workspace::host_currency_code;

TEST_CASE("the host currency is an ISO 4217 code or nothing at all", "[jusprin][locale]")
{
    const std::string code = host_currency_code();

    // Empty is the documented "the OS did not tell us", and callers must drop
    // the money rather than guess. Anything else is a code: exactly three
    // characters, all uppercase Latin letters. This is what rules out the
    // symbol ("$", "€"), the POSIX INT_CURR_SYMBOL separator ("USD "), and a
    // buffer that was written past or not written at all.
    if (code.empty()) SUCCEED("the OS reports no currency for this machine");
    else {
        INFO("host_currency_code() returned \"" << code << "\"");
        CHECK(code.size() == 3);
        // Split rather than one && so a failure names the offending character
        // and the bound it broke, per tests/CLAUDE.md.
        for (const char c : code) {
            CAPTURE(c);
            CHECK(c >= 'A');
            CHECK(c <= 'Z');
        }
    }
}

TEST_CASE("the host currency does not change between calls", "[jusprin][locale]")
{
    // snapshot() caches the first answer in a function-local static and reuses
    // it for the life of the process, so a query that is not idempotent would
    // make the displayed currency depend on which call happened to run first.
    // The POSIX branch is the one at risk: it moves LC_MONETARY to read the
    // environment and must restore it, or the second call reads back "C".
    CHECK(host_currency_code() == host_currency_code());
}
