#pragma once

#include <string>

#include <wx/string.h>

namespace Slic3r::GUI::JusPrin {

// Adds the app's interface language to a JusPrin page's URL as ?lang=<code>
// (or &lang=), the code its catalogs use, such as de_DE or zh_CN. The page
// picks its translations from it (AgentUI/src/i18n.tsx). The language
// changes only with a restart, so the URL is the whole channel.
wxString with_page_language(const wxString& url);

// The app's interface language as a language tag such as "de-DE" or "zh-CN",
// for the Accept-Language header of the requests whose answer carries text
// the person reads: JusPrin's server answers in it (its pages, the account's
// price and delete sentences, the sign-in code email). Empty before the
// language is loaded. Call it on the GUI thread.
std::string server_language();

} // namespace Slic3r::GUI::JusPrin
