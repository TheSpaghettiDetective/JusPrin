#pragma once

// What JusPrin's update check remembers between runs, and when it runs
// next. Kept in AppConfig; GUI-free.

#include "ReleaseFeed.hpp"

#include <ctime>
#include <optional>
#include <string>

namespace Slic3r { class AppConfig; }

namespace Slic3r::GUI::JusPrin::Updates {

// "New JusPrin versions" in Preferences > Updates: "false" turns the daily
// check off. Check now still works.
inline constexpr char kAutoCheckKey[] = "jusprin_update_check";
// Where the release list is read from, for tests against a local server.
inline constexpr char kFeedUrlKey[] = "jusprin_update_feed_url";

struct UpdateRecord
{
    std::time_t            last_attempt{0}; // 0: never
    std::time_t            last_success{0};
    bool                   last_failed{false};
    std::optional<Release> available; // newer than the running version
};

bool auto_check_enabled(const AppConfig& config);

// A release remembered from an earlier run is dropped once the running
// version has caught up with it, and so is a skipped one.
UpdateRecord load_record(const AppConfig& config, const std::string& running);
void         save_success(AppConfig& config, std::time_t now, const std::optional<Release>& available);
void         save_failure(AppConfig& config, std::time_t now);
// "Skip this version": that version and older ones are no longer offered.
void         skip_version(AppConfig& config, const std::string& version);

// Whether the daily check should run: a day after the last attempt and, at
// startup, also when the last attempt failed.
bool check_due(const UpdateRecord& record, std::time_t now, bool at_startup);

} // namespace Slic3r::GUI::JusPrin::Updates
