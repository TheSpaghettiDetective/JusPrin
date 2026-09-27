// Contract tests for reading a Klipper printer's multi-filament unit from its
// Moonraker host: what each reply means, and how long a reading counts.
// The lane records follow scripts/test_moonraker_lane_data.py, upstream's
// fixture writer for the same namespace.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Printers/HostLanes.hpp"

#include <string>

using namespace Slic3r::GUI::JusPrin::Printers;
using namespace std::chrono_literals;

namespace {

const std::string kFourLanes = R"({"result": {"namespace": "lane_data", "value": {
    "lane1": {"color": "#5F7D4F", "material": "PLA", "bed_temp": 60, "nozzle_temp": 210, "scan_time": "", "td": "", "lane": "0", "spool_id": 12},
    "lane2": {"color": "", "material": "", "bed_temp": null, "nozzle_temp": null, "scan_time": "", "td": "", "lane": "1", "spool_id": null},
    "lane3": {"color": "#None", "material": "PETG", "bed_temp": 80, "nozzle_temp": 235, "scan_time": "", "td": "", "lane": "2", "spool_id": null},
    "lane10": {"color": "0x112233", "material": "ASA Sparkle", "bed_temp": 105, "nozzle_temp": 245, "scan_time": "", "td": "", "lane": "9", "spool_id": 3}
}}})";

std::string happy_hare(const std::string& mmu) { return R"({"result": {"eventtime": 1.0, "status": {"mmu": )" + mmu + "}}}"; }

} // namespace

TEST_CASE("a lane colour is #RRGGBB or nothing", "[host-lanes]")
{
    CHECK(lane_colour("#5f7d4f") == "#5F7D4F");
    CHECK(lane_colour("5F7D4F") == "#5F7D4F");
    CHECK(lane_colour("0x5F7D4F") == "#5F7D4F");
    CHECK(lane_colour(" #5F7D4FFF") == "#5F7D4F");
    // Not a colour, and not white either.
    CHECK(lane_colour("").empty());
    CHECK(lane_colour("#None").empty());
    CHECK(lane_colour("red").empty());
    CHECK(lane_colour("#FFF").empty());
}

TEST_CASE("lane_data lists the lanes that hold filament, in lane order", "[host-lanes]")
{
    const auto report = parse_lane_data(kFourLanes);
    REQUIRE(report);
    CHECK(report->unit);
    REQUIRE(report->loaded.size() == 3);
    CHECK(report->loaded[0].index == 0);
    CHECK(report->loaded[0].material == "PLA");
    CHECK(report->loaded[0].colour == "#5F7D4F");
    CHECK(report->loaded[1].index == 2);
    CHECK(report->loaded[1].material == "PETG");
    CHECK(report->loaded[1].colour.empty());
    CHECK(report->loaded[2].index == 9);
    CHECK(report->loaded[2].material == "ASA Sparkle");
    CHECK(report->loaded[2].colour == "#112233");
}

TEST_CASE("lane_data with every lane empty is a unit holding nothing", "[host-lanes]")
{
    const auto report = parse_lane_data(
        R"({"result": {"namespace": "lane_data", "value": {"lane1": {"material": "", "color": "", "lane": "0"}}}})");
    REQUIRE(report);
    CHECK(report->unit);
    CHECK(report->loaded.empty());
}

TEST_CASE("lane_data without a numbered lane is no unit", "[host-lanes]")
{
    // Upstream reads the lane number as a string and skips anything else.
    const auto report = parse_lane_data(R"({"result": {"namespace": "lane_data", "value": {
        "lane1": {"material": "PLA", "lane": ""}, "lane2": {"material": "PLA", "lane": 1},
        "lane3": {"material": "PLA", "lane": "-1"}, "lane4": "PLA"}}})");
    REQUIRE(report);
    CHECK_FALSE(report->unit);
    CHECK(report->loaded.empty());
    const auto empty = parse_lane_data(R"({"result": {"namespace": "lane_data", "value": {}}})");
    REQUIRE(empty);
    CHECK_FALSE(empty->unit);
}

TEST_CASE("a reply that is not lane_data says nothing", "[host-lanes]")
{
    CHECK_FALSE(parse_lane_data(""));
    CHECK_FALSE(parse_lane_data("<html></html>"));
    CHECK_FALSE(parse_lane_data(R"({"result": {"klippy_state": "ready"}})"));
    CHECK_FALSE(parse_lane_data(R"({"error": {"code": 404, "message": "Namespace 'lane_data' not found"}})"));
    CHECK_FALSE(parse_lane_data(R"({"result": {"value": []}})"));
}

TEST_CASE("Happy Hare lists the available gates that name a material", "[host-lanes]")
{
    const auto report = parse_happy_hare(happy_hare(R"({"num_gates": 5,
        "gate_status": [1, 0, 2, -1, 1],
        "gate_material": ["PLA", "PETG", "ABS", "TPU", ""],
        "gate_color": ["ff0000", "00ff00", "", "0000ff", "ffffff"],
        "gate_temperature": [210, 235, 250, 220, 0]})"));
    REQUIRE(report);
    CHECK(report->unit);
    REQUIRE(report->loaded.size() == 2);
    CHECK(report->loaded[0].index == 0);
    CHECK(report->loaded[0].material == "PLA");
    CHECK(report->loaded[0].colour == "#FF0000");
    CHECK(report->loaded[1].index == 2);
    CHECK(report->loaded[1].material == "ABS");
    CHECK(report->loaded[1].colour.empty());
}

