#include "ReleaseFeed.hpp"

#include "libslic3r/Semver.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Updates {

namespace {

std::optional<Release> newest(const nlohmann::json& releases, const Semver& current)
{
    if (!releases.is_array())
        throw FeedError("the release list is not a JSON array");
    const bool test_build = current.prerelease() != nullptr;

    std::optional<Release> found;
    Semver                 found_version = current;
    for (const nlohmann::json& release : releases) {
        if (release.at("draft").get<bool>())
            continue;
        if (release.at("prerelease").get<bool>() && !test_build)
            continue;
        const std::string version = tag_version(release.at("tag_name").get<std::string>());
        if (version.empty())
            continue;
        const Semver candidate(version);
        if (!(found_version < candidate))
            continue;
        found_version = candidate;
        // GitHub sends "body": null for a release published without notes.
        const nlohmann::json body  = release.value("body", nlohmann::json());
        std::string          notes = body.is_string() ? body.get<std::string>() : std::string();
        if (notes.size() > kNotesLimit) {
            // Cut on a character boundary: half a UTF-8 sequence would make
            // the notes unsendable to the page.
            std::size_t end = kNotesLimit;
            while (end > 0 && (static_cast<unsigned char>(notes[end]) & 0xC0) == 0x80)
                --end;
            notes.resize(end);
        }
        found = Release{version, release.at("html_url").get<std::string>(), release.value("published_at", std::string()),
                        std::move(notes)};
    }
    return found;
}

} // namespace

std::string tag_version(const std::string& tag)
{
    std::string version = tag;
    // JusPrin 1.4.0 was tagged jusprin/v1.4.0, the releases before it v1.3.0.
    if (boost::starts_with(version, "jusprin/"))
        version.erase(0, 8);
    if (boost::starts_with(version, "v"))
        version.erase(0, 1);
    return Semver::parse(version) ? version : std::string();
}

std::optional<Release> newer_release(const std::string& body, const std::string& running)
{
    const Semver current(running);
    try {
        return newest(nlohmann::json::parse(body), current);
    } catch (const nlohmann::json::exception& error) {
        throw FeedError(error.what());
    }
}

} // namespace Slic3r::GUI::JusPrin::Updates
