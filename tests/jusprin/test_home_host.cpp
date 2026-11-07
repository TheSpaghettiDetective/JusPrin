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
    std::string               projects_problem;
    std::vector<PrinterEntry> machines;

    std::vector<std::string> opened;
    std::vector<std::string> monitored;
    int                      new_projects{0};
    int                      file_opens{0};
    std::vector<DroppedFile> dropped;
    int                      wizards{0};
    // Each onboarding action, in the order the page sent them.
    std::vector<std::string> onboarding_calls;
    std::vector<std::string> chosen_printers;
    std::vector<json>        form_messages;
    bool                     onboarding_changed{false};
    bool                      dark() const override { return is_dark; }
    ProjectList               recent_projects() const override { return {projects, projects_problem}; }
    std::vector<PrinterEntry> printers() const override { return machines; }
    Snapshot::Onboarding onboarding_state;
    Snapshot::Onboarding onboarding() override { return onboarding_state; }

    void open_project(const std::string& id) override { opened.push_back(id); }
    void new_project() override { ++new_projects; }
    void open_file() override { ++file_opens; }
    void open_dropped_files(const std::vector<DroppedFile>& files) override
    {
        dropped.insert(dropped.end(), files.begin(), files.end());
    }
    void launch_monitor(const std::string& id) override { monitored.push_back(id); }
    void add_printer() override { ++wizards; }
    void open_preferences() override { ++preferences_opened; }

    bool reports_offered() const override { return offers_reports; }
    std::optional<PrintReportPhoto> choose_report_photo() override { return report_photo; }
    void send_print_report(const PrintReportForm& form, std::function<void(const PrintReportAnswer&)> done) override
    {
        report_forms.push_back(form);
        report_done = std::move(done);
    }
    bool offers_reports{false};
    std::optional<PrintReportPhoto> report_photo;
    std::vector<PrintReportForm> report_forms;
    std::function<void(const PrintReportAnswer&)> report_done;
    int  preferences_opened{0};
    bool poll_onboarding() override { return std::exchange(onboarding_changed, false); }
    void start_sign_in() override { onboarding_calls.push_back("sign_in"); }
    void cancel_sign_in() override { onboarding_calls.push_back("sign_in_cancel"); }
    void sign_in_with_email(const std::string& email) override { onboarding_calls.push_back("email:" + email); }
    void submit_sign_in_code(const std::string& code) override { onboarding_calls.push_back("code:" + code); }
    void submit_sign_in_password(const std::string& password) override { onboarding_calls.push_back("password:" + password); }
    void send_sign_in_code() override { onboarding_calls.push_back("send_code"); }
    void reopen_sign_in_browser() override { onboarding_calls.push_back("reopen"); }
    void open_sign_in_link(const std::string& link) override { onboarding_calls.push_back("link:" + link); }
    void open_agent_door(const std::string& door) override { onboarding_calls.push_back("door:" + door); }
    void leave_welcome() override { onboarding_calls.push_back("leave_welcome"); }
    void step_back() override { onboarding_calls.push_back("back"); }
    void start_fresh() override { onboarding_calls.push_back("start_fresh"); }
    void ask_agent_for_printer() override { onboarding_calls.push_back("ask_agent"); }
    std::string import_orca_printers(const std::vector<std::string>& printers) override
    {
        onboarding_calls.push_back("import");
        chosen_printers = printers;
        return refusal;
    }
    std::string assume_default_printer() override
    {
        onboarding_calls.push_back("assume_default");
        return refusal;
    }
    void finish_orca_import() override { onboarding_calls.push_back("import_continue"); }
    void printer_form_message(const json& payload) override { form_messages.push_back(payload); }
    void dismiss_account_card() override { onboarding_calls.push_back("dismiss_account_card"); }

    std::vector<std::string>                         settings_opened;
    std::vector<std::string>                         settings_asked;
    std::vector<std::string>                         connections_opened;
    std::vector<std::string>                         calibrations;
    std::vector<std::pair<std::string, std::string>> renamed;
    std::vector<std::string>                         removed;
    std::string                                      refusal; // what every printer action answers

    std::string open_printer_settings(const std::string& id) override
    {
        settings_opened.push_back(id);
        return refusal;
    }
    std::string ask_about_printer_settings(const std::string& id) override
    {
        settings_asked.push_back(id);
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
    std::string calibrate_printer(const std::string& id) override
    {
        calibrations.push_back(id);
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

std::string page_message(const std::string& type, const json& payload = json::object(), const std::string& id = "w-1")
{
    return json{{"protocol", "jusprin-home-bridge"},
                {"version", 1},
                {"id", id},
                {"type", type},
                {"payload", payload}}
        .dump();
}

std::string hello(int version = 1)
{
    return page_message("hello", json{{"protocolVersions", json::array({version})}, {"capabilities", json::array()}});
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
    CHECK(state.at("projectsProblem") == "");
    CHECK(host.connected());
}

// A projects folder that cannot be read is a different screen from an empty
// one: the page is told why there is nothing to show.
TEST_CASE("projects that could not be listed reach the page as a problem", "[home]")
{
    FakeBackend backend;
    backend.projects_problem = "Your projects couldn't be loaded.";
    Wire     wire;
    HomeHost host(backend, wire.sink());

    host.on_page_message(hello());

    const json state = wire.of_type("state").front().at("payload");
    CHECK(state.at("projects").empty());
    CHECK(state.at("projectsProblem") == "Your projects couldn't be loaded.");
}

// The live refresh keeps reading a projects folder it could not read, and
// fills the gallery in once it can, without the person leaving Home.
TEST_CASE("a gallery that could not be read fills in once it can", "[home]")
{
    FakeBackend backend;
    backend.projects_problem = "Your projects couldn't be loaded.";
    Wire     wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());

    CHECK_FALSE(host.refresh_if_changed());

    backend.projects_problem.clear();
    backend.projects = {a_project("0", "Garage bracket")};
    CHECK(host.refresh_if_changed());
    const json latest = wire.of_type("state").back().at("payload");
    CHECK(latest.at("projectsProblem") == "");
    CHECK(latest.at("projects").size() == 1);

    // Readable again, the projects go back to being read only with a send.
    CHECK_FALSE(host.refresh_if_changed());
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
    host.on_page_message(page_message("open_file"));

    CHECK(backend.opened.empty());
    CHECK(backend.new_projects == 0);
    CHECK(backend.file_opens == 0);
}

TEST_CASE("each page message reaches its own action", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message(page_message("open_project", json{{"id", "2"}}));
    host.on_page_message(page_message("new_project"));
    host.on_page_message(page_message("open_file"));
    host.on_page_message(page_message("launch_monitor", json{{"id", "FAKE001"}}));
    host.on_page_message(page_message("add_printer"));
    host.on_page_message(page_message("open_preferences"));

    CHECK(backend.opened == std::vector<std::string>{"2"});
    CHECK(backend.new_projects == 1);
    CHECK(backend.file_opens == 1);
    CHECK(backend.monitored == std::vector<std::string>{"FAKE001"});
    CHECK(backend.wizards == 1);
    CHECK(backend.preferences_opened == 1);
}

// The page hands over a dropped file's name and bytes; a file whose bytes do
// not decode, or that has no name, is left out rather than opened empty.
TEST_CASE("dropped files reach the backend with their bytes", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message(page_message("open_dropped_files",
                                      json{{"files", json::array({json{{"name", "cube.stl"}, {"dataBase64", "data:model/stl;base64,c29saWQgY3ViZQ=="}},
                                                                 json{{"name", "broken.stl"}, {"dataBase64", "not base64!"}},
                                                                 json{{"name", ""}, {"dataBase64", "c29saWQ="}}})}}));

    REQUIRE(backend.dropped.size() == 1);
    CHECK(backend.dropped[0].name == "cube.stl");
    CHECK(backend.dropped[0].bytes == "solid cube");

    host.on_page_message(page_message("open_dropped_files", json{{"files", json::array({json{{"name", "x.stl"}, {"dataBase64", "?"}}})}}));
    CHECK(backend.dropped.size() == 1); // nothing decodable, nothing opened
}

TEST_CASE("first-launch messages reach the backend and refresh the screen", "[home][onboarding]")
{
    FakeBackend backend;
    Wire wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states = wire.of_type("state").size();

    host.on_page_message(page_message("onboarding_sign_in"));
    host.on_page_message(page_message("onboarding_sign_in_cancel"));
    host.on_page_message(page_message("onboarding_leave_welcome"));
    host.on_page_message(page_message("onboarding_back"));
    host.on_page_message(page_message("onboarding_start_fresh"));
    host.on_page_message(page_message("onboarding_ask_agent"));
    host.on_page_message(page_message("onboarding_import", json{{"printers", {"My X1C", "Bambu Lab P1S 0.4 nozzle"}}}));
    host.on_page_message(page_message("onboarding_assume_default"));
    host.on_page_message(page_message("agent_card_dismiss"));
    host.on_page_message(page_message("onboarding_printer_form", json{{"action", "form_options"}, {"catalogId", "c1"}, {"nozzle", 0.4}}));

    CHECK(backend.onboarding_calls == std::vector<std::string>{"sign_in", "sign_in_cancel", "leave_welcome", "back", "start_fresh",
                                                               "ask_agent", "import", "assume_default", "dismiss_account_card"});
    CHECK(backend.chosen_printers == std::vector<std::string>{"My X1C", "Bambu Lab P1S 0.4 nozzle"});
    REQUIRE(backend.form_messages.size() == 1);
    CHECK(backend.form_messages.front().at("catalogId") == "c1");
    CHECK(wire.of_type("state").size() == states + 10);
}

// Preferences opens on the frame's next turn and is modal, so the page hears
// how it closed from the poll, not from this message.
TEST_CASE("the email door's messages reach the backend with what was typed", "[home][onboarding]")
{
    FakeBackend backend;
    Wire wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states = wire.of_type("state").size();

    host.on_page_message(page_message("onboarding_sign_in_email", json{{"email", "alex@example.com"}}));
    host.on_page_message(page_message("onboarding_sign_in_code", json{{"code", "418203"}}));
    host.on_page_message(page_message("onboarding_sign_in_password", json{{"password", "correct horse"}}));
    host.on_page_message(page_message("onboarding_sign_in_send_code"));
    host.on_page_message(page_message("onboarding_sign_in_reopen"));
    host.on_page_message(page_message("onboarding_open_link", json{{"link", "terms"}}));

    CHECK(backend.onboarding_calls == std::vector<std::string>{"email:alex@example.com", "code:418203", "password:correct horse",
                                                               "send_code", "reopen", "link:terms"});
    CHECK(wire.of_type("state").size() == states + 6);
}

TEST_CASE("the free doors open Preferences and the poll brings back how it closed", "[home][onboarding]")
{
    FakeBackend backend;
    Wire wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states = wire.of_type("state").size();

    host.on_page_message(page_message("onboarding_open_preferences", json{{"door", "key"}}));
    host.on_page_message(page_message("onboarding_open_preferences", json{{"door", "tool"}}));
    host.on_page_message(page_message("onboarding_open_preferences", json{{"door", "somewhere"}}));
    CHECK(backend.onboarding_calls == std::vector<std::string>{"door:key", "door:tool"});
    CHECK(wire.of_type("state").size() == states);

    CHECK_FALSE(host.poll());
    backend.onboarding_changed = true;
    CHECK(host.poll());
    CHECK(wire.of_type("state").size() == states + 1);
}

TEST_CASE("a printer the first launch could not bring says why and keeps the step", "[home][onboarding]")
{
    FakeBackend backend;
    backend.refusal = "JusPrin couldn't bring these printers. OrcaSlicer was not changed. Choose your printer instead.";
    Wire wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message(page_message("onboarding_import", json{{"printers", json::array()}}));

    const auto errors = wire.of_type("onboarding_error");
    REQUIRE(errors.size() == 1);
    CHECK(errors.front().at("payload").at("message") == backend.refusal);
    CHECK(wire.of_type("state").size() == 2);
}

TEST_CASE("the import result's Continue reaches the backend", "[home][onboarding]")
{
    FakeBackend backend;
    Wire wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states = wire.of_type("state").size();

    host.on_page_message(page_message("onboarding_import_continue"));
    CHECK(backend.onboarding_calls == std::vector<std::string>{"import_continue"});
    CHECK(wire.of_type("state").size() == states + 1);
    CHECK(wire.of_type("onboarding_error").empty());
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
                                   {"type", "open_file"},
                                   {"payload", json::object()}}
                                  .dump();
    host.on_page_message(agent);

    CHECK(backend.file_opens == 0);
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
    host.on_page_message(page_message("open_file"));

    CHECK(backend.file_opens == 1);
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

// A Calibrate… row's question (board 3), asked on Home: held until the page
// is there, then answered once with its request id.
TEST_CASE("a question asked on Home reaches the page and its answer comes back", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    std::vector<std::pair<std::string, bool>> answers;
    host.set_confirmation_listener([&](const std::string& id, bool confirmed) { answers.emplace_back(id, confirmed); });

    host.ask_confirmation({"calibrate-1", "Start a calibration test?", "Backpack hook will be saved and closed.", "Start test",
                           "Cancel"});
    CHECK(wire.of_type("confirm").empty());
    host.on_page_message(hello());
    REQUIRE(wire.of_type("confirm").size() == 1);
    CHECK(wire.of_type("confirm").front().at("payload") ==
          json{{"requestId", "calibrate-1"}, {"title", "Start a calibration test?"},
               {"body", "Backpack hook will be saved and closed."}, {"confirm", "Start test"}, {"cancel", "Cancel"}});

    host.on_page_message(page_message("confirm_answer", json{{"requestId", "calibrate-1"}, {"confirmed", true}}));
    CHECK(answers == std::vector<std::pair<std::string, bool>>{{"calibrate-1", true}});
}

TEST_CASE("the printer menu reaches its actions and refreshes the rail", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());
    const size_t states_after_hello = wire.of_type("state").size();

    host.on_page_message(page_message("open_printer_settings", json{{"id", "named:Garage X1C"}}));
    host.on_page_message(page_message("ask_about_printer_settings", json{{"id", "named:Garage X1C"}}));
    host.on_page_message(page_message("connect_printer", json{{"id", "named:Garage X1C"}}));
    host.on_page_message(page_message("calibrate_printer", json{{"id", "named:Garage X1C"}}));
    host.on_page_message(page_message("rename_printer", json{{"id", "named:Garage X1C"}, {"name", "Shed X1C"}}));
    host.on_page_message(page_message("remove_printer", json{{"id", "named:Shed X1C"}}));

    CHECK(backend.settings_opened == std::vector<std::string>{"named:Garage X1C"});
    CHECK(backend.settings_asked == std::vector<std::string>{"named:Garage X1C"});
    CHECK(backend.connections_opened == std::vector<std::string>{"named:Garage X1C"});
    CHECK(backend.calibrations == std::vector<std::string>{"named:Garage X1C"});
    CHECK(backend.wizards == 0);
    REQUIRE(backend.renamed.size() == 1);
    CHECK(backend.renamed.front() == std::make_pair(std::string("named:Garage X1C"), std::string("Shed X1C")));
    CHECK(backend.removed == std::vector<std::string>{"named:Shed X1C"});
    CHECK(wire.of_type("printer_error").empty());
    CHECK(wire.of_type("state").size() == states_after_hello + 6);
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

    // An id of its own, so the error is seen to answer this message, not the hello.
    host.on_page_message(page_message("rename_printer", json{{"id", "named:Garage X1C"}, {"name", "Office A1"}}, "w-2"));

    const auto errors = wire.of_type("printer_error");
    REQUIRE(errors.size() == 1);
    CHECK(errors.front().at("correlationId") == "w-2");
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
    garage.status          = PrinterStatus::Connected;
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

    backend.machines.front().status           = PrinterStatus::Printing;
    backend.machines.front().progress_percent = 43;
    CHECK(host.refresh_if_changed());
    json payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("status") == "printing");
    CHECK(payload.at("printers").at(0).at("progressPercent") == 43);
    // A send carries the whole screen, projects re-read with it.
    CHECK(payload.at("projects").size() == 2);
    CHECK_FALSE(host.refresh_if_changed());

    backend.machines.front().progress_percent = 44;
    CHECK(host.refresh_if_changed());
    backend.machines.front().status  = PrinterStatus::NotConnected;
    backend.machines.front().message = "The printer stopped sending updates.";
    CHECK(host.refresh_if_changed());
    CHECK(wire.of_type("state").back().at("payload").at("printers").at(0).at("status") == "not_connected");
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

    backend.machines.front().status = PrinterStatus::NotConnected;
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
    backend.machines.front().status = PrinterStatus::Printing;
    CHECK(host.refresh_if_changed());
    json payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("name") == "New Printer");
    CHECK(payload.at("highlightPrinter") == "New Printer");

    // The next explicit push is the ordinary order again, and so is every
    // live push after it.
    host.push_state();
    backend.machines.front().status = PrinterStatus::Ready;
    CHECK(host.refresh_if_changed());
    payload = wire.of_type("state").back().at("payload");
    CHECK(payload.at("printers").at(0).at("name") == "Existing Printer");
    CHECK(payload.at("highlightPrinter") == "");
}

