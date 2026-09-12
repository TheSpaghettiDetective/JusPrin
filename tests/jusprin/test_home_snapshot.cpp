// Contract tests for the Home page's `state` payload. The page reads these
// exact field names (src/slic3r/GUI/JusPrin/HomeUI/src/bridge/protocol.ts) and
// treats a missing optional field as "the host has nothing to say", so the
// omission rules below are part of the protocol, not a formatting detail.

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include "slic3r/GUI/JusPrin/Home/HomeSnapshot.hpp"

using namespace Slic3r::GUI::JusPrin::Home;
using nlohmann::json;

namespace {

ProjectEntry a_project()
{
    ProjectEntry project;
    project.id          = "p1";
    project.name        = "Garage bracket";
    project.path        = "C:/projects/garage.3mf";
    project.status_kind = ProjectStatusKind::Sliced;
    project.status_text = "Sliced - ready to send";
    return project;
}

PrinterEntry a_printer()
{
    PrinterEntry printer;
    printer.id                 = "x1";
    printer.name               = "X1 Carbon";
    printer.state              = PrinterState::Printing;
    printer.status_text        = "Printing - 43% - 2h left";
    printer.progress_percent   = 43;
    printer.connection_text    = "Connected - LAN";
    printer.nozzle_text        = "0.4 mm nozzle";
    printer.material_label     = "PLA";
    printer.spools             = {SpoolEntry{"#000000"}, SpoolEntry{"#FFFFFF"}};
    printer.can_launch_monitor = true;
    return printer;
}

} // namespace

TEST_CASE("the payload carries the three fields the page reads", "[home]")
{
    Snapshot snapshot;
    snapshot.dark = true;
    const json payload = state_payload(snapshot);
    CHECK(payload.at("appearance") == "dark");
    CHECK(payload.at("projects").is_array());
    CHECK(payload.at("printers").is_array());
    CHECK(payload.at("projects").empty());
}

TEST_CASE("appearance is the page's own vocabulary, not a boolean", "[home]")
{
    Snapshot snapshot;
    CHECK(state_payload(snapshot).at("appearance") == "light");
    snapshot.dark = true;
    CHECK(state_payload(snapshot).at("appearance") == "dark");
}

TEST_CASE("a project carries its identity and its status", "[home]")
{
    Snapshot snapshot;
    snapshot.projects = {a_project()};
    const json project = state_payload(snapshot).at("projects").at(0);
    CHECK(project.at("id") == "p1");
    CHECK(project.at("name") == "Garage bracket");
    CHECK(project.at("path") == "C:/projects/garage.3mf");
    CHECK(project.at("status").at("kind") == "sliced");
    CHECK(project.at("status").at("text") == "Sliced - ready to send");
}

// A card without a thumbnail must keep its frame. The page decides that from
// the field being absent, so an empty string would draw a broken image.
TEST_CASE("a missing thumbnail is an absent field, not an empty string", "[home]")
{
    Snapshot snapshot;
    snapshot.projects = {a_project()};
    CHECK_FALSE(state_payload(snapshot).at("projects").at(0).contains("thumbnailUrl"));

    snapshot.projects[0].thumbnail_url = "file:///thumbs/garage.png";
    CHECK(state_payload(snapshot).at("projects").at(0).at("thumbnailUrl") == "file:///thumbs/garage.png");
}

// Nothing records per-project slice state yet, so the host says so rather than
// guessing a kind the card would then decorate.
TEST_CASE("every status kind maps to the page's vocabulary", "[home]")
{
    Snapshot snapshot;
    snapshot.projects = {a_project()};
    const std::pair<ProjectStatusKind, const char*> expected[] = {
        {ProjectStatusKind::Unknown, "unknown"},
        {ProjectStatusKind::Draft, "draft"},
        {ProjectStatusKind::Sliced, "sliced"},
        {ProjectStatusKind::Printing, "printing"},
        {ProjectStatusKind::Completed, "completed"},
    };
    for (const auto& [kind, text] : expected) {
        snapshot.projects[0].status_kind = kind;
        INFO(text);
        CHECK(state_payload(snapshot).at("projects").at(0).at("status").at("kind") == text);
    }
}

TEST_CASE("a printing printer carries its job, its bar, and its spools", "[home]")
{
    Snapshot snapshot;
    snapshot.printers = {a_printer()};
    const json printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("state") == "printing");
    CHECK(printer.at("statusText") == "Printing - 43% - 2h left");
    CHECK(printer.at("progressPercent") == 43);
    CHECK(printer.at("connectionText") == "Connected - LAN");
    CHECK(printer.at("nozzleText") == "0.4 mm nozzle");
    CHECK(printer.at("materialLabel") == "PLA");
    CHECK(printer.at("canLaunchMonitor") == true);
    REQUIRE(printer.at("spools").size() == 2);
    CHECK(printer.at("spools").at(0).at("colour") == "#000000");
}

// An idle printer is one collapsed row. Sending a bar or a job line for it
// would make the two states look alike, which is the thing the design most
// wants to avoid.
TEST_CASE("an idle printer sends no progress bar", "[home]")
{
    Snapshot snapshot;
    PrinterEntry idle;
    idle.id               = "mk4";
    idle.name             = "Prusa MK4";
    idle.state            = PrinterState::Idle;
    idle.progress_percent = 43; // stale value from a finished job
    snapshot.printers     = {idle};
    const json printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("state") == "idle");
    CHECK_FALSE(printer.contains("progressPercent"));
    CHECK_FALSE(printer.contains("statusText"));
    CHECK(printer.at("spools").empty());
    CHECK(printer.at("canLaunchMonitor") == false);
}

TEST_CASE("a printing printer with no percentage yet sends no bar", "[home]")
{
    Snapshot snapshot;
    PrinterEntry starting = a_printer();
    starting.progress_percent = -1;
    snapshot.printers         = {starting};
    const json printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("state") == "printing");
    CHECK_FALSE(printer.contains("progressPercent"));
    CHECK(printer.at("statusText") == "Printing - 43% - 2h left");
}

TEST_CASE("an offline printer is its own state", "[home]")
{
    Snapshot snapshot;
    PrinterEntry offline;
    offline.state     = PrinterState::Offline;
    snapshot.printers = {offline};
    CHECK(state_payload(snapshot).at("printers").at(0).at("state") == "offline");
}
