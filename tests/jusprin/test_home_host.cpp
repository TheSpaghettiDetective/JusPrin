// Protocol tests for the Home bridge's native side. HomeHost reaches the
// application only through IHomeBackend, so the handshake, the state pushes,
// and the mapping from page message to product action can be exercised here
// with no wx, no Orca, and no web view.

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

#include "slic3r/GUI/JusPrin/Home/HomeHost.hpp"

using namespace Slic3r::GUI::JusPrin::Home;
using nlohmann::json;

namespace {

// Records what Home asked the application to do, and answers its reads.
class FakeBackend final : public IHomeBackend
{
public:
    bool                      is_dark{false};
    std::vector<ProjectEntry> projects;
    std::vector<PrinterEntry> machines;

    std::vector<std::string> opened;
    std::vector<std::string> monitored;
    int                      new_projects{0};
    int                      imports{0};
    int                      wizards{0};

    bool                      dark() const override { return is_dark; }
    std::vector<ProjectEntry> recent_projects() const override { return projects; }
    std::vector<PrinterEntry> printers() const override { return machines; }

    void open_project(const std::string& id) override { opened.push_back(id); }
    void new_project() override { ++new_projects; }
    void import_model() override { ++imports; }
    void launch_monitor(const std::string& id) override { monitored.push_back(id); }
    void add_printer() override { ++wizards; }

    std::vector<std::string>                         settings_opened;
    std::vector<std::string>                         connections_opened;
    std::vector<std::pair<std::string, std::string>> renamed;
    std::vector<std::string>                         removed;
    std::string                                      refusal; // what every printer action answers

    std::string open_printer_settings(const std::string& id) override
    {
        settings_opened.push_back(id);
        return refusal;
    }
    std::string rename_printer(const std::string& id, const std::string& name) override
    {
        renamed.emplace_back(id, name);
        return refusal;
    }
    std::string connect_printer(const std::string& id) override
    {
        connections_opened.push_back(id);
        return refusal;
    }
    std::string remove_printer(const std::string& id) override
    {
        removed.push_back(id);
        return refusal;
    }
};

// Collects the envelopes the host sends to the page.
struct Wire
{
    std::vector<json> sent;

    HomeHost::Send sink()
    {
        return [this](const std::string& text) { sent.push_back(json::parse(text)); };
    }

    std::vector<json> of_type(const std::string& type) const
    {
        std::vector<json> found;
        for (const json& envelope : sent)
            if (envelope.at("type") == type)
                found.push_back(envelope);
        return found;
    }
};

std::string page_message(const std::string& type, const json& payload = json::object())
{
    return json{{"protocol", "jusprin-home-bridge"},
                {"version", 1},
                {"id", "w-1"},
                {"type", type},
                {"payload", payload}}
        .dump();
}

std::string hello(int version = 1)
{
    return page_message("hello",
                        json{{"protocolVersions", json::array({version})}, {"capabilities", json::array()}});
}

ProjectEntry a_project(const std::string& id, const std::string& name)
{
    ProjectEntry project;
    project.id          = id;
    project.name        = name;
    project.status_kind = ProjectStatusKind::Unknown;
    project.status_text = "Edited 2026-09-09 18:14";
    return project;
}

} // namespace

TEST_CASE("the handshake is answered with an ack and the whole screen", "[home]")
{
    FakeBackend backend;
    backend.projects = {a_project("0", "Garage bracket")};
    Wire     wire;
    HomeHost host(backend, wire.sink());

    host.on_page_message(hello());

    REQUIRE(wire.of_type("hello_ack").size() == 1);
    REQUIRE(wire.of_type("state").size() == 1);
    const json state = wire.of_type("state").front().at("payload");
    CHECK(state.at("projects").size() == 1);
    CHECK(state.at("projects").at(0).at("name") == "Garage bracket");
    CHECK(host.connected());
}

// A page speaking a version this build does not must be told so, not left
// waiting for a state that never comes.
TEST_CASE("a page speaking another version is rejected", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());

    host.on_page_message(hello(99));

    CHECK(wire.of_type("hello_reject").size() == 1);
    CHECK(wire.of_type("state").empty());
    CHECK_FALSE(host.connected());
}

// Nothing but hello is answered before the handshake, so a page that starts
// sending actions early cannot drive the application.
TEST_CASE("actions before the handshake are ignored", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());

    host.on_page_message(page_message("open_project", json{{"id", "0"}}));
    host.on_page_message(page_message("new_project"));

    CHECK(backend.opened.empty());
    CHECK(backend.new_projects == 0);
}

