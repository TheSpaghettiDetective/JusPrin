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
    printer.connection_state   = ConnectionState::Online;
    printer.connection_text    = "Online - LAN";
    printer.connection_kind    = "lan";
    printer.model_text         = "X1 Carbon - 0.4 mm";
    printer.spools             = {SpoolEntry{"PLA", "#000000"}, SpoolEntry{"PETG", "#FFFFFF"}};
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
    CHECK(payload.at("highlightPrinter") == "");
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
    CHECK(printer.at("connectionState") == "online");
    CHECK(printer.at("connectionText") == "Online - LAN");
    CHECK(printer.at("connectionKind") == "lan");
    CHECK(printer.at("modelText") == "X1 Carbon - 0.4 mm");
    CHECK(printer.at("canLaunchMonitor") == true);
    CHECK_FALSE(printer.contains("address"));
    REQUIRE(printer.at("spools").size() == 2);
    CHECK(printer.at("spools").at(0).at("material") == "PLA");
    CHECK(printer.at("spools").at(0).at("colour") == "#000000");
    // The card's material now comes from what the printer holds; the
    // project's filament is not a fact about the printer.
    CHECK_FALSE(printer.contains("materialLabel"));
    CHECK_FALSE(printer.contains("nozzleText"));
}

// The dot and the connection row rest on the state; the words are the host's.
TEST_CASE("every connection state maps to the page's vocabulary", "[home]")
{
    Snapshot snapshot;
    snapshot.printers = {PrinterEntry{}};
    CHECK(state_payload(snapshot).at("printers").at(0).at("connectionState") == "none");
    const std::pair<ConnectionState, const char*> expected[] = {
        {ConnectionState::None, "none"},
        {ConnectionState::Online, "online"},
        {ConnectionState::Offline, "offline"},
        {ConnectionState::Connected, "connected"},
    };
    for (const auto& [state, text] : expected) {
        snapshot.printers[0].connection_state = state;
        INFO(text);
        CHECK(state_payload(snapshot).at("printers").at(0).at("connectionState") == text);
    }
}

// A spool described without a colour is still a material the card names; a
// sent empty colour would draw a swatch of nothing.
TEST_CASE("a spool without a colour sends its material and no colour", "[home]")
{
    Snapshot snapshot;
    PrinterEntry printer;
    printer.spools    = {SpoolEntry{"PLA", ""}, SpoolEntry{"", "#C8202D"}};
    snapshot.printers = {printer};
    const json spools = state_payload(snapshot).at("printers").at(0).at("spools");
    REQUIRE(spools.size() == 2);
    CHECK(spools.at(0).at("material") == "PLA");
    CHECK_FALSE(spools.at(0).contains("colour"));
    CHECK_FALSE(spools.at(1).contains("material"));
    CHECK(spools.at(1).at("colour") == "#C8202D");
}

// A print host is Connected on its saved address and offers its own page.
TEST_CASE("a print host carries its address and its page", "[home]")
{
    Snapshot snapshot;
    PrinterEntry host;
    host.id                 = "named:Voron";
    host.connection_state   = ConnectionState::Connected;
    host.connection_kind    = "host";
    host.address            = "192.168.1.42:7125";
    host.connection_text    = "Connected - 192.168.1.42:7125";
    host.can_launch_monitor = true;
    snapshot.printers       = {host};
    const json printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("connectionState") == "connected");
    CHECK(printer.at("connectionKind") == "host");
    CHECK(printer.at("address") == "192.168.1.42:7125");
    CHECK(printer.at("canLaunchMonitor") == true);
    CHECK_FALSE(printer.contains("connectionAction"));
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
    CHECK_FALSE(printer.contains("modelText"));
    CHECK_FALSE(printer.contains("connectionKind"));
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

// The page decides how to confirm an action from the card's kind, and never
// offers what the host said the card cannot do.
TEST_CASE("a printer carries its kind and which menu actions it offers", "[home]")
{
    Snapshot     snapshot;
    PrinterEntry named;
    named.id                = "named:Garage X1C";
    named.kind              = PrinterKind::Named;
    named.can_open_settings = true;
    named.can_rename        = true;
    named.can_remove        = true;
    PrinterEntry device;
    device.id   = "device:FAKE001";
    device.kind = PrinterKind::Device;
    snapshot.printers = {named, device};

    const json printers = state_payload(snapshot).at("printers");
    CHECK(printers.at(0).at("kind") == "named");
    CHECK(printers.at(0).at("canOpenSettings") == true);
    CHECK(printers.at(0).at("canRename") == true);
    CHECK(printers.at(0).at("canRemove") == true);
    CHECK(printers.at(1).at("kind") == "device");
    CHECK(printers.at(1).at("canOpenSettings") == false);
    CHECK(printers.at(1).at("canRename") == false);
    CHECK(printers.at(1).at("canRemove") == false);
}

TEST_CASE("Home connection actions do not depend on translated status text", "[home]")
{
    Snapshot snapshot;
    snapshot.printers = {a_printer()};
    snapshot.printers[0].connection_action = "reconnect";
    snapshot.printers[0].connection_text   = "Localized status";
    CHECK(state_payload(snapshot)["printers"][0]["connectionAction"] == "reconnect");
    // No button is no field.
    snapshot.printers[0].connection_action.clear();
    CHECK_FALSE(state_payload(snapshot)["printers"][0].contains("connectionAction"));
}
