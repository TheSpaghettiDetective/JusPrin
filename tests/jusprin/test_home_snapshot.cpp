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
    project.status_kind = ProjectStatusKind::Sent;
    project.status_text = "Sent Oct 20 - X1 Carbon - PLA";
    return project;
}

PrinterEntry a_printer()
{
    PrinterEntry printer;
    printer.id                 = "x1";
    printer.name               = "X1 Carbon";
    printer.status           = PrinterStatus::Printing;
    printer.job              = "Vent grille";
    printer.progress_percent = 43;
    printer.seconds_left     = 7200;
    printer.connection_label = "LAN";
    printer.model_text       = "X1 Carbon - 0.4 mm";
    printer.spools           = {SpoolEntry{"PLA", "#000000"}, SpoolEntry{"PETG", "#FFFFFF"}};
    printer.action           = PrinterAction::Monitor;
    return printer;
}

} // namespace

TEST_CASE("the payload carries the fields the page reads, empty when there is nothing to show", "[home]")
{
    Snapshot snapshot;
    snapshot.dark = true;
    const json payload = state_payload(snapshot);
    CHECK(payload.at("appearance") == "dark");
    CHECK(payload.at("projects").is_array());
    CHECK(payload.at("printers").is_array());
    CHECK(payload.at("projects").empty());
    CHECK(payload.at("projectsProblem") == "");
    CHECK(payload.at("highlightPrinter") == "");
    CHECK(payload.at("onboarding").at("visible") == false);
    CHECK(payload.at("onboarding").at("step") == "hidden");
}

TEST_CASE("onboarding state uses the page contract", "[home][onboarding]")
{
    Snapshot snapshot;
    snapshot.onboarding.step             = OnboardingStep::Printers;
    snapshot.onboarding.printers_screen  = PrintersScreen::OrcaImport;
    snapshot.onboarding.sign_in          = "browser";
    snapshot.onboarding.sign_in_error    = "That sign-in expired. Try again.";
    snapshot.onboarding.account_email    = "kim@example.com";
    snapshot.onboarding.agent_configured = true;
    snapshot.onboarding.orca_source      = "/tmp/orca";
    snapshot.onboarding.orca_printers    = {"Voron 2.4 0.4 nozzle"};
    snapshot.onboarding.orca_selected    = "Voron 2.4 0.4 nozzle";
    snapshot.onboarding.orca_filaments   = 3;
    snapshot.onboarding.agent_card       = HomeAgentCard::Account;
    snapshot.onboarding.account          = json{{"credits", json{{"total", 500}}}};

    const json state = state_payload(snapshot).at("onboarding");
    CHECK(state.at("visible") == true);
    CHECK(state.at("step") == "printers");
    CHECK(state.at("printersScreen") == "orca_import");
    CHECK(state.at("signIn") == "browser");
    CHECK(state.at("signInError") == "That sign-in expired. Try again.");
    CHECK(state.at("accountEmail") == "kim@example.com");
    CHECK(state.at("agentConfigured") == true);
    CHECK(state.at("orca").at("source") == "/tmp/orca");
    CHECK(state.at("orca").at("printers") == json::array({"Voron 2.4 0.4 nozzle"}));
    CHECK(state.at("orca").at("selected") == "Voron 2.4 0.4 nozzle");
    CHECK(state.at("orca").at("filamentCount") == 3);
    // No form is open on the OrcaSlicer screen.
    CHECK(state.at("printerForm").is_null());
    CHECK(state.at("agentCard") == "account");
    CHECK(state.at("account").at("credits").at("total") == 500);
}

TEST_CASE("an import's result lists each item for the page", "[home][onboarding]")
{
    Snapshot snapshot;
    snapshot.onboarding.step            = OnboardingStep::Printers;
    snapshot.onboarding.printers_screen = PrintersScreen::ImportResult;
    CHECK(state_payload(snapshot).at("onboarding").at("orca").at("importResult").is_null());

    snapshot.onboarding.facts.orca_printers = true;
    snapshot.onboarding.import_result       = std::vector<Snapshot::Onboarding::ImportItem>{
        {"printer", "Shop X1C", "Shop X1C (Orca copy)", true, ""},
        {"filament", "Broken PLA", "", false, "JusPrin couldn't read this file. To try again, choose File › Import › Import from OrcaSlicer…"},
    };
    const json state = state_payload(snapshot).at("onboarding");
    CHECK(state.at("printersScreen") == "import_result");
    CHECK(state.at("orca").at("found") == true);
    const json& items = state.at("orca").at("importResult");
    REQUIRE(items.size() == 2);
    CHECK(items.at(0) == json{{"kind", "printer"}, {"name", "Shop X1C"}, {"renamedTo", "Shop X1C (Orca copy)"},
                              {"imported", true}, {"reason", ""}});
    CHECK(items.at(1).at("imported") == false);
    CHECK(items.at(1).at("reason") == "JusPrin couldn't read this file. To try again, choose File › Import › Import from OrcaSlicer…");
}

