#include "V1Import.hpp"

#include "libslic3r_version.h"


#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace Slic3r::GUI::JusPrin::V1Import {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// v1's app key, which names its folder and its settings file.
constexpr const char* kV1Key = "JusPrin";
// Where JusPrin 2 keeps what it writes for itself inside its data folder.
constexpr const char* kReportPath = "jusprin/v1-import.json";

// The Agent section JusPrin 2 keeps its provider choices in, and the account
// provider's items there (AgentConfiguration.cpp reads them).
constexpr const char* kAgentSection = "jusprin_agent";
constexpr const char* kAccountProvider = "jusprin";

std::string now_utc()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char              text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return text;
}

std::optional<json> read_json(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    json parsed = json::parse(text.str(), nullptr, false);
    if (parsed.is_discarded())
        return std::nullopt;
    return parsed;
}

bool write_text(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

// Copies every file under `from` to the same place under `to`, recording
// any that fail (by their path inside v1's folder) and going on.
void copy_tree(const fs::path& from, const fs::path& to, const fs::path& v1, std::vector<std::string>& failed)
{
    std::error_code ec;
    if (!fs::is_directory(from, ec))
        return;
    for (fs::recursive_directory_iterator it(from, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::path target = to / fs::relative(it->path(), from);
        std::error_code step;
        if (fs::is_directory(it->path(), step))
            fs::create_directories(target, step);
        else {
            fs::create_directories(target.parent_path(), step);
            if (!step)
                fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, step);
        }
        if (step)
            failed.push_back(fs::relative(it->path(), v1).generic_u8string());
    }
    if (ec)
        failed.push_back(fs::relative(from, v1).generic_u8string());
}

// A user preset's name is its file's, without ".json".
std::vector<std::string> preset_names(const fs::path& folder)
{
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(folder, ec))
        return names;
    for (fs::directory_iterator it(folder, ec), end; it != end && !ec; it.increment(ec))
        if (it->path().extension() == ".json")
            names.push_back(it->path().stem().u8string());
    std::sort(names.begin(), names.end());
    return names;
}

// v1's settings as JusPrin 2's: everything carries over as it is (OrcaSlicer
// reads an older settings file the way it reads its own from an earlier
// version), and v1's JusPrin sign-in becomes the account provider's token.
// v1 signed in to the server in jusprin_server/base_url, which stays, so a
// self-hosted server keeps being the one signed in to.
json carried_settings(json settings, bool& signed_in)
{
    signed_in = false;
    if (!settings.is_object())
        return settings;
    json& server = settings["jusprin_server"];
    if (server.is_object() && server.contains("access_token")) {
        const std::string token = server.value("access_token", "");
        server.erase("access_token");
        if (!token.empty()) {
            json& agent = settings[kAgentSection];
            if (!agent.is_object())
                agent = json::object();
            agent[std::string(kAccountProvider) + "_api_key"] = token;
            agent["provider"]                               = kAccountProvider;
            agent["enabled"]                                = "true";
            agent["cloud_consent"]                          = "true";
            signed_in                                       = true;
        }
    }
    return settings;
}

} // namespace

json to_json(const Report& report)
{
    return json{{"source", report.source},           {"importedAt", report.imported_at}, {"printers", report.printers},
                {"presets", report.presets},         {"connections", report.connections},
                {"preferences", report.preferences}, {"signedIn", report.signed_in},     {"failed", report.failed},
                {"pending", report.pending}};
}

Report report_from_json(const json& json)
{
    Report report;
    report.source      = json.value("source", "");
    report.imported_at = json.value("importedAt", "");
    report.printers    = json.value("printers", std::vector<std::string>{});
    report.presets     = json.value("presets", std::vector<std::string>{});
    report.connections = json.value("connections", std::vector<std::string>{});
    report.preferences = json.value("preferences", false);
    report.signed_in   = json.value("signedIn", false);
    report.failed      = json.value("failed", std::vector<std::string>{});
    report.pending     = json.value("pending", false);
    return report;
}

fs::path v1_folder(const fs::path& data_dir, bool datadir_redefined)
{
    if (const char* chosen = std::getenv("JUSPRIN_V1_DATADIR"); chosen != nullptr && *chosen != '\0')
        return fs::u8path(chosen);
    if (datadir_redefined)
        return {};
    return data_dir.parent_path() / kV1Key;
}

std::optional<Report> import_if_first_launch(const fs::path& data_dir, const fs::path& v1, const std::string& app_key)
{
    std::error_code ec;
    const fs::path settings_file = data_dir / (app_key + ".conf");
    const fs::path v1_settings   = v1 / (std::string(kV1Key) + ".conf");
    if (v1.empty() || fs::exists(settings_file, ec) || !fs::is_regular_file(v1_settings, ec))
        return std::nullopt;

    Report report;
    report.source      = v1.u8string();
    report.imported_at = now_utc();

    // The person's own presets. A printer preset carries its network
    // connection (print_host and its key), so connections come with it.
    copy_tree(v1 / "user", data_dir / "user", v1, report.failed);
    for (const char* file : {"BambuNetworkEngine.conf"}) {
        if (!fs::is_regular_file(v1 / file, ec))
            continue;
        std::error_code step;
        fs::copy_file(v1 / file, data_dir / file, fs::copy_options::overwrite_existing, step);
        if (step)
            report.failed.push_back(file);
    }
    for (fs::directory_iterator user(data_dir / "user", ec), end; user != end && !ec; user.increment(ec)) {
        if (!fs::is_directory(user->path(), ec))
            continue;
        for (const std::string& name : preset_names(user->path() / "machine")) {
            report.printers.push_back(name);
            const std::optional<json> preset = read_json(user->path() / "machine" / (name + ".json"));
            if (preset && preset->is_object() && !preset->value("print_host", "").empty())
                report.connections.push_back(name);
        }
        for (const char* kind : {"filament", "process"})
            for (const std::string& name : preset_names(user->path() / kind))
                report.presets.push_back(name);
    }

    // The settings file last: its presence is what says the import is done,
    // so a crash before this point imports again next launch.
    if (const std::optional<json> settings = read_json(v1_settings)) {
        const json carried = carried_settings(*settings, report.signed_in);
        report.preferences = write_text(settings_file, carried.dump(4));
        if (!report.preferences)
            report.failed.push_back(std::string(kV1Key) + ".conf");
    } else {
        report.failed.push_back(std::string(kV1Key) + ".conf");
    }
    write_text(data_dir / kReportPath, to_json(report).dump(2));
    return report;
}

void import_on_first_launch(const std::string& data_dir, bool datadir_redefined)
{
    const fs::path folder = fs::u8path(data_dir);
    import_if_first_launch(folder, v1_folder(folder, datadir_redefined), SLIC3R_APP_KEY);
}

std::optional<Report> read_report(const fs::path& data_dir)
{
    const std::optional<json> stored = read_json(data_dir / kReportPath);
    if (!stored || !stored->is_object())
        return std::nullopt;
    return report_from_json(*stored);
}

void mark_shown(const fs::path& data_dir)
{
    std::optional<Report> report = read_report(data_dir);
    if (!report)
        return;
    report->pending = false;
    write_text(data_dir / kReportPath, to_json(*report).dump(2));
}

} // namespace Slic3r::GUI::JusPrin::V1Import
