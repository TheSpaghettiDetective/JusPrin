// JusPrin's update check, without the network: which release in GitHub's
// list counts as newer, and when the daily check runs.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Updates/ReleaseFeed.hpp"
#include "slic3r/GUI/JusPrin/Updates/UpdateRecord.hpp"

#include "libslic3r/AppConfig.hpp"

#include <nlohmann/json.hpp>

using namespace Slic3r::GUI::JusPrin::Updates;
using nlohmann::json;

namespace {

json release(const std::string& tag, bool prerelease = false, bool draft = false)
{
    return {{"tag_name", tag},
            {"prerelease", prerelease},
            {"draft", draft},
            {"html_url", "https://github.com/TheSpaghettiDetective/JusPrin/releases/tag/" + tag},
            {"published_at", "2026-11-02T10:00:00Z"},
            {"body", "notes"}};
}

std::optional<Release> newer(const json& releases, const std::string& running)
{
    return newer_release(releases.dump(), running);
}

} // namespace

TEST_CASE("A release tag names its version", "[Updates]")
{
    CHECK(tag_version("v2.1.0") == "2.1.0");
    CHECK(tag_version("jusprin/v1.4.0") == "1.4.0");
    CHECK(tag_version("v2.1.0-beta1") == "2.1.0-beta1");
    CHECK(tag_version("nightly-builds").empty());
}

TEST_CASE("Today's release list offers nothing to JusPrin 2.0.0", "[Updates]")
{
    // The tags on GitHub on 2026-10-22: the rolling nightly pre-release and
    // the JusPrin 1 releases.
    const json today = json::array({release("nightly-builds", true), release("jusprin/v1.4.0"), release("v1.3.0"), release("v0.2.0")});
    CHECK_FALSE(newer(today, "2.0.0"));
}

TEST_CASE("The newest stable release above the running one is offered", "[Updates]")
{
    const json releases = json::array({release("v2.2.0-beta1", true), release("v3.0.0", false, true), release("v2.0.1"),
                                       release("v2.1.0"), release("jusprin/v1.4.0")});
    const auto found = newer(releases, "2.0.0");
    REQUIRE(found);
    CHECK(found->version == "2.1.0");
    CHECK(found->url == "https://github.com/TheSpaghettiDetective/JusPrin/releases/tag/v2.1.0");
    CHECK(found->published == "2026-11-02T10:00:00Z");
    CHECK(found->notes == "notes");
}

TEST_CASE("Release notes are optional and kept short", "[Updates]")
{
    json bare = release("v2.1.0");
    bare["body"] = nullptr;
    const auto found = newer(json::array({bare}), "2.0.0");
    REQUIRE(found);
    CHECK(found->notes.empty());

    json long_notes = release("v2.1.0");
    long_notes["body"] = std::string(kNotesLimit + 100, 'x');
    CHECK(newer(json::array({long_notes}), "2.0.0")->notes.size() == kNotesLimit);

    // "é" is two bytes (C3 A9); the cut must not split it. Written as
    // escapes so the bytes do not depend on the source file's encoding.
    json accented = release("v2.1.0");
    accented["body"] = std::string(kNotesLimit - 1, 'x') + "\xC3\xA9";
    const std::string notes = newer(json::array({accented}), "2.0.0")->notes;
    CHECK(notes.size() == kNotesLimit - 1);
    CHECK_NOTHROW(json(notes).dump());
}

TEST_CASE("The running version itself is not offered", "[Updates]")
{
    CHECK_FALSE(newer(json::array({release("v2.0.0")}), "2.0.0"));
    CHECK_FALSE(newer(json::array({release("v2.0.0-rc1", true)}), "2.0.0"));
}

TEST_CASE("Only a test build is offered test builds", "[Updates]")
{
    const json releases = json::array({release("v2.1.0-beta2", true)});
    CHECK_FALSE(newer(releases, "2.0.0"));
    const auto found = newer(releases, "2.1.0-beta1");
    REQUIRE(found);
    CHECK(found->version == "2.1.0-beta2");

    SECTION("and the stable release that follows its test builds")
    {
        const auto stable = newer(json::array({release("v2.1.0-beta2", true), release("v2.1.0")}), "2.1.0-beta1");
        REQUIRE(stable);
        CHECK(stable->version == "2.1.0");
    }
}