TEST_CASE("the page chooses a report's photo through the host and hears back on it", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    HomeHost    host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message(page_message("print_report_choose_photo"));
    CHECK_FALSE(wire.of_type("print_report_photo").back().at("payload").contains("photo"));

    backend.report_photo = PrintReportPhoto{"1", "bracket.jpg", 2400000, "data:image/png;base64,AA"};
    host.on_page_message(page_message("print_report_choose_photo"));
    const json photo = wire.of_type("print_report_photo").back().at("payload").at("photo");
    CHECK(photo.at("id") == "1");
    CHECK(photo.at("name") == "bracket.jpg");
    CHECK(photo.at("sizeBytes") == 2400000);
    CHECK(photo.at("thumbnail") == "data:image/png;base64,AA");
}

TEST_CASE("a print report's answer reaches the page later, with the history as it now stands", "[home]")
{
    FakeBackend backend;
    backend.offers_reports = true;
    Wire     wire;
    HomeHost host(backend, wire.sink());
    host.on_page_message(hello());

    host.on_page_message(page_message("history_request"));
    CHECK(wire.of_type("history").back().at("payload").at("reportsOffered") == true);

    host.on_page_message(page_message("print_report_send", json{{"projectId", "p1"}, {"hash", "a71f"}, {"outcome", "partly"},
                                                             {"text", "It curled."}, {"photoId", "1"}}));
    REQUIRE(backend.report_forms.size() == 1);
    CHECK(backend.report_forms[0].project_id == "p1");
    CHECK(backend.report_forms[0].outcome == "partly");
    CHECK(backend.report_forms[0].photo_id == "1");
    const std::size_t before = wire.of_type("history").size();

    PrintReportAnswer sent;
    sent.state   = PrintReportAnswer::State::Sent;
    sent.credits = 25;
    backend.report_done(sent);
    const json result = wire.of_type("print_report_result").back().at("payload");
    CHECK(result.at("hash") == "a71f");
    CHECK(result.at("state") == "sent");
    CHECK(result.at("credits") == 25);
    CHECK(wire.of_type("history").size() == before + 1);
}

TEST_CASE("a print report answered after the host is gone goes nowhere", "[home]")
{
    FakeBackend backend;
    Wire        wire;
    std::function<void(const PrintReportAnswer&)> done;
    std::size_t sent = 0;
    {
        HomeHost host(backend, wire.sink());
        host.on_page_message(hello());
        host.on_page_message(page_message("print_report_send", json{{"hash", "a71f"}}));
        done = backend.report_done;
        sent = wire.sent.size();
    }
    done(PrintReportAnswer{});
    CHECK(wire.sent.size() == sent);
}
