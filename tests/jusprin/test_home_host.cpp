// Protocol tests for the Home bridge's native side. HomeHost reaches the
// application only through IHomeBackend, so the handshake, the state pushes,
// and the mapping from page message to product action can be exercised here
// with no wx, no Orca, and no web view.

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

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