TEST_CASE("Anything but a release list is a failed check", "[Updates]")
{
    CHECK_THROWS_AS(newer_release("<html>Sign in to the Wi-Fi</html>", "2.0.0"), FeedError);
    CHECK_THROWS_AS(newer_release(R"({"message": "API rate limit exceeded"})", "2.0.0"), FeedError);
    CHECK_THROWS_AS(newer_release(R"([{"name": "no tag"}])", "2.0.0"), FeedError);
}

TEST_CASE("The daily check runs a day after the last attempt", "[Updates]")
{
    const std::time_t now = 1'800'000'000;
    const std::time_t hour = 60 * 60;
    UpdateRecord      record;
    CHECK(check_due(record, now, true));

    record.last_attempt = now - hour;
    CHECK_FALSE(check_due(record, now, true));
    CHECK_FALSE(check_due(record, now, false));
    record.last_attempt = now - 25 * hour;
    CHECK(check_due(record, now, false));

    SECTION("and at the next start after a failed one")
    {
        record.last_attempt = now - hour;
        record.last_failed  = true;
        CHECK(check_due(record, now, true));
        CHECK_FALSE(check_due(record, now, false));
    }
    SECTION("and when the clock went back")
    {
        record.last_attempt = now + hour;
        CHECK(check_due(record, now, false));
    }
}

TEST_CASE("What a check found is remembered until JusPrin catches up", "[Updates]")
{
    Slic3r::AppConfig config;
    CHECK(auto_check_enabled(config));
    CHECK(load_record(config, "2.0.0").last_attempt == 0);

    save_success(config, 1000, Release{"2.1.0", "https://example.test/v2.1.0", "2026-11-02T10:00:00Z", "## Fixes"});
    UpdateRecord record = load_record(config, "2.0.0");
    CHECK(record.last_success == 1000);
    CHECK_FALSE(record.last_failed);
    REQUIRE(record.available);
    CHECK(record.available->notes == "## Fixes");
    CHECK(record.available->url == "https://example.test/v2.1.0");

    SECTION("a failed check keeps the release it found earlier")
    {
        save_failure(config, 2000);
        record = load_record(config, "2.0.0");
        CHECK(record.last_attempt == 2000);
        CHECK(record.last_success == 1000);
        CHECK(record.last_failed);
        CHECK(record.available);
    }
    SECTION("once JusPrin 2.1.0 runs it is no longer newer")
    {
        CHECK_FALSE(load_record(config, "2.1.0").available);
    }
    SECTION("an up-to-date check clears it")
    {
        save_success(config, 3000, std::nullopt);
        CHECK_FALSE(load_record(config, "2.0.0").available);
    }
}

TEST_CASE("A skipped version is offered again only when a newer one comes out", "[Updates]")
{
    Slic3r::AppConfig config;
    save_success(config, 1000, Release{"2.1.0", "https://example.test/v2.1.0", "", ""});
    skip_version(config, "2.1.0");
    CHECK_FALSE(load_record(config, "2.0.0").available);

    save_success(config, 2000, Release{"2.2.0", "https://example.test/v2.2.0", "", ""});
    const UpdateRecord record = load_record(config, "2.0.0");
    REQUIRE(record.available);
    CHECK(record.available->version == "2.2.0");
}

TEST_CASE("New JusPrin versions can be turned off", "[Updates]")
{
    Slic3r::AppConfig config;
    config.set_bool(kAutoCheckKey, false);
    CHECK_FALSE(auto_check_enabled(config));
    config.set_bool(kAutoCheckKey, true);
    CHECK(auto_check_enabled(config));
    config.set_bool(kAutoCheckKey, false);
    CHECK_FALSE(auto_check_enabled(config));
}
