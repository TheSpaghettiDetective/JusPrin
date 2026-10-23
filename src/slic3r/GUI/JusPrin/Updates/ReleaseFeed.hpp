#pragma once

// Reads JusPrin's release list as GitHub's REST API returns it
// (GET /repos/TheSpaghettiDetective/JusPrin/releases). GUI-free.

#include <optional>
#include <stdexcept>
#include <string>

namespace Slic3r::GUI::JusPrin::Updates {

struct Release
{
    std::string version;   // "2.1.0", from the tag
    std::string url;       // the release page
    std::string published; // when it was published, as GitHub gives it
    std::string notes;     // the release notes, in Markdown, cut to at most kNotesLimit bytes
};

inline constexpr std::size_t kNotesLimit = 16 * 1024;

// The version a release tag names: "v2.1.0" and "jusprin/v2.1.0" both give
// "2.1.0". Empty when the tag names no version, as "nightly-builds" does.
std::string tag_version(const std::string& tag);

// What came back was not GitHub's release list.
struct FeedError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// The newest release in `body`, the release list as JSON, that is newer than
// `running`. Drafts are skipped, and so are pre-releases unless `running` is
// itself one: someone on a test build hears about newer test builds, and
// nobody else does. Throws FeedError when `body` is not a release list.
std::optional<Release> newer_release(const std::string& body, const std::string& running);

} // namespace Slic3r::GUI::JusPrin::Updates