TEST_CASE("each page message reaches its own action", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message(page_message("open_project", json{{"id", "2"}}));
    host.on_page_message(page_message("new_project"));
    host.on_page_message(page_message("import_project"));
    host.on_page_message(page_message("launch_monitor", json{{"id", "FAKE001"}}));
    host.on_page_message(page_message("add_printer"));

    CHECK(backend.opened == std::vector<std::string>{"2"});
    CHECK(backend.new_projects == 1);
    CHECK(backend.imports == 1);
    CHECK(backend.monitored == std::vector<std::string>{"FAKE001"});
    CHECK(backend.wizards == 1);
}

// Adding a printer changes what the rail should show, so the screen is pushed
// again without the page having to ask.
TEST_CASE("adding a printer refreshes the screen", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states_after_hello = wire.of_type("state").size();

    host.on_page_message(page_message("add_printer"));

    CHECK(wire.of_type("state").size() == states_after_hello + 1);
}

// A printer the conversation just added leads the column and is highlighted,
// for that one push; an ordinary refresh reorders and highlights nothing.
TEST_CASE("a printer just added leads the column, for one push", "[home]")
{
    FakeBackend backend;
    PrinterEntry existing;
    existing.id   = "named:Existing";
    existing.name = "Existing Printer";
    PrinterEntry added;
    added.id   = "named:New";
    added.name = "New Printer";
    backend.machines = {existing, added};
    Wire     wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());

    host.push_state("New Printer");
    json payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("name") == "New Printer");
    CHECK(payload.at("highlightPrinter") == "New Printer");

    host.push_state();
    payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("name") == "Existing Printer");
    CHECK(payload.at("highlightPrinter") == "");
    // A name Home does not list is no highlight at all.
    host.push_state("Gone Printer");
    CHECK(wire.of_type("state").back().at("payload").at("highlightPrinter") == "");
}

TEST_CASE("state_request sends the screen again", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    backend.projects = {a_project("0", "Vent grille")};
    host.on_page_message(page_message("state_request"));

    const json latest = wire.of_type("state").back().at("payload");
    CHECK(latest.at("projects").at(0).at("name") == "Vent grille");
}

// A reload has to introduce itself again: the host must not keep answering
// the page that went away.
TEST_CASE("a page reload invalidates the handshake", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    host.reset_page();
    CHECK_FALSE(host.connected());
    host.on_page_message(page_message("new_project"));
    CHECK(backend.new_projects == 0);

    host.push_state();
    CHECK(wire.of_type("state").size() == 1); // the one from the first hello
}

TEST_CASE("traffic from another surface is ignored", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    const std::string agent = json{{"protocol", "jusprin-agent-bridge"},
                                   {"version", 1},
                                   {"id", "w-9"},
                                   {"type", "new_project"},
                                   {"payload", json::object()}}
                                  .dump();
    host.on_page_message(agent);

    CHECK(backend.new_projects == 0);
}

// The page is the only writer on this transport; unreadable text is dropped
// rather than taking the bridge down with it.
TEST_CASE("unreadable page text does not break the bridge", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message("{not json");
    host.on_page_message(page_message("new_project"));

    CHECK(backend.new_projects == 1);
}

TEST_CASE("appearance follows the host, and only while connected", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());

    host.push_appearance(true);
    CHECK(wire.of_type("appearance").empty());

    host.on_page_message(hello());
    host.push_appearance(true);
    REQUIRE(wire.of_type("appearance").size() == 1);
    CHECK(wire.of_type("appearance").front().at("payload").at("appearance") == "dark");
}

TEST_CASE("the printer menu reaches its actions and refreshes the rail", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states_after_hello = wire.of_type("state").size();

    host.on_page_message(page_message("open_printer_settings", json{{"id", "named:Garage X1C"}}));
    host.on_page_message(page_message("connect_printer", json{{"id", "named:Garage X1C"}}));
    host.on_page_message(page_message("rename_printer", json{{"id", "named:Garage X1C"}, {"name", "Shed X1C"}}));
    host.on_page_message(page_message("remove_printer", json{{"id", "named:Shed X1C"}}));

    CHECK(backend.settings_opened == std::vector<std::string>{"named:Garage X1C"});
    CHECK(backend.connections_opened == std::vector<std::string>{"named:Garage X1C"});
    CHECK(backend.wizards == 0);
    REQUIRE(backend.renamed.size() == 1);
    CHECK(backend.renamed.front() == std::make_pair(std::string("named:Garage X1C"), std::string("Shed X1C")));
    CHECK(backend.removed == std::vector<std::string>{"named:Shed X1C"});
    CHECK(wire.of_type("printer_error").empty());
    CHECK(wire.of_type("state").size() == states_after_hello + 4);
}