TEST_CASE("Happy Hare with no gate loaded is a unit holding nothing", "[host-lanes]")
{
    const auto report = parse_happy_hare(happy_hare(
        R"({"num_gates": 2, "gate_status": [0, -1], "gate_material": ["PLA", "PLA"], "gate_color": ["", ""], "gate_temperature": [0, 0]})"));
    REQUIRE(report);
    CHECK(report->unit);
    CHECK(report->loaded.empty());
}

TEST_CASE("a Klipper printer without Happy Hare is no unit", "[host-lanes]")
{
    // Klipper leaves out an object it does not have.
    const auto absent = parse_happy_hare(R"({"result": {"eventtime": 1.0, "status": {}}})");
    REQUIRE(absent);
    CHECK_FALSE(absent->unit);
    // Happy Hare not installed.
    const auto empty = parse_happy_hare(happy_hare("{}"));
    REQUIRE(empty);
    CHECK_FALSE(empty->unit);
    const auto no_gates = parse_happy_hare(happy_hare(R"({"num_gates": 0})"));
    REQUIRE(no_gates);
    CHECK_FALSE(no_gates->unit);
    // Upstream gives up on arrays of the wrong type rather than guessing.
    const auto malformed = parse_happy_hare(happy_hare(R"({"num_gates": 1, "gate_status": {"0": 1}})"));
    REQUIRE(malformed);
    CHECK_FALSE(malformed->unit);
}

TEST_CASE("a reply that is not an object query says nothing", "[host-lanes]")
{
    CHECK_FALSE(parse_happy_hare(""));
    CHECK_FALSE(parse_happy_hare(R"({"error": {"code": 503, "message": "Klippy Host not connected"}})"));
    CHECK_FALSE(parse_happy_hare(R"({"result": {"klippy_state": "ready"}})"));
}

TEST_CASE("a host reading counts while it is fresh", "[host-lanes]")
{
    HostReadings     readings;
    const auto       t0 = HostReadings::Clock::time_point{} + 1h;
    const HostLane   pla{0, "PLA", "#5F7D4F"};

    // Never asked: nothing to say yet.
    CHECK(readings.status("printer", t0).link == HostLink::Checking);

    REQUIRE(readings.begin("printer", t0));
    // One read at a time, and none again within the poll interval.
    CHECK_FALSE(readings.begin("printer", t0 + 1s));
    CHECK(readings.status("printer", t0 + 1s).link == HostLink::Checking);

    readings.finish("printer", HostReading{true, std::vector<HostLane>{pla}}, t0 + 2s);
    CHECK_FALSE(readings.begin("printer", t0 + 5s));
    HostStatus status = readings.status("printer", t0 + 5s);
    CHECK(status.link == HostLink::Reachable);
    REQUIRE(status.loaded);
    REQUIRE(status.loaded->size() == 1);
    CHECK(status.loaded->front().material == "PLA");

    // Due again after the interval; a failed read does not undo a fresh answer.
    REQUIRE(readings.begin("printer", t0 + kHostPollInterval));
    readings.finish("printer", HostReading{}, t0 + 12s);
    CHECK(readings.status("printer", t0 + 12s).link == HostLink::Reachable);

    // Once the answer is stale and the latest attempt failed, it is unreachable.
    CHECK(readings.status("printer", t0 + 2s + kHostReadingFresh).link == HostLink::Unreachable);
    CHECK_FALSE(readings.status("printer", t0 + 2s + kHostReadingFresh).loaded);

    // Asked again much later: neither the old answer nor the old failure says
    // anything about now.
    CHECK(readings.status("printer", t0 + 10min).link == HostLink::Checking);
}

TEST_CASE("an answer that did not say what is loaded leaves it unsaid", "[host-lanes]")
{
    HostReadings readings;
    const auto   t0 = HostReadings::Clock::time_point{} + 1h;
    readings.begin("printer", t0);
    readings.finish("printer", HostReading{true, std::nullopt}, t0);
    const HostStatus status = readings.status("printer", t0 + 1s);
    CHECK(status.link == HostLink::Reachable);
    CHECK_FALSE(status.loaded);

    // A connection test answering is reachability, not a reading of the unit.
    readings.note_answered("other", t0);
    CHECK(readings.status("other", t0 + 1s).link == HostLink::Reachable);
    CHECK_FALSE(readings.status("other", t0 + 1s).loaded);
    // And it does not stop the first read from starting.
    CHECK(readings.begin("other", t0 + 1s));
}

TEST_CASE("each host's settings are read separately", "[host-lanes]")
{
    HostReadings readings;
    const auto   t0 = HostReadings::Clock::time_point{} + 1h;
    readings.begin("printer\nhttp://a", t0);
    readings.finish("printer\nhttp://a", HostReading{true, std::vector<HostLane>{}}, t0);
    CHECK(readings.status("printer\nhttp://a", t0).link == HostLink::Reachable);
    CHECK(readings.status("printer\nhttp://b", t0).link == HostLink::Checking);
    CHECK(readings.begin("printer\nhttp://b", t0));
}
