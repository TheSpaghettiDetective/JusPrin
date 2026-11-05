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
    // Times are read off the named intervals, so tuning them keeps this
    // sequence in order.
    const auto before_due = t0 + kHostPollInterval - 1s;
    CHECK_FALSE(readings.begin("printer", before_due));
    HostStatus status = readings.status("printer", before_due);
    CHECK(status.link == HostLink::Reachable);
    REQUIRE(status.loaded);
    REQUIRE(status.loaded->size() == 1);
    CHECK(status.loaded->front().material == "PLA");

    // Due again after the interval; a failed read does not undo a fresh answer.
    REQUIRE(readings.begin("printer", t0 + kHostPollInterval));
    const auto failed_at = t0 + kHostPollInterval + 2s;
    readings.finish("printer", HostReading{}, failed_at);
    CHECK(readings.status("printer", failed_at).link == HostLink::Reachable);

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

// The bodies below are what the Docker Moonraker in tests/printer_hosts
// answered on 2026-11-05, trimmed to the fields read.
TEST_CASE("Klipper's state reads as what the printer is doing", "[host-lanes]")
{
    const auto printing = parse_klipper_state(R"({"result": {"eventtime": 526585.9, "status": {
        "webhooks": {"state": "ready", "state_message": "Printer is ready"},
        "print_stats": {"filename": "vent_grille.gcode", "total_duration": 5.2, "print_duration": 120.0, "state": "printing", "message": ""},
        "display_status": {"progress": 0.4307, "message": null}}}})");
    REQUIRE(printing);
    CHECK(printing->klippy == "ready");
    CHECK(printing->print == "printing");
    CHECK(printing->filename == "vent_grille.gcode");
    CHECK(printing->print_duration == 120.0);
    REQUIRE(printing->progress);
    CHECK_THAT(*printing->progress, Catch::Matchers::WithinAbs(0.4307, 1e-9));
    CHECK(host_activity(*printing) == HostActivity::Printing);

    KlipperState paused = *printing;
    paused.print        = "paused";
    CHECK(host_activity(paused) == HostActivity::Paused);
    for (const char* done : {"standby", "complete", "cancelled", "error"}) {
        KlipperState ready = *printing;
        ready.print        = done;
        INFO(done);
        CHECK(host_activity(ready) == HostActivity::Ready);
    }
}

TEST_CASE("a Klipper shutdown is the printer down, in Klipper's first line", "[host-lanes]")
{
    // The print it stopped still says "paused".
    const auto down = parse_klipper_state(R"({"result": {"status": {
        "webhooks": {"state": "shutdown", "state_message": "MCU 'mcu' shutdown: Timer too close\nThis often indicates the host computer is overloaded. Check\nPrinter is shutdown\n"},
        "print_stats": {"filename": "vent_grille.gcode", "print_duration": 0.0, "state": "paused", "message": ""},
        "display_status": {"progress": 0.0234, "message": null}}}})");
    REQUIRE(down);
    CHECK(down->klippy_message == "MCU 'mcu' shutdown: Timer too close");
    CHECK(host_activity(*down) == HostActivity::KlipperDown);
    CHECK(host_activity(KlipperState{"disconnected"}) == HostActivity::KlipperDown);
    CHECK(host_activity(KlipperState{"error"}) == HostActivity::KlipperDown);
    CHECK(host_activity(KlipperState{"startup"}) == HostActivity::Starting);
    CHECK_FALSE(parse_klipper_state(R"({"error": {"code": 503}})"));
    CHECK_FALSE(parse_klipper_state("<html>"));
}

TEST_CASE("the newest history job says how the last print ended", "[host-lanes]")
{
    const auto job = parse_last_job(R"({"result": {"count": 1, "jobs": [{"job_id": "000001",
        "filename": "vent_grille.gcode", "status": "klippy_shutdown", "start_time": 1793889583.3,
        "end_time": 1793889671.9, "metadata": {"estimated_time": 720, "slicer": "OrcaSlicer"}}]}})");
    REQUIRE(job);
    CHECK(job->filename == "vent_grille.gcode");
    CHECK(job->status == "klippy_shutdown");
    CHECK(job->estimated_time == 720.0);
    CHECK(job->end_time > 0.);
    // While it runs, Moonraker sends no end time.
    const auto running = parse_last_job(
        R"({"result": {"jobs": [{"filename": "a.gcode", "status": "in_progress", "end_time": null, "metadata": {}}]}})");
    REQUIRE(running);
    CHECK(running->end_time == 0.);
    CHECK(running->estimated_time == 0.);
    CHECK_FALSE(parse_last_job(R"({"result": {"count": 0, "jobs": []}})"));
}

