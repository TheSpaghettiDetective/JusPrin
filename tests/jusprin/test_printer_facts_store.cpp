// Contract tests for the printer-facts store: what a person states about a
// physical printer that no sensor reports, and for how long it stays true.
// Real files in a unique temporary directory, with an injected clock so
// expiry is tested without waiting.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Workspace/PrinterFactsStore.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace Slic3r::GUI::JusPrin::Workspace;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

class TempDir
{
public:
    TempDir()
    {
        static const std::string run = std::to_string(std::random_device{}());
        static std::atomic<int>  counter{0};
        m_path = fs::temp_directory_path() / ("jusprin-printer-facts-" + run + "-" + std::to_string(counter++));
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() { fs::remove_all(m_path); }
    fs::path file() const { return m_path / "printer_facts.json"; }

private:
    fs::path m_path;
};

// 2026-09-16T12:00:00Z
const std::chrono::system_clock::time_point kNoon = std::chrono::system_clock::time_point(std::chrono::seconds(1789560000));

} // namespace

TEST_CASE("a stated fact is current until it expires", "[printer-facts]")
{
    TempDir dir;
    auto    now = kNoon;
    PrinterFactsStore store({dir.file().string(), [&now] { return now; }});
    CHECK(store.current("FAKE001").empty());

    const auto stated = store.confirm("FAKE001", {{"plate", "Textured PEI", 24h}, {"spool_dry", "true", 2h}});
    REQUIRE(stated.size() == 2);
    // Ordered by fact name, stamped with when it was said and when it stops.
    CHECK(stated[0].fact == "plate");
    CHECK(stated[0].confirmed_at == "2026-09-16T12:00:00Z");
    CHECK(stated[0].expires_at == "2026-09-17T12:00:00Z");
    CHECK(stated[1].expires_at == "2026-09-16T14:00:00Z");

    // Facts belong to one printer.
    CHECK(store.current("OTHER").empty());

    // Three hours later the spool may have picked up moisture again.
    now = kNoon + 3h;
    const auto later = store.current("FAKE001");
    REQUIRE(later.size() == 1);
    CHECK(later[0].fact == "plate");

    // Past a day, nothing stated is still assumed.
    now = kNoon + 25h;
    CHECK(store.current("FAKE001").empty());
}

TEST_CASE("restating a fact replaces it and survives a reload", "[printer-facts]")
{
    TempDir dir;
    auto    now = kNoon;
    {
        PrinterFactsStore store({dir.file().string(), [&now] { return now; }});
        store.confirm("FAKE001", {{"plate", "Textured PEI", 24h}});
        now += 1h;
        const auto swapped = store.confirm("FAKE001", {{"plate", "Smooth PEI", 24h}});
        REQUIRE(swapped.size() == 1);
        CHECK(swapped[0].value == "Smooth PEI");
        CHECK(swapped[0].confirmed_at == "2026-09-16T13:00:00Z");
    }
    PrinterFactsStore reopened({dir.file().string(), [&now] { return now; }});
    CHECK_FALSE(reopened.corrupt());
    const auto facts = reopened.current("FAKE001");
    REQUIRE(facts.size() == 1);
    CHECK(facts[0].value == "Smooth PEI");
}

TEST_CASE("expired facts are not kept on disk", "[printer-facts]")
{
    TempDir dir;
    auto    now = kNoon;
    PrinterFactsStore store({dir.file().string(), [&now] { return now; }});
    store.confirm("FAKE001", {{"bed_clear", "true", 1h}});
    now += 2h;
    store.confirm("FAKE001", {{"plate", "Textured PEI", 24h}});

    std::ifstream in(dir.file());
    const auto    document = nlohmann::json::parse(in);
    REQUIRE(document["facts"].size() == 1);
    CHECK(document["facts"][0]["fact"] == "plate");
}

TEST_CASE("a damaged facts file is moved aside, not overwritten", "[printer-facts]")
{
    TempDir dir;
    {
        std::ofstream out(dir.file());
        out << "{ not json";
    }
    PrinterFactsStore store({dir.file().string(), [] { return kNoon; }});
    CHECK(store.corrupt());
    CHECK(store.current("FAKE001").empty());
    CHECK(fs::exists(dir.file().string() + ".corrupt"));
    store.confirm("FAKE001", {{"plate", "Textured PEI", 24h}});
    CHECK(store.current("FAKE001").size() == 1);
}
