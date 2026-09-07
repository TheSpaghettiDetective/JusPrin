// Contract tests for the remembered-spool store: the fork-owned record that
// pairs an Orca filament preset with a colour and a name, per printer.
//
// Every test runs against a real file in a unique temporary directory, because
// the durability behaviour (atomic replace, reload, a damaged file moved
// aside) is the part worth proving. Clock and uuid are injected so ordering
// assertions do not depend on wall-clock resolution.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using namespace Slic3r::GUI::JusPrin::Workspace;
namespace fs = std::filesystem;

namespace {

// One directory per fixture instance, removed on destruction, so a failing
// test never leaks state into the next one.
class TempDir
{
public:
    TempDir()
    {
        // Unique per run and per fixture, without a POSIX-only pid call.
        static const std::string run = std::to_string(std::random_device{}());
        static std::atomic<int>  counter{0};
        m_path = fs::temp_directory_path() / ("jusprin-spool-store-" + run + "-" + std::to_string(counter++));
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(m_path, ec); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string file() const { return (m_path / "spools.json").string(); }
    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

// A clock the test drives by hand: each call returns the next second, so
// "most recently used" has a defined answer without sleeping.
struct StepClock
{
    int seconds{0};
    std::string operator()()
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "2026-01-01T00:00:%02dZ", seconds++);
        return buffer;
    }
};

struct CountingUuid
{
    int next{1};
    std::string operator()() { return "s" + std::to_string(next++); }
};

SpoolStore::Config config_for(const std::string& file, StepClock& clock, CountingUuid& uuid)
{
    SpoolStore::Config config;
    config.file_path = file;
    config.clock     = [&clock] { return clock(); };
    config.uuid      = [&uuid] { return uuid(); };
    return config;
}

Spool make_spool(const std::string& printer, const std::string& filament, const std::string& colour,
                 const std::string& name, const std::string& brand = "eSun")
{
    Spool spool;
    spool.printer_preset  = printer;
    spool.filament_preset = filament;
    spool.colour          = colour;
    spool.name            = name;
    spool.brand           = brand;
    return spool;
}

std::string read_text(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("a missing file is an empty store, not a failure", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;

    SpoolStore store(config_for(dir.file(), clock, uuid));

    CHECK_FALSE(store.corrupt());
    CHECK(store.size() == 0);
    CHECK(store.spools_for("Bambu P1S 0.4 nozzle").empty());
    // Reading must not create the file; only a write does.
    CHECK_FALSE(fs::exists(dir.file()));
}

TEST_CASE("spools survive a save and reload unchanged", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;

    Spool stored;
    {
        SpoolStore store(config_for(dir.file(), clock, uuid));
        stored = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White PLA+"));
        CHECK(stored.id == "s1");
        CHECK(stored.last_used == "2026-01-01T00:00:00Z");
    }

    StepClock reload_clock;
    CountingUuid reload_uuid;
    SpoolStore reopened(config_for(dir.file(), reload_clock, reload_uuid));

    REQUIRE(reopened.size() == 1);
    const auto found = reopened.find("s1");
    REQUIRE(found.has_value());
    // Field-by-field equality, so a serializer that drops one field fails here
    // rather than in the UI.
    CHECK(*found == stored);
    CHECK(found->printer_preset == "P1S 0.4");
    CHECK(found->filament_preset == "eSun PLA+ @P1S");
    CHECK(found->colour == "#F5F5F0");
    CHECK(found->name == "Cold White PLA+");
    CHECK(found->brand == "eSun");
}

TEST_CASE("spools_for returns one printer's spools, most recently used first", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;
    SpoolStore store(config_for(dir.file(), clock, uuid));

    const Spool white = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White"));
    const Spool black = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#101010", "Black"));
    const Spool other = store.add(make_spool("A1 mini 0.4", "Generic PLA @A1", "#B44757", "Red"));

    const auto listed = store.spools_for("P1S 0.4");
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].id == black.id); // added later, so used more recently
    CHECK(listed[1].id == white.id);

    // The other printer's spool is absent, not merely last.
    for (const Spool& spool : listed)
        CHECK(spool.id != other.id);

    CHECK(store.spools_for("A1 mini 0.4").size() == 1);
    CHECK(store.spools_for("printer that owns nothing").empty());
}

TEST_CASE("touch moves a spool to the front and persists the new order", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;

    std::string white_id;
    {
        SpoolStore store(config_for(dir.file(), clock, uuid));
        white_id = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White")).id;
        store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#101010", "Black"));

        REQUIRE(store.spools_for("P1S 0.4").front().name == "Black");
        REQUIRE(store.touch(white_id));
        CHECK(store.spools_for("P1S 0.4").front().id == white_id);
    }

    StepClock reload_clock;
    CountingUuid reload_uuid;
    SpoolStore reopened(config_for(dir.file(), reload_clock, reload_uuid));
    CHECK(reopened.spools_for("P1S 0.4").front().id == white_id);
}