TEST_CASE("only the first launch's default printer is marked assumed", "[home][onboarding]")
{
    Snapshot snapshot;
    PrinterEntry chosen;
    chosen.id   = "a";
    chosen.name = "Prusa MK4";
    PrinterEntry assumed = chosen;
    assumed.id      = "b";
    assumed.name    = "Generic Klipper Printer";
    assumed.assumed = true;
    snapshot.printers = {chosen, assumed};

    const json printers = state_payload(snapshot).at("printers");
    CHECK_FALSE(printers.at(0).contains("assumed"));
    CHECK(printers.at(1).at("assumed") == true);
}

TEST_CASE("a printer with no connection ever set up says so to the page", "[home]")
{
    Snapshot snapshot;
    PrinterEntry added;
    added.id            = "a";
    added.name          = "Prusa MK4";
    added.no_connection = true;
    PrinterEntry hosted = added;
    hosted.id            = "b";
    hosted.no_connection = false;
    snapshot.printers    = {added, hosted};

    const json printers = state_payload(snapshot).at("printers");
    CHECK(printers.at(0).at("noConnection") == true);
    CHECK_FALSE(printers.at(1).contains("noConnection"));
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
    CHECK(project.at("status").at("kind") == "sent");
    CHECK(project.at("status").at("text") == "Sent Oct 20 - X1 Carbon - PLA");
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

// The page words the date itself, so it gets the moment and not a phrase.
TEST_CASE("a project's last save is an ISO time, absent when unknown", "[home]")
{
    Snapshot snapshot;
    snapshot.projects = {a_project()};
    CHECK_FALSE(state_payload(snapshot).at("projects").at(0).contains("updatedAt"));

    snapshot.projects[0].updated_at = "2026-10-24T18:05:00Z";
    CHECK(state_payload(snapshot).at("projects").at(0).at("updatedAt") == "2026-10-24T18:05:00Z");
}

TEST_CASE("every status kind maps to the page's vocabulary", "[home]")
{
    Snapshot snapshot;
    snapshot.projects = {a_project()};
    const std::pair<ProjectStatusKind, const char*> expected[] = {
        {ProjectStatusKind::Unknown, "unknown"},
        {ProjectStatusKind::Draft, "draft"},
        {ProjectStatusKind::Sent, "sent"},
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
    CHECK(printer.at("status") == "printing");
    CHECK(printer.at("job") == "Vent grille");
    CHECK(printer.at("progressPercent") == 43);
    CHECK(printer.at("secondsLeft") == 7200);
    CHECK(printer.at("connectionLabel") == "LAN");
    CHECK(printer.at("modelText") == "X1 Carbon - 0.4 mm");
    CHECK(printer.at("action") == "monitor");
    CHECK_FALSE(printer.contains("address"));
    CHECK_FALSE(printer.contains("message"));
    CHECK_FALSE(printer.contains("camera"));
    REQUIRE(printer.at("spools").size() == 2);
    CHECK(printer.at("spools").at(0).at("material") == "PLA");
    CHECK(printer.at("spools").at(0).at("colour") == "#000000");
    // The old two-part state is gone: one status says it all.
    for (const char* gone : {"state", "statusText", "connectionState", "connectionText", "canLaunchMonitor"})
        CHECK_FALSE(printer.contains(gone));
}

TEST_CASE("every status, last print and action maps to the page's vocabulary", "[home]")
{
    Snapshot snapshot;
    snapshot.printers = {PrinterEntry{}};
    CHECK(state_payload(snapshot).at("printers").at(0).at("status") == "not_connected");
    CHECK(state_payload(snapshot).at("printers").at(0).at("action") == "none");
    const std::pair<PrinterStatus, const char*> statuses[] = {
        {PrinterStatus::NotConnected, "not_connected"}, {PrinterStatus::Connecting, "connecting"},
        {PrinterStatus::Connected, "connected"},        {PrinterStatus::Ready, "ready"},
        {PrinterStatus::Printing, "printing"},          {PrinterStatus::Paused, "paused"},
        {PrinterStatus::NeedsAttention, "attention"},
    };
    for (const auto& [status, text] : statuses) {
        snapshot.printers[0].status = status;
        INFO(text);
        CHECK(state_payload(snapshot).at("printers").at(0).at("status") == text);
    }
    const std::pair<PrinterAction, const char*> actions[] = {
        {PrinterAction::Monitor, "monitor"},     {PrinterAction::PrinterWindow, "printer_window"},
        {PrinterAction::Reconnect, "reconnect"}, {PrinterAction::Connect, "connect"},
    };
    for (const auto& [action, text] : actions) {
        snapshot.printers[0].action = action;
        INFO(text);
        CHECK(state_payload(snapshot).at("printers").at(0).at("action") == text);
    }
    snapshot.printers[0].status = PrinterStatus::Ready;
    snapshot.printers[0].last_print = LastPrint::Finished;
    CHECK(state_payload(snapshot).at("printers").at(0).at("lastPrint") == "finished");
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

// A print host's card says how it is reached and where, and its camera.
TEST_CASE("a print host carries its label, its address and its camera", "[home]")
{
    Snapshot snapshot;
    PrinterEntry host;
    host.id                  = "named:Voron";
    host.status              = PrinterStatus::Connected;
    host.connection_label    = "OctoPrint";
    host.address             = "192.168.1.42:7125";
    host.camera_stream_url   = "http://192.168.1.42/webcam/?action=stream";
    host.camera_snapshot_url = "http://192.168.1.42/webcam/?action=snapshot";
    host.action              = PrinterAction::PrinterWindow;
    snapshot.printers        = {host};
    const json printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("status") == "connected");
    CHECK(printer.at("connectionLabel") == "OctoPrint");
    CHECK(printer.at("address") == "192.168.1.42:7125");
    CHECK(printer.at("camera").at("streamUrl") == "http://192.168.1.42/webcam/?action=stream");
    CHECK(printer.at("camera").at("snapshotUrl") == "http://192.168.1.42/webcam/?action=snapshot");
    CHECK(printer.at("action") == "printer_window");
}

// A job's bar and time left belong to a job that runs; a ready printer's
// stale values from a finished job are not sent, and neither is a "Last
// connected" on a printer that is connected.
TEST_CASE("a field is sent only on the statuses it belongs to", "[home]")
{
    Snapshot snapshot;
    PrinterEntry ready;
    ready.status            = PrinterStatus::Ready;
    ready.progress_percent  = 100;
    ready.seconds_left      = 60;
    ready.last_connected_at = "2026-11-05T12:00:00Z";
    snapshot.printers       = {ready};
    json printer = state_payload(snapshot).at("printers").at(0);
    CHECK_FALSE(printer.contains("progressPercent"));
    CHECK_FALSE(printer.contains("secondsLeft"));
    CHECK_FALSE(printer.contains("lastConnectedAt"));
    CHECK_FALSE(printer.contains("lastPrint"));
    CHECK_FALSE(printer.contains("modelText"));
    CHECK(printer.at("spools").empty());

    snapshot.printers[0].status = PrinterStatus::NotConnected;
    printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("lastConnectedAt") == "2026-11-05T12:00:00Z");

    snapshot.printers[0].status     = PrinterStatus::Paused;
    snapshot.printers[0].last_print = LastPrint::Finished;
    printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("progressPercent") == 100);
    CHECK_FALSE(printer.contains("lastPrint"));
}

TEST_CASE("a printing printer with no percentage yet sends no bar", "[home]")
{
    Snapshot snapshot;
    PrinterEntry starting = a_printer();
    starting.progress_percent = -1;
    starting.seconds_left     = -1;
    snapshot.printers         = {starting};
    const json printer = state_payload(snapshot).at("printers").at(0);
    CHECK(printer.at("status") == "printing");
    CHECK_FALSE(printer.contains("progressPercent"));
    CHECK_FALSE(printer.contains("secondsLeft"));
    CHECK(printer.at("job") == "Vent grille");
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
