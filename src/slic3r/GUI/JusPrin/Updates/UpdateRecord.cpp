#include "UpdateRecord.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Semver.hpp"

namespace Slic3r::GUI::JusPrin::Updates {

namespace {

constexpr char        kLastAttemptKey[] = "jusprin_update_last_attempt";
constexpr char        kLastSuccessKey[] = "jusprin_update_last_success";
constexpr char        kLastFailedKey[]  = "jusprin_update_last_failed";
constexpr char        kVersionKey[]     = "jusprin_update_version";
constexpr char        kUrlKey[]         = "jusprin_update_url";
constexpr char        kPublishedKey[]   = "jusprin_update_published";
constexpr char        kNotesKey[]       = "jusprin_update_notes";
constexpr char        kSkippedKey[]     = "jusprin_update_skipped";
constexpr std::time_t kDay              = 24 * 60 * 60;

std::time_t read_time(const AppConfig& config, const char* key)
{
    const std::string value = config.get(key);
    return value.empty() ? 0 : static_cast<std::time_t>(std::stoll(value));
}

} // namespace

// Written with set_bool, as Preferences writes its switches; on until turned off.
bool auto_check_enabled(const AppConfig& config) { return config.get(kAutoCheckKey) != "false"; }

UpdateRecord load_record(const AppConfig& config, const std::string& running)
{
    UpdateRecord record;
    record.last_attempt = read_time(config, kLastAttemptKey);
    record.last_success = read_time(config, kLastSuccessKey);
    record.last_failed  = config.get(kLastFailedKey) == "1";
    const std::string version = config.get(kVersionKey);
    const auto        parsed  = Semver::parse(version);
    if (!parsed || !(Semver(running) < *parsed))
        return record;
    if (const auto skipped = Semver::parse(config.get(kSkippedKey)); skipped && !(*skipped < *parsed))
        return record;
    record.available = Release{version, config.get(kUrlKey), config.get(kPublishedKey), config.get(kNotesKey)};
    return record;
}

void save_success(AppConfig& config, std::time_t now, const std::optional<Release>& available)
{
    config.set(kLastAttemptKey, std::to_string(now));
    config.set(kLastSuccessKey, std::to_string(now));
    config.set(kLastFailedKey, "0");
    config.set(kVersionKey, available ? available->version : std::string());
    config.set(kUrlKey, available ? available->url : std::string());
    config.set(kPublishedKey, available ? available->published : std::string());
    config.set(kNotesKey, available ? available->notes : std::string());
}

void save_failure(AppConfig& config, std::time_t now)
{
    config.set(kLastAttemptKey, std::to_string(now));
    config.set(kLastFailedKey, "1");
}

void skip_version(AppConfig& config, const std::string& version) { config.set(kSkippedKey, version); }

bool check_due(const UpdateRecord& record, std::time_t now, bool at_startup)
{
    if (at_startup && record.last_failed)
        return true;
    // A clock set back past the last attempt must not hold the check off.
    return now < record.last_attempt || now - record.last_attempt >= kDay;
}

} // namespace Slic3r::GUI::JusPrin::Updates