TEST_CASE("current matches on filament preset and colour together", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;
    SpoolStore store(config_for(dir.file(), clock, uuid));

    const Spool white = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White"));
    store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#101010", "Black"));

    const auto matched = store.current("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0");
    REQUIRE(matched.has_value());
    CHECK(matched->id == white.id);

    // Same preset, a colour nothing is remembered in.
    CHECK_FALSE(store.current("P1S 0.4", "eSun PLA+ @P1S", "#00FF00").has_value());
    // Same colour, a different preset.
    CHECK_FALSE(store.current("P1S 0.4", "Generic PLA @P1S", "#F5F5F0").has_value());
    // Right spool, wrong printer: spools do not leak between printers.
    CHECK_FALSE(store.current("A1 mini 0.4", "eSun PLA+ @P1S", "#F5F5F0").has_value());
}

TEST_CASE("rename, recolour and remove persist; an unknown id changes nothing", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;

    std::string kept_id;
    {
        SpoolStore store(config_for(dir.file(), clock, uuid));
        kept_id = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White")).id;
        const std::string doomed_id = store.add(make_spool("P1S 0.4", "Generic PLA @P1S", "#101010", "Black")).id;

        CHECK(store.rename(kept_id, "Bone White PLA+"));
        CHECK(store.recolour(kept_id, "#EDEAE0"));
        CHECK(store.remove(doomed_id));

        // An id that is not in the store is a normal negative answer.
        CHECK_FALSE(store.rename("no-such-spool", "Nothing"));
        CHECK_FALSE(store.recolour("no-such-spool", "#000000"));
        CHECK_FALSE(store.remove("no-such-spool"));
        CHECK_FALSE(store.touch("no-such-spool"));
        CHECK(store.size() == 1);
    }

    StepClock reload_clock;
    CountingUuid reload_uuid;
    SpoolStore reopened(config_for(dir.file(), reload_clock, reload_uuid));
    REQUIRE(reopened.size() == 1);
    const auto found = reopened.find(kept_id);
    REQUIRE(found.has_value());
    CHECK(found->name == "Bone White PLA+");
    CHECK(found->colour == "#EDEAE0");
}

TEST_CASE("a damaged file is moved aside, never overwritten", "[spools]")
{
    TempDir dir;
    const std::string damaged = "{\"spools\": [ this is not json";
    { std::ofstream out(dir.file()); out << damaged; }

    StepClock clock;
    CountingUuid uuid;
    SpoolStore store(config_for(dir.file(), clock, uuid));

    // Visible, not silently swallowed.
    CHECK(store.corrupt());
    CHECK_FALSE(store.corrupt_reason().empty());
    CHECK(store.size() == 0);

    // The bytes the person had are still on disk, under a name that says why.
    const std::string aside = dir.file() + ".corrupt";
    REQUIRE(fs::exists(aside));
    CHECK(read_text(aside) == damaged);

    // And the store still works: a new spool writes a fresh, valid file.
    const Spool added = store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White"));
    REQUIRE(fs::exists(dir.file()));
    CHECK(nlohmann::json::parse(read_text(dir.file())).at("spools").size() == 1);
    CHECK(read_text(aside) == damaged); // still untouched
    CHECK(store.find(added.id).has_value());
}

TEST_CASE("a well-formed file whose entries lack required fields is treated as damaged", "[spools]")
{
    TempDir dir;
    // Parses as JSON, but "colour" is missing, so the record cannot be read.
    { std::ofstream out(dir.file()); out << R"({"version":1,"spools":[{"id":"s1","printer_preset":"P1S 0.4",)"
                                            R"("filament_preset":"eSun PLA+ @P1S","name":"Cold White"}]})"; }

    StepClock clock;
    CountingUuid uuid;
    SpoolStore store(config_for(dir.file(), clock, uuid));

    CHECK(store.corrupt());
    CHECK(store.size() == 0);
    CHECK(fs::exists(dir.file() + ".corrupt"));
}

TEST_CASE("the store writes no temporary file behind after a successful save", "[spools]")
{
    TempDir dir;
    StepClock clock;
    CountingUuid uuid;
    SpoolStore store(config_for(dir.file(), clock, uuid));
    store.add(make_spool("P1S 0.4", "eSun PLA+ @P1S", "#F5F5F0", "Cold White"));

    int files = 0;
    for (const auto& entry : fs::directory_iterator(dir.path())) {
        CHECK(entry.path().extension() != ".tmp");
        ++files;
    }
    CHECK(files == 1);
}