// A refusal is the person's to read, and the rail is sent again either way so
// the page never shows a name the host did not accept.
TEST_CASE("a refused printer action reaches the page with its reason", "[home]")
{
    FakeBackend backend;
    backend.refusal = "Another printer or profile is already named \"Office A1\".";
    Wire     wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states_after_hello = wire.of_type("state").size();

    host.on_page_message(page_message("rename_printer", json{{"id", "named:Garage X1C"}, {"name", "Office A1"}}));

    const auto errors = wire.of_type("printer_error");
    REQUIRE(errors.size() == 1);
    CHECK(errors.front().at("correlationId") == "w-1");
    CHECK(errors.front().at("payload").at("id") == "named:Garage X1C");
    CHECK(errors.front().at("payload").at("message") == backend.refusal);
    CHECK(wire.of_type("state").size() == states_after_hello + 1);
}

// Home stays on screen while its printers change. The live path sends only
// when a card would look different, so a quiet printer costs the page
// nothing, and a printer that starts printing or drops off reaches it.
TEST_CASE("a live refresh sends only when a card changed", "[home]")
{
    FakeBackend  backend;
    PrinterEntry garage;
    garage.id              = "named:Garage A1 mini";
    garage.name            = "Garage A1 mini";
    garage.connection_text = "Connected";
    backend.machines       = {garage};
    backend.projects       = {a_project("0", "Garage bracket")};
    Wire     wire;
    HomeHost host(backend, wire.sink());

    CHECK_FALSE(host.refresh_if_changed()); // nothing to send before the handshake
    host.on_page_message(hello());
    const size_t states_after_hello = wire.of_type("state").size();

    CHECK_FALSE(host.refresh_if_changed());
    // Projects alone are not what the live path watches.
    backend.projects.push_back(a_project("1", "Vent grille"));
    CHECK_FALSE(host.refresh_if_changed());
    CHECK(wire.of_type("state").size() == states_after_hello);

    backend.machines.front().state            = PrinterState::Printing;
    backend.machines.front().progress_percent = 43;
    CHECK(host.refresh_if_changed());
    json payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("state") == "printing");
    CHECK(payload.at("printers").at(0).at("progressPercent") == 43);
    // A send carries the whole screen, projects re-read with it.
    CHECK(payload.at("projects").size() == 2);
    CHECK_FALSE(host.refresh_if_changed());

    backend.machines.front().progress_percent = 44;
    CHECK(host.refresh_if_changed());
    backend.machines.front().state            = PrinterState::Offline;
    backend.machines.front().connection_state = ConnectionState::Offline;
    backend.machines.front().connection_text  = "Offline";
    CHECK(host.refresh_if_changed());
    CHECK(wire.of_type("state").back().at("payload").at("printers").at(0).at("state") == "offline");
    CHECK(wire.of_type("state").size() == states_after_hello + 3);

    // A progress value the page never draws -- no job, so no bar -- is no
    // reason to send.
    backend.machines.front().progress_percent = 90;
    CHECK_FALSE(host.refresh_if_changed());
}

// The comparison is against what the page was last sent, whichever path
// sent it, so a live refresh never repeats an explicit push.
TEST_CASE("a live refresh after an explicit push sends nothing new", "[home]")
{
    FakeBackend  backend;
    PrinterEntry garage;
    garage.id        = "named:Garage A1 mini";
    garage.name      = "Garage A1 mini";
    backend.machines = {garage};
    Wire     wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());

    backend.machines.front().state = PrinterState::Offline;
    host.push_state();
    const size_t states = wire.of_type("state").size();
    CHECK_FALSE(host.refresh_if_changed());
    CHECK(wire.of_type("state").size() == states);
}

// A printer the conversation just added keeps leading the column, and keeps
// its highlight, across live pushes: the page would otherwise move the card
// out from under the person and cut its glow short.
TEST_CASE("a live refresh keeps the added printer leading and highlighted", "[home]")
{
    FakeBackend  backend;
    PrinterEntry existing;
    existing.id   = "named:Existing";
    existing.name = "Existing Printer";
    PrinterEntry added;
    added.id         = "named:New";
    added.name       = "New Printer";
    backend.machines = {existing, added};
    Wire     wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());
    host.push_state("New Printer");

    CHECK_FALSE(host.refresh_if_changed());
    backend.machines.front().state = PrinterState::Printing;
    CHECK(host.refresh_if_changed());
    json payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("name") == "New Printer");
    CHECK(payload.at("highlightPrinter") == "New Printer");

    // The next explicit push is the ordinary order again, and so is every
    // live push after it.
    host.push_state();
    backend.machines.front().state = PrinterState::Idle;
    CHECK(host.refresh_if_changed());
    payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("name") == "Existing Printer");
    CHECK(payload.at("highlightPrinter") == "");
}
