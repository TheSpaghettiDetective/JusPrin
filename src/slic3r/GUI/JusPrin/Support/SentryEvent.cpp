#include "SentryEvent.hpp"

#include "slic3r/GUI/JusPrin/Support/ErrorReport.hpp"

#include <sentry.h>

#include <cstddef>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

// Sentry keeps at most 200 characters of a tag's value; cutting at as many
// bytes, on a character boundary, keeps within it.
std::string tag_value(std::string text)
{
    constexpr std::size_t limit = 200;
    if (text.size() > limit) {
        std::size_t end = limit;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            --end;
        text.resize(end);
    }
    return text;
}

// The object under `key`, made when there is none.
sentry_value_t object_at(sentry_value_t parent, const char* key)
{
    sentry_value_t child = sentry_value_get_by_key(parent, key);
    if (sentry_value_get_type(child) != SENTRY_VALUE_TYPE_OBJECT) {
        sentry_value_set_by_key(parent, key, sentry_value_new_object());
        child = sentry_value_get_by_key(parent, key);
    }
    return child;
}

} // namespace

std::optional<SentryDsn> parse_sentry_dsn(const std::string& dsn)
{
    // <scheme>://<key>[:<secret>]@<host>[:<port>][/<path>]/<project>
    static const std::regex shape(R"(^(https?)://([^:@/]+)(?::[^@/]*)?@([^/]+)(/(?:[^/]+/)*)([0-9]+)/?$)");
    std::smatch part;
    if (!std::regex_match(dsn, part, shape))
        return std::nullopt;
    return SentryDsn{part[1].str() + "://" + part[3].str() + part[4].str() + "api/" + part[5].str() + "/envelope/", part[2].str()};
}

std::string sentry_auth_header(const SentryDsn& dsn, const std::string& client)
{
    return "Sentry sentry_version=7, sentry_client=" + client + ", sentry_key=" + dsn.public_key;
}

std::vector<std::string> sentry_fingerprint(const ErrorReport& report)
{
    static const std::regex request_ids(R"( \(request [^()]*\)|"request_id"\s*:\s*"[^"]*")");
    return {report.doing, std::regex_replace(report.error, request_ids, "")};
}

sentry_value_t sentry_event(const ErrorReport& report)
{
    sentry_value_t event;
    if (report.page_event.empty()) {
        event = sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "jusprin.app", report.error.c_str());
    } else {
        event = sentry_value_from_json(report.page_event.data(), report.page_event.size());
    }
    sentry_value_set_by_key(event, "release", sentry_value_new_string(report.app.c_str()));

    sentry_value_t tags = object_at(event, "tags");
    sentry_value_set_by_key(tags, "doing", sentry_value_new_string(tag_value(report.doing).c_str()));
    sentry_value_set_by_key(tags, "os", sentry_value_new_string(tag_value(report.os).c_str()));

    sentry_value_t extra = object_at(event, "extra");
    sentry_value_set_by_key(extra, "doing", sentry_value_new_string(report.doing.c_str()));
    sentry_value_set_by_key(extra, "error", sentry_value_new_string(report.error.c_str()));
    sentry_value_set_by_key(extra, "where", sentry_value_new_string(report.where.c_str()));

    sentry_value_t fingerprint = sentry_value_new_list();
    for (const std::string& part : sentry_fingerprint(report))
        sentry_value_append(fingerprint, sentry_value_new_string(part.c_str()));
    sentry_value_set_by_key(event, "fingerprint", fingerprint);
    return event;
}

} // namespace Slic3r::GUI::JusPrin
