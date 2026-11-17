#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "slic3r/GUI/JusPrin/Support/ErrorReport.hpp"
#include "slic3r/GUI/JusPrin/Support/SentryEvent.hpp"

#include <nlohmann/json.hpp>
#include <sentry.h>

#include <optional>
#include <string>

using namespace Slic3r::GUI::JusPrin;
using nlohmann::json;

namespace {

const ErrorReport kReport{"JusPrin 2.0.1 (OrcaSlicer 2.5.0)", "macOS 26.3", "exporting G-code from Home",
                          "std::filesystem::filesystem_error: filesystem error: in copy_file: Permission denied", "", ""};

// The value as the SDK would write it, released.
json json_of(sentry_value_t value)
{
    char* const text   = sentry_value_to_json(value);
    json        parsed = json::parse(text);
    sentry_free(text);
    sentry_value_decref(value);
    return parsed;
}

// What the app sends for one provider refusal: its own request and the
// provider's request id differ every time.
ErrorReport refusal(const std::string& turn, const std::string& request_id, int status = 400)
{
    ErrorReport report = kReport;
    report.doing       = "answering in the chat";
    report.error = "Anthropic answered HTTP " + std::to_string(status) + " (request " + turn + " #1, its request id " + request_id +
                   "): {\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"message\":\"thinking blocks cannot be "
                   "modified.\"},\"request_id\":\"" +
                   request_id + "\"}";
    return report;
}

// A page's own event as @sentry/browser writes it (WebShared/errorReports.ts).
const json kPageEvent{
    {"platform", "javascript"},
    {"exception",
     {{"values", json::array({{{"type", "TypeError"},
                               {"value", "x is undefined"},
                               {"stacktrace", {{"frames", json::array({{{"filename", "app:///index.js"}, {"lineno", 1}}})}}}}})}}},
    {"tags", {{"page", "home"}}},
};

} // namespace

TEST_CASE("A DSN names the envelope address and the key", "[SentryEvent]")
{
    const std::optional<SentryDsn> plain = parse_sentry_dsn("https://abc123@errors.example.com/16");
    REQUIRE(plain);
    CHECK(plain->envelope_url == "https://errors.example.com/api/16/envelope/");
    CHECK(plain->public_key == "abc123");

    const std::optional<SentryDsn> stand_in = parse_sentry_dsn("http://key:secret@127.0.0.1:8123/prefix/16");
    REQUIRE(stand_in);
    CHECK(stand_in->envelope_url == "http://127.0.0.1:8123/prefix/api/16/envelope/");
    CHECK(stand_in->public_key == "key");

    CHECK(sentry_auth_header(*plain, "jusprin.app/2.0.1") == "Sentry sentry_version=7, sentry_client=jusprin.app/2.0.1, sentry_key=abc123");
}

TEST_CASE("An empty or misshapen DSN names nowhere to send", "[SentryEvent]")
{
    const std::string dsn = GENERATE("", "https://errors.example.com/16", "ftp://k@errors.example.com/16", "https://k@errors.example.com/",
                                     "https://k@errors.example.com/project");
    CAPTURE(dsn);
    CHECK_FALSE(parse_sentry_dsn(dsn));
}

TEST_CASE("The app's own report is one error event filed by release and tags", "[SentryEvent]")
{
    const json event = json_of(sentry_event(kReport));
    CHECK(event["level"] == "error");
    CHECK(event["release"] == kReport.app);
    CHECK(event["message"]["formatted"] == kReport.error);
    CHECK(event["tags"] == json{{"doing", kReport.doing}, {"os", kReport.os}});
    CHECK(event["extra"] == json{{"doing", kReport.doing}, {"error", kReport.error}, {"where", ""}});
}

TEST_CASE("A tag keeps at most 200 bytes, cut on a character boundary", "[SentryEvent]")
{
    ErrorReport report = kReport;
    report.doing       = std::string(199, 'a') + "\xC3\xA9" + "tail"; // "é" straddles byte 200
    const json event   = json_of(sentry_event(report));
    CHECK(event["tags"]["doing"] == std::string(199, 'a'));
    CHECK(event["extra"]["doing"] == report.doing);
}

TEST_CASE("Two reports of one failure are one Sentry issue and keep their ids", "[SentryEvent]")
{
    const ErrorReport first  = refusal("p1-c1-m1-attempt-1", "req_011A");
    const ErrorReport second = refusal("p1-c1-m7-attempt-2", "req_022B");
    const ErrorReport other  = refusal("p1-c1-m9-attempt-1", "req_033C", 500);
    CHECK(sentry_fingerprint(first) == sentry_fingerprint(second));
    CHECK(sentry_fingerprint(first) != sentry_fingerprint(other));
    CHECK(sentry_fingerprint(first)[0] == "answering in the chat");

    const json event = json_of(sentry_event(second));
    CHECK(event["fingerprint"] == json(sentry_fingerprint(second)));
    const std::string message = event["message"]["formatted"];
    CHECK(message.find("p1-c1-m7-attempt-2") != std::string::npos);
    CHECK(message.find("req_022B") != message.rfind("req_022B"));
}

TEST_CASE("A page's report is the page's own event, filed like the app's", "[SentryEvent]")
{
    ErrorReport report = kReport;
    report.doing       = "running the Home page's code";
    report.error       = "TypeError: x is undefined";
    report.page_event  = kPageEvent.dump();

    const json event = json_of(sentry_event(report));
    CHECK(event["platform"] == "javascript");
    CHECK(event["exception"] == kPageEvent["exception"]);
    CHECK(event["release"] == kReport.app);
    CHECK(event["tags"] == json{{"page", "home"}, {"doing", report.doing}, {"os", report.os}});
    CHECK(event["fingerprint"] == json(sentry_fingerprint(report)));
}