TEST_CASE("the first enabled camera the card can show", "[host-lanes]")
{
    const auto docker = parse_webcams(R"({"result": {"webcams": [{"name": "printer", "enabled": true,
        "service": "mjpegstreamer", "stream_url": "http://127.0.0.1:7140/api/stream.mjpeg?src=klipper",
        "snapshot_url": "http://127.0.0.1:7140/api/frame.jpeg?src=klipper"}]}})",
                                      "http://127.0.0.1:7130/");
    REQUIRE(docker);
    CHECK(docker->stream_url == "http://127.0.0.1:7140/api/stream.mjpeg?src=klipper");
    CHECK(docker->snapshot_url == "http://127.0.0.1:7140/api/frame.jpeg?src=klipper");

    // Moonraker's sample paths are the web server's, on its default port.
    const auto relative = parse_webcams(R"({"result": {"webcams": [
        {"name": "off", "enabled": false, "service": "mjpegstreamer", "stream_url": "/off/?action=stream"},
        {"name": "cam", "enabled": true, "service": "mjpegstreamer", "stream_url": "/webcam/?action=stream", "snapshot_url": "webcam/?action=snapshot"}]}})",
                                        "http://voron.local:7125/");
    REQUIRE(relative);
    CHECK(relative->stream_url == "http://voron.local/webcam/?action=stream");
    CHECK(relative->snapshot_url == "http://voron.local/webcam/?action=snapshot");

    // A WebRTC camera shows only its picture; one with neither is none.
    const auto webrtc = parse_webcams(R"({"result": {"webcams": [{"name": "c", "service": "webrtc-camerastreamer",
        "stream_url": "http://pi/webrtc", "snapshot_url": "http://pi/snapshot"}]}})",
                                      "http://pi/");
    REQUIRE(webrtc);
    CHECK(webrtc->stream_url.empty());
    CHECK(webrtc->snapshot_url == "http://pi/snapshot");
    CHECK_FALSE(parse_webcams(R"({"result": {"webcams": [{"name": "c", "service": "hlsstream", "stream_url": "http://pi/hls"}]}})",
                              "http://pi/"));
    CHECK_FALSE(parse_webcams(R"({"result": {"webcams": []}})", "http://pi/"));
}

TEST_CASE("a reading keeps what Klipper said, and when the printer was last connected", "[host-lanes]")
{
    HostReadings readings;
    const auto   t0   = HostReadings::Clock::time_point{} + 1h;
    const auto   wall = HostReadings::Wall::time_point{} + 1000h;
    HostReading  reading;
    reading.reached = true;
    reading.klipper = KlipperState{"ready", "Printer is ready", "printing", "a.gcode"};
    reading.camera  = HostCamera{"http://cam/stream", ""};
    readings.begin("printer", t0);
    readings.finish("printer", reading, t0, wall);
    HostStatus status = readings.status("printer", t0 + 1s);
    REQUIRE(status.klipper);
    CHECK(status.klipper->print == "printing");
    REQUIRE(status.camera);
    CHECK(status.camera->stream_url == "http://cam/stream");
    REQUIRE(status.last_connected);
    CHECK(*status.last_connected == wall);

    // The Pi answers for a shut-down Klipper: the printer is not connected,
    // so it was last connected when Klipper was last up.
    reading.klipper = KlipperState{"shutdown", "Lost communication with MCU 'mcu'"};
    readings.begin("printer", t0 + kHostPollInterval);
    readings.finish("printer", reading, t0 + kHostPollInterval, wall + 10s);
    status = readings.status("printer", t0 + kHostPollInterval + 1s);
    CHECK(status.link == HostLink::Reachable);
    REQUIRE(status.last_connected);
    CHECK(*status.last_connected == wall);

    // Silent since: what it said is not now, but when it was last connected is.
    readings.begin("printer", t0 + 2 * kHostPollInterval);
    readings.finish("printer", HostReading{}, t0 + 2 * kHostPollInterval + kHostReadingFresh, wall + 1min);
    status = readings.status("printer", t0 + 2 * kHostPollInterval + kHostReadingFresh);
    CHECK(status.link == HostLink::Unreachable);
    CHECK_FALSE(status.klipper);
    CHECK_FALSE(status.camera);
    REQUIRE(status.last_connected);
    CHECK(*status.last_connected == wall);
}
