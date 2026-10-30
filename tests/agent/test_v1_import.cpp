// Unit tests for bringing a JusPrin v1 setup into JusPrin 2 on first launch:
// what is copied, what becomes of v1's sign-in, and when nothing happens.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/V1Import/V1Import.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Slic3r::GUI::JusPrin;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path)
{
    std::ifstream     in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// A v1 folder with what a person who used it for a while has, and an empty
// JusPrin 2 folder beside it.
struct Folders
{
    fs::path root;
    fs::path v1;
    fs::path v2;

    Folders()
    {
        // A folder of its own: test cases run as separate processes, at once
        // under ctest -j, and each deletes its folder when done.
        root = fs::temp_directory_path() / ("jusprin-v1-import-" + boost::uuids::to_string(boost::uuids::random_generator()()));
        v1 = root / "JusPrin";
        v2 = root / "JusPrin2";
        fs::create_directories(v2);
        write(v1 / "JusPrin.conf", json{{"app", {{"language", "de_DE"}, {"units", "mm"}}},
                                        {"jusprin_server", {{"access_token", "v1-token"}, {"base_url", "https://jusprin.test"}}},
                                        {"presets", {{"machine", "MyKlipper 0.4 nozzle"}}}}
                                       .dump(4));
        write(v1 / "user/default/machine/MyKlipper 0.4 nozzle.json",
              json{{"name", "MyKlipper 0.4 nozzle"}, {"print_host", "192.168.1.20"}}.dump());
        write(v1 / "user/default/machine/MyKlipper 0.4 nozzle.info", "sync_info = \n");
        write(v1 / "user/default/machine/Bambu Lab A1 0.4 nozzle - Copy.json", json{{"name", "Bambu Lab A1 0.4 nozzle - Copy"}}.dump());
        write(v1 / "user/default/filament/Silk PETG.json", "{}");
        write(v1 / "user/default/process/0.20mm Strong.json", "{}");
        write(v1 / "BambuNetworkEngine.conf", "{}");
        // What is not the person's: logs, caches, OrcaSlicer's copies of the
        // built-in profiles, v1's own backups.
        write(v1 / "log/debug.log", "log");
        write(v1 / "cache/x.bin", "cache");
        write(v1 / "system/BBL.json", "{}");
        write(v1 / "user_backup-v1.4.0/default/filament/Old.json", "{}");
    }
    ~Folders() { fs::remove_all(root); }
};

} // namespace

TEST_CASE("a first launch copies v1's setup and carries its sign-in over", "[v1_import]")
{
    Folders f;
    const std::optional<V1Import::Report> report = V1Import::import_if_first_launch(f.v2, f.v1, "JusPrin2");
    REQUIRE(report.has_value());

    // The person's presets, with the printers' connections inside them.
    CHECK(fs::exists(f.v2 / "user/default/machine/MyKlipper 0.4 nozzle.json"));
    CHECK(fs::exists(f.v2 / "user/default/machine/MyKlipper 0.4 nozzle.info"));
    CHECK(fs::exists(f.v2 / "user/default/filament/Silk PETG.json"));
    CHECK(fs::exists(f.v2 / "BambuNetworkEngine.conf"));
    CHECK(report->printers == std::vector<std::string>{"Bambu Lab A1 0.4 nozzle - Copy", "MyKlipper 0.4 nozzle"});
    CHECK(report->presets == std::vector<std::string>{"Silk PETG", "0.20mm Strong"});
    CHECK(report->connections == std::vector<std::string>{"MyKlipper 0.4 nozzle"});
    CHECK(report->failed.empty());
    CHECK(report->pending);

    // Nothing that is not the person's.
    CHECK_FALSE(fs::exists(f.v2 / "log"));
    CHECK_FALSE(fs::exists(f.v2 / "cache"));
    CHECK_FALSE(fs::exists(f.v2 / "system"));
    CHECK_FALSE(fs::exists(f.v2 / "user_backup-v1.4.0"));

    // v1's settings as JusPrin 2's, its sign-in now the account provider's.
    REQUIRE(report->preferences);
    CHECK(report->signed_in);
    const json settings = json::parse(read(f.v2 / "JusPrin2.conf"));
    CHECK(settings["app"]["language"] == "de_DE");
    CHECK(settings["presets"]["machine"] == "MyKlipper 0.4 nozzle");
    CHECK(settings["jusprin_server"] == json{{"base_url", "https://jusprin.test"}});
    CHECK(settings["jusprin_agent"]["jusprin_api_key"] == "v1-token");
    CHECK(settings["jusprin_agent"]["provider"] == "jusprin");
    CHECK(settings["jusprin_agent"]["enabled"] == "true");
    CHECK(settings["jusprin_agent"]["cloud_consent"] == "true");

    // v1's folder is only read.
    CHECK(json::parse(read(f.v1 / "JusPrin.conf"))["jusprin_server"]["access_token"] == "v1-token");

    // The report is kept for the welcome-back screen, which shows it once.
    const std::optional<V1Import::Report> kept = V1Import::read_report(f.v2);
    REQUIRE(kept.has_value());
    CHECK(kept->printers == report->printers);
    CHECK(kept->pending);
    V1Import::mark_shown(f.v2);
    CHECK_FALSE(V1Import::read_report(f.v2)->pending);
}

TEST_CASE("v1 without a sign-in imports everything else", "[v1_import]")
{
    Folders f;
    write(f.v1 / "JusPrin.conf", json{{"app", {{"units", "in"}}}}.dump());
    const std::optional<V1Import::Report> report = V1Import::import_if_first_launch(f.v2, f.v1, "JusPrin2");
    REQUIRE(report.has_value());
    CHECK_FALSE(report->signed_in);
    CHECK_FALSE(json::parse(read(f.v2 / "JusPrin2.conf")).contains("jusprin_agent"));
}

TEST_CASE("nothing is imported once JusPrin 2 has settings of its own, or without v1", "[v1_import]")
{
    Folders f;
    write(f.v2 / "JusPrin2.conf", "{}");
    CHECK_FALSE(V1Import::import_if_first_launch(f.v2, f.v1, "JusPrin2").has_value());
    CHECK_FALSE(fs::exists(f.v2 / "user"));
    CHECK(read(f.v2 / "JusPrin2.conf") == "{}");

    Folders g;
    fs::remove(g.v1 / "JusPrin.conf");
    CHECK_FALSE(V1Import::import_if_first_launch(g.v2, g.v1, "JusPrin2").has_value());
    CHECK_FALSE(V1Import::import_if_first_launch(g.v2, {}, "JusPrin2").has_value());
    CHECK_FALSE(V1Import::read_report(g.v2).has_value());
}

TEST_CASE("v1's folder is the one beside JusPrin 2's, never for a --datadir run", "[v1_import]")
{
    const fs::path data = fs::path("/Users/alex/Library/Application Support/JusPrin2");
    CHECK(V1Import::v1_folder(data, false) == fs::path("/Users/alex/Library/Application Support/JusPrin"));
    CHECK(V1Import::v1_folder(data, true).empty());
}
