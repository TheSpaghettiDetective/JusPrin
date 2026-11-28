#pragma once

// A GitHub issue for the JusPrin developers, opened in the person's browser
// with a title, a body and the app's own facts filled in. Nothing is filed:
// the person reads the page and submits it with their own GitHub account. The
// Help menu's Report a problem uses the link; the AI's issue_report_open, in
// the project chat and the printer chat, uses all of it.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin {

// The GitHub page for a new JusPrin issue, filled in with `title` and `body`
// and ending in the app's own `footer`. A body that would take the link past
// kIssueLinkLimit bytes is cut, never the footer, and `shortened` says so.
inline constexpr std::size_t kIssueLinkLimit = 8000;
struct IssueLink
{
    std::string url;
    bool        shortened{false};
};

inline std::string percent_encoded(const std::string& text)
{
    static const char* const digits = "0123456789ABCDEF";
    std::string              out;
    for (const unsigned char c : text)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
            out += static_cast<char>(c);
        else
            out += {'%', digits[c >> 4], digits[c & 15]};
    return out;
}

inline IssueLink issue_link(const std::string& title, const std::string& body, const std::string& footer)
{
    static const std::string page   = "https://github.com/TheSpaghettiDetective/JusPrin/issues/new";
    static const std::string marker = percent_encoded("\n\n(Cut short to fit the link.)");
    const std::string        start  = page + "?title=" + percent_encoded(title) + "&body=";
    const std::string        end    = percent_encoded(footer);
    const std::string        whole  = percent_encoded(body);
    if (start.size() + whole.size() + end.size() <= kIssueLinkLimit)
        return {start + whole + end, false};
    if (start.size() + marker.size() + end.size() > kIssueLinkLimit)
        throw std::logic_error("An issue's title and footer leave no room for its body");
    // Whole characters, so the cut never splits one.
    const std::size_t room = kIssueLinkLimit - start.size() - marker.size() - end.size();
    std::string       kept;
    for (std::size_t from = 0; from < body.size();) {
        std::size_t next = from + 1;
        while (next < body.size() && (static_cast<unsigned char>(body[next]) & 0xC0) == 0x80)
            ++next;
        const std::string character = percent_encoded(body.substr(from, next - from));
        if (kept.size() + character.size() > room)
            break;
        kept += character;
        from = next;
    }
    return {start + kept + marker + end, true};
}

// An issue holds neither an access code nor an API key. A credential never
// reaches the chat; this takes out what a model may copy into an issue
// anyway, and a printer's serial, which an issue has no need of: a key's own
// shape, and any run of six or more letters and digits with a digit in it. A
// code of letters alone reads as a word, which is why the person reads the
// page before submitting it.
inline std::string without_secrets(const std::string& text)
{
    static const std::regex key("sk-[A-Za-z0-9_-]{6,}");
    static const std::regex token("[A-Za-z0-9]{6,}");
    const std::string       no_keys = std::regex_replace(text, key, "[hidden]");
    std::string             out;
    std::size_t             from = 0;
    for (auto it = std::sregex_iterator(no_keys.begin(), no_keys.end(), token); it != std::sregex_iterator(); ++it) {
        const std::string word = it->str();
        out += no_keys.substr(from, it->position() - from);
        out += std::any_of(word.begin(), word.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ? "[hidden]" : word;
        from = it->position() + word.size();
    }
    return out + no_keys.substr(from);
}

// What an issue on GitHub, which anyone can read, holds of the person's own:
// none of the `known` values the app names as theirs (a printer's serial and
// address, a project's name and folder), nothing that reads as an IP address,
// and nothing without_secrets takes out.
inline std::string for_public(std::string text, const std::vector<std::string>& known)
{
    static const std::regex address("\\b(?:[0-9]{1,3}\\.){3}[0-9]{1,3}\\b");
    for (const std::string& value : known)
        for (std::size_t at = value.empty() ? std::string::npos : text.find(value); at != std::string::npos;
             at = text.find(value, at + 8))
            text.replace(at, value.size(), "[hidden]");
    return without_secrets(std::regex_replace(text, address, "[hidden]"));
}

// What the chat asking for an issue knows: the footer of the app's own facts,
// which the model may have wrong or left out, and the values to take out.
struct IssueContext
{
    std::string              footer;
    std::vector<std::string> known;
};

// issue_report_open's work, wherever it is offered: opens the page with the
// model's `arguments` (title and body) and returns the tool's result, or
// nothing when no browser opened (kIssueNoBrowser says so).
inline constexpr const char* kIssueNoBrowser = "The app could not open the person's browser, so no issue page is open.";
inline std::optional<nlohmann::json> open_issue_page(const nlohmann::json& arguments, const IssueContext& context,
                                                     const std::function<bool(const std::string&)>& open)
{
    const IssueLink link = issue_link(for_public(arguments.at("title").get<std::string>(), context.known),
                                      for_public(arguments.at("body").get<std::string>(), context.known), context.footer);
    if (!open(link.url))
        return std::nullopt;
    std::string message = "The new issue page is open in the person's browser, filled in. Nothing has been filed: it is filed "
                          "only when they press Submit on it, signed in to GitHub.";
    if (link.shortened)
        message += " The body was too long for the link, so its end was cut; they can add the rest on the page.";
    return nlohmann::json{{"state", "opened"}, {"shortened", link.shortened}, {"message", message}};
}

} // namespace Slic3r::GUI::JusPrin
