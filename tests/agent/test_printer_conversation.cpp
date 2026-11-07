// The printer panel's session: what the model is told, what each of its tools
// checks and does, and what the app says on its own. The first half drives
// the conversation alone, with a fake backend and a recording panel; the
// second runs it inside a real AgentHost with a scripted model, to pin what
// actually reaches the model and what never does.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.hpp"
#include "../jusprin_support/FakeWorkspace.hpp"
#include "kept_turn.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <tuple>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::PrinterSetup;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using nlohmann::json;

namespace {

CatalogPrinter mini()
{
    CatalogPrinter printer;
    printer.id              = "BBL/Bambu Lab A1 mini";
    printer.vendor_id       = "BBL";
    printer.vendor_name     = "Bambu Lab";
    printer.model_id        = "Bambu Lab A1 mini";
    printer.model_name      = "A1 mini";
    printer.device_model_id = "N1";
    printer.build_volume    = "180 × 180 × 180 mm";
    printer.default_plate   = "Textured PEI Plate";
    printer.nozzles         = {0.2, 0.4, 0.6, 0.8};
    printer.filaments       = {"Bambu PLA Basic @BBL A1M 0.2 nozzle", "Bambu PLA Basic @BBL A1M", "Bambu PLA Basic @BBL A1M",
                               "Bambu PLA Basic @BBL A1M"};
    return printer;
}

// No plate in its profile, like most printers, and a nozzle list without 0.4
// on one of them.
CatalogPrinter mk3s()
{
    CatalogPrinter printer;
    printer.id           = "Prusa/Prusa MK3S";
    printer.vendor_id    = "Prusa";
    printer.vendor_name  = "Prusa";
    printer.model_id     = "Prusa MK3S";
    printer.model_name   = "MK3S";
    printer.build_volume = "250 × 210 × 210 mm";
    printer.nozzles      = {0.25, 0.4, 0.6, 0.8};
    printer.filaments    = {"Prusa Generic PLA", "Prusa Generic PLA", "Prusa Generic PLA", "Prusa Generic PLA"};
    return printer;
}

CatalogPrinter ender(const std::string& name)
{
    CatalogPrinter printer;
    printer.id           = "Creality/Creality " + name;
    printer.vendor_id    = "Creality";
    printer.vendor_name  = "Creality";
    printer.model_id     = "Creality " + name;
    printer.model_name   = name;
    printer.build_volume = "220 × 220 × 250 mm";
    printer.nozzles      = {0.6, 0.8};
    printer.filaments    = {"Creality Generic PLA", ""};
    return printer;
}

class FakeBackend final : public IPrinterBackend
{
public:
    std::vector<CatalogPrinter>    catalogue{mini(), mk3s(), ender("Ender-3 V2"), ender("Ender-3 S1"), ender("Ender-3 Pro"),
                                          ender("Ender-3 Max")};
    std::vector<DiscoveredPrinter> network;
    std::vector<SavedPrinter>      saved;
    std::string                    refusal;

    std::vector<AddPrinterRequest>    added;
    std::vector<ChangePrinterRequest> changed;
    int                               manual_setups{0};
    std::vector<std::string>          settings_opened;

    // OrcaSlicer's own printer profiles a person could select.
    std::vector<SavedPrinter>      stock;

    const std::vector<CatalogPrinter>& catalog() const override { return catalogue; }
    std::vector<DiscoveredPrinter>     network_printers() const override { return network; }
    // Everything found, the printers without an access code included.
    std::vector<DiscoveredPrinter>     nearby;
    std::vector<DiscoveredPrinter>     nearby_printers() const override { return nearby; }
    // What the printer form is offered, by catalogue id and nozzle.
    std::map<std::pair<std::string, double>, SetupOptions> options;
    SetupOptions setup_options(const std::string& id, double nozzle) const override
    {
        SetupOptions found = options.at({id, nozzle});
        found.catalog_id   = id;
        found.nozzle       = nozzle;
        return found;
    }
    std::vector<SavedPrinter>          saved_printers() const override { return saved; }
    SavedPrinter                       stock_printer(const std::string& name) const override
    {
        for (SavedPrinter printer : stock)
            if (printer.name == name) {
                printer.stock = true;
                return printer;
            }
        return {};
    }

    // Saves the printer into `saved`, as the Orca backend does.
    std::string add_printer(const AddPrinterRequest& request, SavedPrinter& result) override
    {
        added.push_back(request);
        if (!refusal.empty())
            return refusal;
        result.name      = request.name;
        result.model     = request.name;
        result.model_id  = request.model_id;
        result.vendor_id = request.vendor_id;
        result.nozzle    = std::stod(request.variant);
        saved.push_back(result);
        return {};
    }
    // Saves the change into `saved`, as the Orca backend does.
    std::vector<std::string> dropped; // what a change reports it discarded
    std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& result, std::vector<std::string>& discarded) override
    {
        changed.push_back(request);
        discarded = dropped;
        if (!refusal.empty())
            return refusal;
        for (SavedPrinter& printer : saved)
            if (printer.name == request.name) {
                if (request.nozzle)
                    printer.nozzle = *request.nozzle;
                result = printer;
            }
        return {};
    }
    std::vector<std::string> removed;
    // Takes the printer out of `saved`, as the Orca backend does.
    std::string remove_printer(const std::string& name) override
    {
        removed.push_back(name);
        if (!refusal.empty())
            return refusal;
        saved.erase(std::remove_if(saved.begin(), saved.end(), [&name](const SavedPrinter& printer) { return printer.name == name; }),
                    saved.end());
        return {};
    }
    ManualPrinterResult      manual_result;
    PrinterConnectionInfo    connection_info;
    std::vector<std::string> prepared;
    std::string              connection_error;
    // What each connect was handed, the credential included.
    std::vector<std::vector<std::string>> connects;
    PrinterConnectionInfo connection(const std::string&) override { return connection_info; }
    void prepare_connection(const std::string& name) override { prepared.push_back(name); }
    std::vector<std::string> kept_reports;
    std::string              keep_report_path{"/data/report.json"};
    std::string keep_connection_report(const std::string& text) override
    {
        kept_reports.push_back(text);
        return keep_report_path;
    }
    DiscoveryPort port{DiscoveryPort::Ours};
    int           port_checks{0};
    DiscoveryPort discovery_port() override
    {
        ++port_checks;
        return port;
    }
    std::string connect_printer(const std::string& name, const std::string& device, const std::string& code) override
    {
        connects.push_back({name, device, code});
        return connection_error;
    }
    // The test's end, for a test to call with the outcome it wants.
    std::function<void(const HostTestOutcome&)> tested;
    std::string connect_host(const std::string& name, const std::string& type, const std::string& address, const std::string& key,
                             std::function<void(const HostTestOutcome&)> end) override
    {
        connects.push_back({name, type, address, key});
        if (connection_error.empty())
            tested = std::move(end);
        return connection_error;
    }
    std::vector<std::string> cancelled;
    void cancel_connection(const std::string& name) override { cancelled.push_back(name); }
    ManualPrinterResult run_manual_setup() override { ++manual_setups; return manual_result; }
    // What each start_with_filament was handed, and what it answers.
    std::vector<std::tuple<std::string, std::string, bool>> starts;
    std::string                                             start_problem;
    std::string start_with_filament(const std::string& printer, const std::string& filament, bool copy, std::string& used) override
    {
        starts.emplace_back(printer, filament, copy);
        if (!start_problem.empty())
            return start_problem;
        used = copy ? filament + " @" + printer : filament;
        return {};
    }
    // The printers a preset was made for, and the name of the one made.
    std::vector<std::string> creates;
    std::string              created;
    std::string create_filament_preset(const std::string& printer) override
    {
        creates.push_back(printer);
        return created;
    }
    void open_printer_settings(const std::string& name) override { settings_opened.push_back(name); }
    // The installer: counts runs, and loads the plug-in only when told to.
    int  plugin_installs{0};
    bool install_succeeds{false};
    void install_network_plugin() override
    {
        ++plugin_installs;
        if (install_succeeds) {
            connection_info.needs_network_plugin = false;
            connection_info.state                = "not_configured";
            connection_info.message.clear();
        }
    }
};

class RecordingPanel final : public IConversationHost
{
public:
    std::vector<std::string> notes;
    std::vector<std::string> openings;
    int                      turns{0};
    int                      states{0};
    int                      profiles{0};
    int                      closes{0};
    int                      refreshes{0};

    nlohmann::json chat_record() const override { return json{{"turns", json::array()}}; }
    std::string post_note(const std::string& text) override
    {
        notes.push_back(text);
        return "note-" + std::to_string(notes.size());
    }
    std::string post_opening(const std::string& text) override
    {
        openings.push_back(text);
        return "opening-" + std::to_string(openings.size());
    }
    void start_turn() override { ++turns; }
    // Each call ended later, with its outcome; false once the call is gone.
    std::vector<std::pair<std::string, Agent::ToolExecutionCoordinator::ExtensionResult>> finished;
    bool                                                                                  finishes{true};
    bool finish_tool(const std::string& action_id, const Agent::ToolExecutionCoordinator::ExtensionResult& outcome) override
    {
        finished.emplace_back(action_id, outcome);
        return finishes;
    }
    // Each retitle of a waiting call, and whether its dialogs were answered from then on.
    std::vector<std::tuple<std::string, std::string, bool>> updates;
    bool update_tool(const std::string& action_id, const std::string& title, bool answer_dialogs) override
    {
        updates.emplace_back(action_id, title, answer_dialogs);
        return finishes;
    }
    void session_changed() override { ++states; }
    void profile_changed() override { ++profiles; }
    void close_panel() override { ++closes; }
    std::vector<std::string> opened; // each page a browser was asked to open
    bool                     browser_opens{true};
    bool open_in_browser(const std::string& url) override
    {
        opened.push_back(url);
        return browser_opens;
    }
    std::string app_version() const override { return "02.06.00.51"; }
    std::vector<std::string> added_printers; // what each refresh named, empty for none
    void printers_changed(const std::string& added) override
    {
        ++refreshes;
        added_printers.push_back(added);
    }
};

Agent::ToolActivity call(const char* tool, const json& arguments, const std::string& message = "m-1")
{
    Agent::ToolActivity activity;
    activity.tool           = tool;
    activity.correlation_id = message;
    activity.arguments_json = arguments.dump();
    return activity;
}

// Runs one of the session's tools as the coordinator would, after the
// registry has checked the call.
Agent::ToolExecutionCoordinator::ExtensionResult run(PrinterConversation& conversation, const char* tool, const json& arguments,
                                                     const std::string& message = "m-1")
{
    const auto& registry = Agent::ToolRegistry::instance();
    const Agent::ToolDefinition* definition = registry.find(tool);
    REQUIRE(definition != nullptr);
    REQUIRE(registry.validate_call(*definition, arguments.dump()).valid());
    return conversation.execute_tool(definition->handler, call(tool, arguments, message));
}

// The same, for a call that succeeds: its output, checked against the
// registry's promise of what the tool returns.
json ran(PrinterConversation& conversation, const char* tool, const json& arguments, const std::string& message = "m-1")
{
    const auto result = run(conversation, tool, arguments, message);
    REQUIRE(result.handled);
    INFO((result.error ? result.error->message : std::string()));
    REQUIRE_FALSE(result.error.has_value());
    const json output = json::parse(result.result_json);
    CHECK(Agent::ToolRegistry::instance().validate_output(*Agent::ToolRegistry::instance().find(tool), output));
    return output;
}

std::string refused(PrinterConversation& conversation, const char* tool, const json& arguments)
{
    const auto result = run(conversation, tool, arguments);
    REQUIRE(result.error.has_value());
    return result.error->code;
}

DiscoveredPrinter found_a1()
{
    DiscoveredPrinter printer;
    printer.stable_id       = "01P00A3B";
    printer.name            = "Bambu Lab A1 mini";
    printer.device_model_id = "N1";
    printer.connection      = "LAN";
    printer.connected       = true;
    printer.nozzle_diameter = 0.4;
    printer.ams_name        = "AMS lite";
    printer.spools          = {{"PLA Matte", "#5f7d4f", "PLA"}, {"PETG HF", "#204080", "PETG"}};
    return printer;
}

SavedPrinter lab_printer()
{
    SavedPrinter saved;
    saved.name     = "Lab Printer";
    saved.model    = "Bambu Lab A1 mini";
    saved.model_id = "Bambu Lab A1 mini";
    saved.nozzle   = 0.4;
    saved.nozzles  = {0.2, 0.4, 0.6, 0.8};
    saved.plate    = "Textured PEI Plate";
    saved.ams      = "AMS lite";
    saved.spools   = {PrinterSpool{"PLA Matte", "PLA", "#5f7d4f"}, PrinterSpool{"PETG", "PETG", "#204080"}};
    return saved;
}

// The words that would make a note an order rather than a record.
void check_is_a_statement(const std::string& note)
{
    INFO(note);
    for (const char* order : {"printer_", "Propose", "propose", "Find ", "call ", "Call ", "Say ", "say that", "Ask "})
        CHECK_THAT(note, !ContainsSubstring(order));
}

} // namespace

// -- What the model is told --------------------------------------------------

TEST_CASE("every session offers every printer tool and the settings tools, and nothing of the project", "[printer-conversation]")
{
    // The settings tools could reach the project's own settings; the
    // session's preflight refuses every scope but its printer's.
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    std::vector<std::string> printer_tools;
    // All but issue_report_open, which comes with the connection skill.
    for (const Agent::ToolDefinition& definition : Agent::ToolRegistry::instance().exposed(Agent::ToolExposure::Printer))
        if (definition.name != "issue_report_open")
            printer_tools.push_back(definition.name);
    for (const std::string& name : PrinterConversation::settings_tools())
        printer_tools.push_back(name);

    for (const ConversationMode mode : {ConversationMode::Add, ConversationMode::Change, ConversationMode::Connect}) {
        PrinterConversation conversation(backend, panel);
        conversation.start(mode, "Lab Printer");
        const Agent::AgentSessionProfile profile = conversation.profile();
        CHECK(profile.tool_names == printer_tools);
        CHECK_FALSE(profile.include_workspace);
    }
}

TEST_CASE("an Add session sends the page every printer, what is on the network, and the ways in", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json state = conversation.state_json();
    CHECK(state.at("mode") == "add");
    CHECK(state.at("printerName") == "");
    const json& context = state.at("context");
    REQUIRE(context.at("printers").size() == backend.catalogue.size());
    CHECK(context["printers"][0] == json::array({"BBL/Bambu Lab A1 mini", "Bambu Lab A1 mini", "180 × 180 × 180 mm"}));
    CHECK(context.at("network") == json::array({json{{"name", "Bambu Lab A1 mini"}, {"serial", "01P00A3B"}}}));
    CHECK_FALSE(context.contains("printer"));
    const json& blocks = state.at("blocks");
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].at("kind") == "tip");
    CHECK(blocks[1].at("printers")[0] == json{{"name", "Bambu Lab A1 mini"}, {"serial", "01P00A3B"}, {"online", true}});
}

TEST_CASE("a Change session sends the page the printer as it is now", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    backend.saved.front().vendor_id = "BBL";
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const json state = conversation.state_json();
    CHECK(state.at("printerName") == "Lab Printer");
    CHECK(state.at("blocks").empty());
    const json& context = state.at("context");
    CHECK_FALSE(context.contains("printers"));
    const json& printer = context.at("printer");
    CHECK(printer.at("name") == "Lab Printer");
    CHECK(printer.at("nozzle") == 0.4);
    CHECK(printer.at("nozzles") == json::array({0.2, 0.4, 0.6, 0.8}));
    CHECK(printer.at("spools")[0] == json{{"name", "PLA Matte"}, {"material", "PLA"}, {"colour", "#5f7d4f"}});
    CHECK(printer.at("provider") == "bambu");
}

TEST_CASE("a change to the stock profile the project has selected stays a change about that profile", "[printer-conversation]")
{
    // The header's Printer settings… on a project that uses one of Orca's
    // own profiles once opened a conversation about adding a printer.
    FakeBackend backend;
    SavedPrinter shipped = lab_printer();
    shipped.name         = "Bambu Lab A1 mini 0.4 nozzle";
    shipped.copy_name    = "Bambu Lab A1 mini 0.4 nozzle - Copy";
    backend.stock        = {shipped};
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(conversation.mode() == ConversationMode::Change);
    CHECK(conversation.printer_name() == "Bambu Lab A1 mini 0.4 nozzle");
    const json printer = conversation.state_json().at("context").at("printer");
    CHECK(printer.at("name") == "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(printer.at("stock") == true);
    // The copy a change is saved as, named for the model up front.
    CHECK(printer.at("copyName") == "Bambu Lab A1 mini 0.4 nozzle - Copy");
}

TEST_CASE("a change for a printer that is not there is its opener's bug, never a conversation about adding one", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    CHECK_THROWS_AS(conversation.start(ConversationMode::Change, "Bambu Lab A1 mini 0.4 nozzle"), std::logic_error);
    CHECK_THROWS_AS(conversation.start(ConversationMode::Connect, "Lab Printer"), std::logic_error);
}

TEST_CASE("the model's instructions are the page's words, and a new session waits for new ones", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    CHECK(conversation.profile().instructions.empty());

    REQUIRE(conversation.handle_page_message("printer_instructions", json{{"text", "You add printers."}}));
    CHECK(conversation.profile().instructions == "You add printers.");
    CHECK(panel.profiles == 1);
    conversation.handle_page_message("printer_instructions", json{{"text", "You add printers."}});
    CHECK(panel.profiles == 1);
    // Nothing the size of no prompt the page means to send.
    conversation.handle_page_message("printer_instructions", json{{"text", std::string(300 * 1024, 'x')}});
    CHECK(conversation.profile().instructions == "You add printers.");

    conversation.start(ConversationMode::Add);
    CHECK(conversation.profile().instructions.empty());
}

TEST_CASE("the page's opening is posted once and the first cards sit under it", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    REQUIRE(conversation.handle_page_message("printer_opening", json{{"text", ""}}));
    REQUIRE(conversation.handle_page_message("printer_opening", json{{"text", std::string(3 * 1024, 'x')}}));
    CHECK(panel.openings.empty());

    conversation.handle_page_message("printer_opening", json{{"text", "What printer do you have?"}});
    conversation.handle_page_message("printer_opening", json{{"text", "What printer do you have?"}});
    CHECK(panel.openings == std::vector<std::string>{"What printer do you have?"});
    CHECK(conversation.state_json().at("blocks")[0].at("afterMessageId") == "opening-1");
    CHECK(panel.turns == 0);
}

TEST_CASE("the panel answers only its own messages, and Back closes it", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    CHECK_FALSE(conversation.handle_page_message("user_message", json::object()));
    REQUIRE(conversation.handle_page_message("printer_action", json{{"action", "close"}}));
    CHECK(panel.closes == 1);
    // The actions the old panel's buttons sent do nothing now.
    for (const char* old : {"add", "reject", "undo", "connection_start", "network_pick"})
        conversation.handle_page_message("printer_action", json{{"action", old}});
    CHECK(backend.added.empty());
    CHECK(backend.connects.empty());
}

// -- printer_identify -------------------------------------------------------

TEST_CASE("printer_identify draws the printers named and returns what to say about them", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json output = ran(conversation, "printer_identify", json{{"catalogIds", {"BBL/Bambu Lab A1 mini", "Prusa/Prusa MK3S"}}}, "m-7");
    const json& mini = output.at("printers")[0];
    CHECK(mini.at("assumed") == json{{"nozzle", 0.4}, {"plate", "Textured PEI Plate"}, {"filament", "Bambu PLA Basic @BBL A1M"}});
    CHECK(mini.at("alreadyYours") == true);
    // A plate the profile does not name is null, never a guess.
    CHECK(output.at("printers")[1].at("assumed").at("plate").is_null());

    const json state = conversation.state_json();
    const json& card = state.at("blocks").back();
    CHECK(card.at("afterMessageId") == "m-7");
    CHECK(card.at("printers")[0] == json{{"catalogId", "BBL/Bambu Lab A1 mini"}, {"name", "Bambu Lab A1 mini"},
                                         {"buildVolume", "180 × 180 × 180 mm"}, {"picture", ""}});
    // It saves nothing.
    CHECK(backend.added.empty());
    CHECK(panel.refreshes == 0);
}

TEST_CASE("the assumed nozzle is 0.4, else the first size, else what was said", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    json output = ran(conversation, "printer_identify", json{{"catalogIds", {"Creality/Creality Ender-3 V2"}}});
    CHECK(output["printers"][0]["assumed"]["nozzle"] == 0.6);
    output = ran(conversation, "printer_identify", json{{"catalogIds", {"Creality/Creality Ender-3 V2"}}, {"nozzle", 0.8}});
    CHECK(output["printers"][0]["assumed"]["nozzle"] == 0.8);
    CHECK(output["printers"][0]["assumed"]["filament"].is_null());
    // null and 0 are how the model leaves the nozzle unsaid.
    for (const json& unsaid : {json(nullptr), json(0)}) {
        output = ran(conversation, "printer_identify", json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}, {"nozzle", unsaid}});
        CHECK(output["printers"][0]["assumed"]["nozzle"] == 0.4);
    }
}

TEST_CASE("printer_identify refuses what it cannot show, and says what to do instead", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    CHECK(refused(conversation, "printer_identify", json{{"catalogIds", {"Creality/Creality Ender-3 V2", "Creality/Creality Ender-3 S1",
                                                                        "Creality/Creality Ender-3 Pro", "Creality/Creality Ender-3 Max"}}}) == "too_many");
    CHECK(refused(conversation, "printer_identify", json{{"catalogIds", {"BBL/N1"}}}) == "unknown_printer");
    const auto result = run(conversation, "printer_identify", json{{"catalogIds", {"Prusa/Prusa MK3S"}}, {"nozzle", 0.3}});
    REQUIRE(result.error.has_value());
    CHECK(result.error->message == "JusPrin supports 0.25, 0.4, 0.6 and 0.8 mm nozzles for Prusa MK3S, but not 0.3 mm. Ask the person to "
                                   "check the nozzle marking or packaging; do not substitute a size.");
    // Only the tip: nothing refused was drawn.
    CHECK(conversation.state_json().at("blocks").size() == 1);
}

// -- printer_add and printer_change ------------------------------------------

TEST_CASE("printer_add saves once and the session is then about that printer", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json output = ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}, {"nozzle", 0.6}}, "m-4");
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added[0].model_id == "Prusa MK3S");
    CHECK(backend.added[0].variant == "0.6");
    CHECK(backend.added[0].material == "Prusa Generic PLA");
    CHECK(backend.added[0].device_id.empty());
    CHECK(output.at("printer").at("name") == "Prusa MK3S");
    CHECK(output.at("printer").at("nozzle") == 0.6);
    // Home leads with it; the page and the model now have a printer to talk about.
    CHECK(panel.added_printers == std::vector<std::string>{"Prusa MK3S"});
    CHECK(conversation.printer_name() == "Prusa MK3S");
    CHECK(conversation.state_json().at("context").at("printer").at("name") == "Prusa MK3S");
    CHECK(panel.turns == 0);
    CHECK(panel.notes.empty());
    // Its receipt follows the message that added it, stating what was saved.
    const json receipt = conversation.state_json().at("blocks").back();
    CHECK(receipt.at("kind") == "added");
    CHECK(receipt.at("afterMessageId") == "m-4");
    CHECK(receipt.at("printer") == json{{"name", "Prusa MK3S"}, {"nozzle", 0.6}});

    // A second yes is not a second printer, nor a second receipt.
    const size_t blocks = conversation.state_json().at("blocks").size();
    CHECK(refused(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}) == "already_added");
    CHECK(backend.added.size() == 1);
    CHECK(conversation.state_json().at("blocks").size() == blocks);
    // A new session may add another of the same model.
    conversation.start(ConversationMode::Add);
    ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}});
    CHECK(backend.added.size() == 2);
}

TEST_CASE("printer_add refuses an unknown model, an unshipped nozzle, and passes on a failed save", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    CHECK(refused(conversation, "printer_add", json{{"catalogId", "BBL/N1"}}) == "unknown_printer");
    CHECK(refused(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}, {"nozzle", 0.3}}) == "unknown_nozzle");
    CHECK(backend.added.empty());
    backend.refusal = "That name is taken.";
    const auto result = run(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}});
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "add_failed");
    CHECK(result.error->message == "That name is taken.");
    CHECK(panel.refreshes == 0);
    // Nothing was saved, so nothing says it was: only the tip is drawn.
    CHECK(conversation.state_json().at("blocks").size() == 1);
}

TEST_CASE("printer_change saves what changed and returns the printer as it now is", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const json output = ran(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}});
    REQUIRE(backend.changed.size() == 1);
    CHECK(backend.changed[0].nozzle == 0.6);
    CHECK(output.at("changed") == json::array({json{{"field", "nozzle"}, {"before", 0.4}, {"after", 0.6}}}));
    CHECK(output.at("discarded").empty());
    CHECK(output.at("printer").at("nozzle") == 0.6);
    CHECK(conversation.state_json().at("context").at("printer").at("nozzle") == 0.6);
    CHECK(panel.refreshes == 1);

    // Putting it back is the same tool; unsaved edits it drops are listed.
    backend.dropped = {"retraction_length"};
    CHECK(ran(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.4}}).at("discarded") ==
          json::array({"retraction_length"}));
    CHECK(backend.saved[0].nozzle == 0.4);
}

TEST_CASE("printer_change refuses what it cannot change, before saving anything", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", nullptr}}) == "nothing_to_change");
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.4}}) == "nothing_to_change");
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.3}}) == "unknown_nozzle");
    CHECK(backend.changed.empty());
    backend.refusal = "The profile is read-only.";
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}) == "change_failed");
}

// What the app refuses before a call runs, as the host asks it; empty when
// the call may go ahead.
std::string preflight_refusal(PrinterConversation& conversation, const char* tool, const json& arguments)
{
    const Agent::ToolDefinition* definition = Agent::ToolRegistry::instance().find(tool);
    REQUIRE(definition != nullptr);
    Agent::ToolActivity activity = call(tool, arguments);
    const auto problem = conversation.preflight_tool(definition->handler, activity);
    return problem ? problem->code : std::string();
}

TEST_CASE("a session changes the settings of its own printer, and of nothing else", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   garage = lab_printer();
    garage.name           = "Garage";
    backend.saved         = {lab_printer(), garage};
    PrinterConversation conversation(backend, panel);
    // The printer tools, then the settings tools.
    std::vector<std::string> offered = PrinterConversation::session_tools();
    for (const std::string& name : PrinterConversation::settings_tools())
        offered.push_back(name);
    CHECK(conversation.profile().tool_names == offered);

    conversation.start(ConversationMode::Add);
    CHECK(preflight_refusal(conversation, "settings_get", json{{"scope", "printer"}, {"target", {{"preset", "Lab Printer"}}}, {"keys", {"z_hop"}}}) ==
          "no_printer");

    conversation.start(ConversationMode::Change, "Lab Printer");
    const json own{{"preset", "Lab Printer"}};
    CHECK(preflight_refusal(conversation, "settings_get", json{{"scope", "printer"}, {"target", own}, {"keys", {"z_hop"}}}).empty());
    CHECK(preflight_refusal(conversation, "settings_search", json{{"scope", "printer"}, {"target", own}, {"query", "g-code"}}).empty());
    CHECK(preflight_refusal(conversation, "settings_preview_patch",
                            json{{"scope", "printer"}, {"target", own}, {"changes", {{"z_hop", "0.4"}}}, {"persistAs", "Lab Printer"}})
              .empty());
    // The project's own settings are not this conversation's.
    CHECK(preflight_refusal(conversation, "settings_preview_patch", json{{"scope", "process"}, {"changes", {{"wall_loops", "3"}}}}) ==
          "not_in_this_conversation");
    CHECK(preflight_refusal(conversation, "settings_get",
                            json{{"scope", "filament"}, {"target", {{"preset", "Generic PLA"}}}, {"keys", {"nozzle_temperature"}}}) ==
          "not_in_this_conversation");
    CHECK(preflight_refusal(conversation, "settings_apply_patch",
                            json{{"scope", "printer"}, {"target", {{"preset", "Garage"}}}, {"changes", {{"z_hop", "0.4"}}}}) ==
          "other_printer");
}

TEST_CASE("a stock profile's nozzle and connection are not saved, and its copy is the printer from then on", "[printer-conversation]")
{
    FakeBackend  backend;
    SavedPrinter shipped = lab_printer();
    shipped.name         = "Bambu Lab A1 mini 0.4 nozzle";
    backend.stock        = {shipped};
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, shipped.name);
    CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", shipped.name}, {"nozzle", 0.6}}) == "stock_profile");
    CHECK(preflight_refusal(conversation, "printer_connection_status", json::object()) == "stock_profile");
    CHECK(preflight_refusal(conversation, "printer_identify", json{{"catalogIds", {"Prusa/Prusa MK3S"}}}) == "not_in_this_conversation");

    // Saved as a copy: Orca selected the copy, which is a printer of the
    // person's own, so the conversation is about it and Home lists it.
    Agent::ToolActivity applied = call("settings_apply_patch", json{{"scope", "printer"}, {"target", {{"preset", shipped.name}}}});
    applied.state       = Agent::ToolState::Succeeded;
    applied.result_json = json{{"savedAs", "Bambu Lab A1 mini 0.4 nozzle - Copy"}}.dump();
    SavedPrinter copy   = lab_printer();
    copy.name           = "Bambu Lab A1 mini 0.4 nozzle - Copy";
    backend.saved       = {copy};
    const int states    = panel.states;
    conversation.tool_settled(applied);
    CHECK(conversation.printer_name() == "Bambu Lab A1 mini 0.4 nozzle - Copy");
    CHECK(panel.added_printers == std::vector<std::string>{"Bambu Lab A1 mini 0.4 nozzle - Copy"});
    CHECK(panel.states == states + 1);
    CHECK_FALSE(conversation.state_json().at("context").at("printer").contains("stock"));
    CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", copy.name}, {"nozzle", 0.6}}).empty());

    // Saved in place, or not saved: still the same printer.
    applied.result_json = json{{"savedAs", copy.name}}.dump();
    conversation.tool_settled(applied);
    applied.result_json = json{{"savedAs", ""}}.dump();
    conversation.tool_settled(applied);
    CHECK(panel.added_printers.size() == 1);
}

TEST_CASE("a session about a saved printer finds and adds none, and changes only that printer", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   garage = lab_printer();
    garage.name           = "Garage";
    backend.saved         = {lab_printer(), garage};
    for (const char* removed : {"printer_manual_setup", "printer_manual_connection", "printer_setup_finish"})
        CHECK(Agent::ToolRegistry::instance().find(removed) == nullptr);
    for (const ConversationMode mode : {ConversationMode::Change, ConversationMode::Connect}) {
        PrinterConversation conversation(backend, panel);
        conversation.start(mode, "Lab Printer");
        // Every tool is still offered: the refusal says where adding happens.
        const std::vector<std::string> offered = conversation.profile().tool_names;
        for (const std::string& tool : PrinterConversation::session_tools())
            CHECK(std::find(offered.begin(), offered.end(), tool) != offered.end());
        CHECK(preflight_refusal(conversation, "printer_identify", json{{"catalogIds", {"Prusa/Prusa MK3S"}}}) ==
              "not_in_this_conversation");
        CHECK(preflight_refusal(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}) == "not_in_this_conversation");
        CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", "Garage"}, {"nozzle", 0.6}}) == "other_printer");
        CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", "Workshop"}, {"nozzle", 0.6}}) ==
              "unknown_printer");
        CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}).empty());
    }
    CHECK(backend.added.empty());
    CHECK(backend.changed.empty());
}

TEST_CASE("an Add session changes only the printer it added, once it has added one", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}) == "no_printer");
    CHECK(preflight_refusal(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}).empty());
    ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}});
    // Correcting the nozzle it was added with is the same conversation's.
    CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", "Prusa MK3S"}, {"nozzle", 0.6}}).empty());
    CHECK(preflight_refusal(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}) == "other_printer");
}

// -- Connecting ---------------------------------------------------------------

namespace {

PrinterConnectionInfo bambu_on_network()
{
    PrinterConnectionInfo info;
    info.provider   = "bambu";
    info.state      = "not_configured";
    info.candidates = {{"01P00A3B", "Workshop", "192.168.1.30", true}, {"CLOUD01", "Office", "", false, true}};
    return info;
}

// Looked for and not found yet: the state right after a search starts.
PrinterConnectionInfo bambu_nothing_found()
{
    PrinterConnectionInfo info;
    info.provider = "bambu";
    info.state    = "not_configured";
    return info;
}

// A printer that announced itself on the network in cloud mode, as a P1S with
// LAN Only off did on 2026-10-31.
ConnectionCandidate p1s_in_cloud_mode() { return {"01P00C652900116", "P1s", "192.168.0.198", false}; }

// A Bambu Lab printer on a computer without Bambu's network plug-in.
PrinterConnectionInfo bambu_without_plugin()
{
    PrinterConnectionInfo info;
    info.provider             = "bambu";
    info.state                = "unavailable";
    info.message              = "Connecting a Bambu Lab printer needs Bambu's network plug-in.";
    info.needs_network_plugin = true;
    return info;
}

SavedPrinter bambu_lab_printer()
{
    SavedPrinter printer = lab_printer();
    printer.vendor_id    = "BBL";
    return printer;
}

std::vector<json> blocks_of(const PrinterConversation& conversation, const std::string& kind)
{
    std::vector<json> found;
    const json        state = conversation.state_json();
    for (const json& block : state.at("blocks"))
        if (block.value("kind", std::string()) == kind)
            found.push_back(block);
    return found;
}

} // namespace

// -- Bambu's network plug-in -------------------------------------------------

TEST_CASE("without the plug-in, checking the connection draws the app's notice once", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_without_plugin();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");
    CHECK(blocks_of(conversation, "plugin").empty());
    CHECK_FALSE(conversation.state_json().at("context").at("printer").contains("needsNetworkPlugin"));

    const json output = ran(conversation, "printer_connection_status", json::object(), "m-7");
    CHECK(output.at("state") == "unavailable");
    CHECK(output.at("message") == backend.connection_info.message);
    // Under the reply that checked, and once however often the model checks.
    ran(conversation, "printer_connection_status", json::object(), "m-8");
    const auto notices = blocks_of(conversation, "plugin");
    REQUIRE(notices.size() == 1);
    CHECK(notices.front().at("afterMessageId") == "m-7");
    // The model is told the fact too, for the page to write into its instructions.
    CHECK(conversation.state_json().at("context").at("printer").at("needsNetworkPlugin") == true);
}

TEST_CASE("Connect on a Bambu Lab printer without the plug-in opens on the notice", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_without_plugin();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    CHECK(conversation.state_json().at("context").at("printer").at("needsNetworkPlugin") == true);
    conversation.handle_page_message("printer_opening", json{{"text", "To connect Lab Printer, JusPrin first needs the plug-in."}});
    const auto notices = blocks_of(conversation, "plugin");
    REQUIRE(notices.size() == 1);
    CHECK(notices.front().at("afterMessageId") == "opening-1");
    CHECK(panel.turns == 0);

    // A print host needs no plug-in, and asks nothing of Bambu's.
    SavedPrinter host = lab_printer();
    host.name         = "Voron";
    host.vendor_id    = "Voron";
    backend.saved.push_back(host);
    conversation.start(ConversationMode::Connect, "Voron");
    CHECK(blocks_of(conversation, "plugin").empty());
}

TEST_CASE("printer_bambu_connect without the plug-in gives the plug-in as the reason", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_without_plugin();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    Agent::ToolActivity activity = call("printer_bambu_connect", json{{"deviceId", "0309DA123456789"}});
    const auto problem = conversation.preflight_tool(Agent::ToolHandler::PrinterBambuConnect, activity);
    REQUIRE(problem.has_value());
    CHECK(problem->code == "connection_unavailable");
    CHECK(problem->message == backend.connection_info.message);
}

TEST_CASE("Install on the notice runs the installer and tells the model when it worked", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_without_plugin();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");
    ran(conversation, "printer_connection_status", json::object());
    REQUIRE(backend.prepared.size() == 1);

    // Closed without installing: nothing changes and nothing is said.
    REQUIRE(conversation.handle_page_message("printer_action", json{{"action", "install_network_plugin"}}));
    CHECK(backend.plugin_installs == 1);
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);
    CHECK_FALSE(blocks_of(conversation, "plugin").front().value("installed", false));

    backend.install_succeeds = true;
    conversation.handle_page_message("printer_action", json{{"action", "install_network_plugin"}});
    CHECK(backend.plugin_installs == 2);
    CHECK(blocks_of(conversation, "plugin").front().at("installed") == true);
    CHECK_FALSE(conversation.state_json().at("context").at("printer").contains("needsNetworkPlugin"));
    REQUIRE(panel.notes.size() == 1);
    check_is_a_statement(panel.notes.front());
    CHECK_THAT(panel.notes.front(), ContainsSubstring("installed"));
    CHECK(panel.turns == 1);
    // Looking for the printer could not start before; the next check starts it.
    run(conversation, "printer_connection_status", json::object());
    CHECK(backend.prepared.size() == 2);
    // Nothing is left to install.
    conversation.handle_page_message("printer_action", json{{"action", "install_network_plugin"}});
    CHECK(backend.plugin_installs == 2);
}

TEST_CASE("printer_connection_status starts looking and lists only LAN-mode printers", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const json output = ran(conversation, "printer_connection_status", json::object());
    // Looking again does not start looking again.
    ran(conversation, "printer_connection_status", json::object());
    CHECK(backend.prepared == std::vector<std::string>{"Lab Printer"});
    CHECK(output.at("candidates") == json::array({json{{"deviceId", "01P00A3B"}, {"name", "Workshop"}, {"address", "192.168.1.30"}}}));
    // The printer on the account is reported apart from what can be connected.
    CHECK(output.at("seen") == json::array({json{{"name", "Office"}, {"address", ""}, {"reason", "account_only"}}}));
    // Before a printer is saved there is nothing to connect.
    conversation.start(ConversationMode::Add);
    CHECK(refused(conversation, "printer_connection_status", json::object()) == "no_printer");
}

TEST_CASE("the connection tools are checked before their cards, and titles it", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    auto preflight = [&](const json& arguments) {
        const bool          host     = arguments.contains("hostType");
        Agent::ToolActivity activity = call(host ? "printer_host_connect" : "printer_bambu_connect", arguments);
        return std::make_pair(conversation.preflight_tool(host ? Agent::ToolHandler::PrinterHostConnect :
                                                                 Agent::ToolHandler::PrinterBambuConnect,
                                                          activity),
                              activity);
    };
    // A printer reached through a Bambu account is still not offered, but it
    // is no longer hidden: the refusal says what the app knows about it, and
    // does not claim it is on the network, which nothing has shown.
    const auto account_only = preflight(json{{"deviceId", "CLOUD01"}}).first;
    CHECK(account_only->code == "unknown_device");
    CHECK_THAT(account_only->message, ContainsSubstring("Office"));
    CHECK_THAT(account_only->message, ContainsSubstring("Bambu account"));
    CHECK_THAT(account_only->message, ContainsSubstring("has not been seen on the network"));
    CHECK_THAT(account_only->message, !ContainsSubstring("LAN Only off"));
    const auto [problem, activity] = preflight(json{{"deviceId", "01P00A3B"}});
    CHECK_FALSE(problem.has_value());
    CHECK(activity.title == "Waiting for the access code for Workshop");
    CHECK(json::parse(activity.arguments_json).at("provider") == "bambu");
    CHECK(json::parse(activity.arguments_json).at("printerName") == "Lab Printer");
    // What the card names once it has started.
    CHECK(json::parse(activity.arguments_json).at("target") == "Workshop");
    // Named as the person sees it, it is the same printer; the card and the
    // connection use its id.
    const auto [by_name, named] = preflight(json{{"deviceId", "Workshop"}});
    CHECK_FALSE(by_name.has_value());
    CHECK(json::parse(named.arguments_json).at("deviceId") == "01P00A3B");
    CHECK_THAT(preflight(json{{"deviceId", "Nope"}}).first->message, ContainsSubstring("01P00A3B (Workshop)"));

    backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    CHECK(preflight(json{{"deviceId", "01P00A3B"}}).first->code == "address_needed");
    const auto [host_problem, host_card] = preflight(json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}});
    CHECK(host_card.title == "Checking 192.168.1.42");
    CHECK(json::parse(host_card.arguments_json).at("target") == "192.168.1.42");
    backend.connection_info = PrinterConnectionInfo{"unavailable", "unavailable", "Install the network plugin."};
    CHECK(preflight(json{{"deviceId", "01P00A3B"}}).first->message == "Install the network plugin.");
    // It connects the printer the conversation is about, and nothing before one is saved.
    conversation.start(ConversationMode::Add);
    CHECK(preflight(json{{"deviceId", "01P00A3B"}}).first->code == "no_printer");
}

namespace {

std::optional<Agent::ToolError> connect_refusal(PrinterConversation& conversation, const json& arguments)
{
    const bool          host     = arguments.contains("hostType");
    Agent::ToolActivity activity = call(host ? "printer_host_connect" : "printer_bambu_connect", arguments);
    return conversation.preflight_tool(host ? Agent::ToolHandler::PrinterHostConnect : Agent::ToolHandler::PrinterBambuConnect,
                                       activity);
}

} // namespace

TEST_CASE("printer_bambu_connect does not claim what the search has not looked at", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_nothing_found();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    // No search has run: the log of 2026-10-31 shows "No printer in LAN mode
    // is on the network now" hours before anything had looked.
    const auto before = connect_refusal(conversation, json{{"deviceId", "192.168.0.198"}});
    REQUIRE(before.has_value());
    CHECK(before->code == "unknown_device");
    CHECK_THAT(before->message, ContainsSubstring("No network search has run"));
    CHECK_THAT(before->message, ContainsSubstring("printer_connection_status"));
    CHECK_THAT(before->message, !ContainsSubstring("is on the network now"));
    CHECK(backend.prepared.empty());

    // Just started, with a status call listening beside it: nothing is known
    // yet, and a note will say what is heard.
    REQUIRE(run(conversation, "printer_connection_status", json::object()).pending);
    const auto young = connect_refusal(conversation, json{{"deviceId", "192.168.0.198"}});
    REQUIRE(young.has_value());
    CHECK_THAT(young->message, ContainsSubstring("has listened"));
    CHECK_THAT(young->message, ContainsSubstring("note"));
    CHECK_THAT(young->message, !ContainsSubstring("No network search has run"));
    CHECK_THAT(young->message, !ContainsSubstring("is on the network now"));

    // Listened long enough to say something, and still nothing.
    conversation.tick(std::chrono::steady_clock::now() + 31s);
    REQUIRE(panel.finished.size() == 1);
    const auto listened = connect_refusal(conversation, json{{"deviceId", "192.168.0.198"}});
    REQUIRE(listened.has_value());
    CHECK_THAT(listened->message, ContainsSubstring("has listened"));
    CHECK_THAT(listened->message, ContainsSubstring("no printer of this model"));
}

TEST_CASE("printer_connection_status reports a printer in cloud mode apart from what can be connected", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved                      = {bambu_lab_printer()};
    backend.connection_info            = bambu_on_network();
    backend.connection_info.candidates.push_back(p1s_in_cloud_mode());
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const json output = ran(conversation, "printer_connection_status", json::object());
    CHECK(output.at("candidates") == json::array({json{{"deviceId", "01P00A3B"}, {"name", "Workshop"}, {"address", "192.168.1.30"}}}));
    CHECK(output.at("seen") == json::array({json{{"name", "Office"}, {"address", ""}, {"reason", "account_only"}},
                                            json{{"name", "P1s"}, {"address", "192.168.0.198"}, {"reason", "cloud_mode"}}}));

    // The two tools say what the state means. What to do about it is the
    // connection skill's, so a chat that never tries to connect is not told.
    for (const char* tool : {"printer_connection_status", "printer_bambu_connect"}) {
        const std::string description = Agent::ToolRegistry::instance().find(tool)->description;
        INFO(tool);
        CHECK_THAT(description, ContainsSubstring("LAN Only is off"));
        CHECK_THAT(description, !ContainsSubstring("choice"));
    }

    // A print host is reached by its address: nothing is searched for or seen.
    backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    const json host = ran(conversation, "printer_connection_status", json::object());
    CHECK(host.at("seen") == json::array());
    CHECK(host.at("search").at("state") == "not_applicable");
}

// Every kind of printer the search can hear is told apart in what the model
// reads: offered, in cloud mode, known only from the account, another model,
// the same model linked to another saved printer, the same model while this
// printer is linked to a different device, and one bound elsewhere. Before
// 2026-11-01 the last four were dropped from the status, the refusal and the
// search's note alike, and the model read "found no printer of this model".
TEST_CASE("printer_connection_status reports every printer it heard and does not offer, with the reason", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_nothing_found();
    PrinterConversation conversation(backend, panel);

    struct Heard
    {
        const char*         reason;
        ConnectionCandidate candidate;
        const char*         in_words; // what the refusal and the note say about it
    };
    const std::vector<Heard> heard = {
        {"other_model", {"01P00P1P0001", "Garage", "192.168.1.31", true, false, "other_model", "P1P"}, "a P1P"},
        {"linked_elsewhere", {"01P00A3B0002", "Bench", "192.168.1.32", true, false, "linked_elsewhere", "Lab Printer 2"}, "\"Lab Printer 2\""},
        {"other_device", {"01P00A3B0003", "Loaner", "192.168.1.33", true, false, "other_device", "01P00A3B0009"}, "01P00A3B0009"},
        // occupied is asserted against the fake only: no fake or printer at hand
        // on 2026-11-01 reported a bind_state other than free.
        {"occupied", {"01P00A3B0004", "Shared", "192.168.1.34", true, false, "occupied", ""}, "bind state"},
        {"cloud_mode", p1s_in_cloud_mode(), "LAN Only off"},
        {"account_only", {"CLOUD01", "Office", "", false, true}, "Bambu account"},
    };

    std::set<std::string> seen_entries;
    std::set<std::string> refusals;
    std::set<std::string> notes;
    for (const Heard& one : heard) {
        DYNAMIC_SECTION("heard: " << one.reason)
        {
            conversation.start(ConversationMode::Connect, "Lab Printer");
            backend.connection_info.candidates = {one.candidate};
            // One only on the account was not heard: the call listens for it.
            json output;
            if (one.candidate.account_only) {
                REQUIRE(run(conversation, "printer_connection_status", json::object()).pending);
                conversation.tick(std::chrono::steady_clock::now() + 31s);
                REQUIRE(panel.finished.size() == 1);
                output = json::parse(panel.finished.front().second.result_json);
            } else
                output = ran(conversation, "printer_connection_status", json::object());
            CHECK(output.at("candidates") == json::array());
            REQUIRE(output.at("seen").size() == 1);
            const json entry = output.at("seen").front();
            CHECK(entry.at("name") == one.candidate.name);
            CHECK(entry.at("reason") == one.reason);
            if (!one.candidate.detail.empty())
                CHECK(entry.at("detail") == one.candidate.detail);

            const auto refusal = connect_refusal(conversation, json{{"deviceId", one.candidate.id}});
            REQUIRE(refusal.has_value());
            CHECK(refusal->code == "unknown_device");
            CHECK_THAT(refusal->message, ContainsSubstring(one.candidate.name));
            CHECK_THAT(refusal->message, ContainsSubstring(one.in_words));
            CHECK_THAT(refusal->message, !ContainsSubstring("found no printer of this model"));
        }
    }

    SECTION("the search's note names each of them, and the model can tell them apart")
    {
        for (const Heard& one : heard) {
            if (one.candidate.account_only)
                continue; // not on the network: no search can hear it
            conversation.start(ConversationMode::Connect, "Lab Printer");
            backend.connection_info.candidates.clear();
            panel.notes.clear();
            // Heard after the call stopped listening: a note says it.
            const auto start = std::chrono::steady_clock::now();
            REQUIRE(run(conversation, "printer_connection_status", json::object()).pending);
            conversation.tick(start + 31s);
            backend.connection_info.candidates = {one.candidate};
            conversation.tick(start + 40s);
            INFO(one.reason);
            REQUIRE(panel.notes.size() == 1);
            check_is_a_statement(panel.notes.front());
            CHECK_THAT(panel.notes.front(), ContainsSubstring(one.candidate.name));
            CHECK_THAT(panel.notes.front(), ContainsSubstring(one.in_words));
            CHECK_THAT(panel.notes.front(), !ContainsSubstring("no printer of this model"));
            notes.insert(panel.notes.front());
        }
        CHECK(notes.size() == heard.size() - 1);
    }

    SECTION("the reasons are the schema's, and the descriptions name them")
    {
        const auto& registry   = Agent::ToolRegistry::instance();
        const json  schema     = registry.find("printer_connection_status")->output_schema;
        const json  reasons    = schema.at("properties").at("seen").at("items").at("properties").at("reason").at("enum");
        for (const Heard& one : heard) {
            INFO(one.reason);
            CHECK(std::find(reasons.begin(), reasons.end(), json(one.reason)) != reasons.end());
            CHECK_THAT(registry.find("printer_connection_status")->description, ContainsSubstring(one.reason));
        }
    }
}

TEST_CASE("the connection tools name the printer in cloud mode it was asked for", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_nothing_found();
    backend.connection_info.candidates.push_back(p1s_in_cloud_mode());
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    // By id, by name, by address, and by the address a host connection takes.
    for (const json& arguments : {json{{"deviceId", "01P00C652900116"}}, json{{"deviceId", "P1s"}}, json{{"deviceId", "192.168.0.198"}},
                                  json{{"hostType", "moonraker"}, {"address", "192.168.0.198"}}}) {
        const auto problem = connect_refusal(conversation, arguments);
        INFO(arguments.dump());
        REQUIRE(problem.has_value());
        CHECK(problem->code == "unknown_device");
        CHECK_THAT(problem->message, ContainsSubstring("P1s at 192.168.0.198"));
        CHECK_THAT(problem->message, ContainsSubstring("seen on the network with LAN Only off"));
        CHECK_THAT(problem->message, ContainsSubstring("can prepare prints for it without a connection"));
    }

    // Asked for something else, the printer that was seen is still reported.
    const auto other = connect_refusal(conversation, json{{"deviceId", "Nope"}});
    REQUIRE(other.has_value());
    CHECK_THAT(other->message, ContainsSubstring("P1s at 192.168.0.198"));
    CHECK_THAT(other->message, ContainsSubstring("LAN Only off"));

    // A printer with no address known matches no empty argument: without this
    // an address call would find the printer on the account.
    backend.connection_info.candidates = {{"CLOUD01", "Office", "", false, true}};
    const auto by_address = connect_refusal(conversation, json{{"hostType", "moonraker"}, {"address", "192.168.0.198"}});
    REQUIRE(by_address.has_value());
    CHECK_THAT(by_address->message, ContainsSubstring("No network search has run"));
    CHECK_THAT(by_address->message, ContainsSubstring("Office"));
}

TEST_CASE("printer_connection_status listens during the call, and the app says what it hears after", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_nothing_found();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    // Nothing is watched before the search has started.
    const auto start = std::chrono::steady_clock::now();
    conversation.tick(start + 60s);
    CHECK(panel.notes.empty());

    const auto listening = [&] {
        Agent::ToolActivity activity = call("printer_connection_status", json::object());
        activity.action_id           = "t-status";
        const auto result = conversation.execute_tool(Agent::ToolHandler::PrinterConnectionStatus, activity);
        REQUIRE(result.handled);
        REQUIRE_FALSE(result.error.has_value());
        CHECK(result.pending);
    };
    const auto answer = [&] {
        REQUIRE(panel.finished.size() == 1);
        CHECK(panel.finished.front().first == "t-status");
        const auto& outcome = panel.finished.front().second;
        REQUIRE_FALSE(outcome.error.has_value());
        const json output = json::parse(outcome.result_json);
        CHECK(Agent::ToolRegistry::instance().validate_output(*Agent::ToolRegistry::instance().find("printer_connection_status"),
                                                              output));
        return output;
    };

    SECTION("a printer heard while the call listens ends it, with no note")
    {
        listening();
        conversation.tick(start + 5s);
        CHECK(panel.finished.empty());
        backend.connection_info.candidates.push_back(p1s_in_cloud_mode());
        conversation.tick(start + 8s);
        const json output = answer();
        CHECK(output.at("seen").at(0).at("name") == "P1s");
        CHECK(output.at("search").at("state") == "listening");
        CHECK_FALSE(output.at("search").contains("message"));
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
        // Said once.
        conversation.tick(start + 60s);
        CHECK(panel.notes.empty());
        CHECK(ran(conversation, "printer_connection_status", json::object()).at("search").at("state") == "listening");
        // Looking again does not start looking again.
        CHECK(backend.prepared.size() == 1);
    }
    SECTION("30 seconds with nothing ends the call saying the search goes on, and a printer heard later is a note")
    {
        listening();
        conversation.tick(start + 29s);
        CHECK(panel.finished.empty());
        CHECK(backend.port_checks == 0);
        conversation.tick(start + 31s);
        const json output = answer();
        CHECK(output.at("candidates") == json::array());
        CHECK(output.at("search").at("state") == "listening");
        CHECK_THAT(output.at("search").at("message").get<std::string>(), ContainsSubstring("Nothing heard in"));
        CHECK_THAT(output.at("search").at("message").get<std::string>(), ContainsSubstring("still listening"));
        CHECK_THAT(output.dump(), !ContainsSubstring("not found"));
        CHECK(backend.port_checks == 1);
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
        conversation.tick(start + 40s);
        CHECK(panel.notes.empty());

        // On 2026-10-31 the P1S first answered 38 seconds in.
        backend.connection_info.candidates = {{"01P00A3B", "Workshop", "192.168.1.30", true}};
        conversation.tick(start + 46s);
        REQUIRE(panel.notes.size() == 1);
        check_is_a_statement(panel.notes.front());
        CHECK_THAT(panel.notes.front(), ContainsSubstring("Workshop at 192.168.1.30 in LAN mode"));
        CHECK_THAT(panel.notes.front(), !ContainsSubstring("can be connected"));
        CHECK(panel.turns == 1);
        conversation.tick(start + 60s);
        CHECK(panel.notes.size() == 1);
    }
    SECTION("a second call after nothing was heard listens again")
    {
        listening();
        conversation.tick(start + 31s);
        answer();
        panel.finished.clear();
        listening();
        backend.connection_info.candidates.push_back(p1s_in_cloud_mode());
        conversation.tick(start + 33s);
        CHECK(answer().at("seen").at(0).at("name") == "P1s");
        CHECK(panel.notes.empty());
    }
    SECTION("what the first read already listed is returned at once and not said again")
    {
        backend.connection_info.candidates.push_back(p1s_in_cloud_mode());
        const json first = ran(conversation, "printer_connection_status", json::object());
        CHECK(first.at("search").at("state") == "listening");
        conversation.tick(start + 30s);
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
    }
    SECTION("a printer known only from the account is not a printer heard on the network")
    {
        backend.connection_info.candidates = {{"CLOUD01", "Office", "", false, true}};
        listening();
        conversation.tick(start + 5s);
        CHECK(panel.finished.empty());
        conversation.tick(start + 31s);
        CHECK(answer().at("seen").at(0).at("reason") == "account_only");
        CHECK(panel.notes.empty());
    }
    SECTION("a printer reached through the person's account is no search's business")
    {
        backend.connection_info.state      = "verified";
        backend.connection_info.candidates = {{"CLOUD01", "Office", "", false, true}};
        ran(conversation, "printer_connection_status", json::object());
        conversation.tick(start + 31s);
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
    }
    SECTION("a printer removed while the call listens ends it with what the app now reads")
    {
        listening();
        backend.connection_info = PrinterConnectionInfo{"unavailable", "unavailable", "This printer was renamed or removed."};
        conversation.tick(start + 16s);
        CHECK(answer().at("state") == "unavailable");
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
    }
    SECTION("a new session drops the call and the search it did not start")
    {
        listening();
        conversation.start(ConversationMode::Connect, "Lab Printer");
        conversation.tick(start + 31s);
        CHECK(panel.finished.empty());
        CHECK(panel.notes.empty());
        CHECK(connect_refusal(conversation, json{{"deviceId", "x"}})->message.find("No network search has run") != std::string::npos);
    }
}

TEST_CASE("the search says when another app holds the port printers announce on", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_nothing_found();
    backend.port            = DiscoveryPort::Taken;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const auto start  = std::chrono::steady_clock::now();
    const auto status = [&] { return ran(conversation, "printer_connection_status", json::object()); };
    REQUIRE(run(conversation, "printer_connection_status", json::object()).pending);
    // Checking binds the port for a moment, which could cost discovery its own
    // bind: not while it has only just started.
    conversation.tick(start + 10s);
    CHECK(backend.port_checks == 0);

    // The call ends with the port, not with a note.
    conversation.tick(start + 31s);
    REQUIRE(panel.finished.size() == 1);
    const json output = json::parse(panel.finished.front().second.result_json);
    CHECK(output.at("search").at("state") == "port_taken");
    CHECK_FALSE(output.at("search").contains("message"));
    CHECK(panel.notes.empty());
    CHECK(status().at("search").at("state") == "port_taken");
    const auto refusal = connect_refusal(conversation, json{{"deviceId", "192.168.0.198"}});
    REQUIRE(refusal.has_value());
    CHECK_THAT(refusal->message, ContainsSubstring("another app"));
    CHECK_THAT(refusal->message, !ContainsSubstring("found no printer"));

    // Said once, however long it stays that way.
    conversation.tick(start + 40s);
    CHECK(panel.notes.empty());

    SECTION("when the other app lets go, the app says only a restart brings the search back")
    {
        // Nothing this app can call takes the port up again, so "listening"
        // would be false from here on.
        backend.port = DiscoveryPort::Free;
        conversation.tick(start + 46s);
        REQUIRE(panel.notes.size() == 1);
        check_is_a_statement(panel.notes.back());
        CHECK_THAT(panel.notes.back(), ContainsSubstring("not listening"));
        CHECK_THAT(panel.notes.back(), ContainsSubstring("restarted"));
        CHECK_THAT(panel.notes.back(), !ContainsSubstring("found no printer"));
        CHECK(panel.turns == 1);
        CHECK(status().at("search").at("state") == "not_listening");
        const auto after = connect_refusal(conversation, json{{"deviceId", "192.168.0.198"}});
        REQUIRE(after.has_value());
        CHECK_THAT(after->message, ContainsSubstring("not listening"));

        conversation.tick(start + 52s);
        CHECK(panel.notes.size() == 1);

        // It opened again: that is news too.
        backend.port = DiscoveryPort::Taken;
        conversation.tick(start + 60s);
        REQUIRE(panel.notes.size() == 2);
        check_is_a_statement(panel.notes.back());
        CHECK_THAT(panel.notes.back(), ContainsSubstring("another app"));
        CHECK_THAT(panel.notes.back(), ContainsSubstring("2021"));
        CHECK(status().at("search").at("state") == "port_taken");
    }
    SECTION("a printer that was already known ends it like any other")
    {
        backend.connection_info.candidates.push_back(p1s_in_cloud_mode());
        conversation.tick(start + 46s);
        REQUIRE(panel.notes.size() == 1);
        CHECK_THAT(panel.notes.back(), ContainsSubstring("P1s at 192.168.0.198"));
    }
}

TEST_CASE("a port nobody holds is reported as a search nobody is running", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_nothing_found();
    backend.port            = DiscoveryPort::Free;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");
    REQUIRE(run(conversation, "printer_connection_status", json::object()).pending);

    const auto start = std::chrono::steady_clock::now();
    conversation.tick(start + 31s);
    REQUIRE(panel.finished.size() == 1);
    CHECK(json::parse(panel.finished.front().second.result_json).at("search").at("state") == "not_listening");
    CHECK(panel.notes.empty());
    CHECK(ran(conversation, "printer_connection_status", json::object()).at("search").at("state") == "not_listening");
    conversation.tick(start + 45s);
    CHECK(panel.notes.empty());
}

TEST_CASE("without the plug-in no search has started, and none is reported", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_lab_printer()};
    backend.connection_info = bambu_without_plugin();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const auto start = std::chrono::steady_clock::now();
    CHECK(ran(conversation, "printer_connection_status", json::object()).at("search") == json{{"state", "not_started"}});
    conversation.tick(start + 30s);
    CHECK(panel.notes.empty());

    // Installed: the next check starts the search, and only then is it watched.
    backend.install_succeeds = true;
    conversation.handle_page_message("printer_action", json{{"action", "install_network_plugin"}});
    REQUIRE(panel.notes.size() == 1);
    conversation.tick(start + 60s);
    CHECK(panel.notes.size() == 1);
    CHECK(run(conversation, "printer_connection_status", json::object()).pending);
    CHECK(backend.prepared.size() == 2);
}

TEST_CASE("printer_host_connect takes an address and printer_bambu_connect a found printer, never part of one",
          "[printer-conversation]")
{
    const auto&                  registry = Agent::ToolRegistry::instance();
    const Agent::ToolDefinition* host     = registry.find("printer_host_connect");
    const Agent::ToolDefinition* code     = registry.find("printer_bambu_connect");
    REQUIRE(host != nullptr);
    REQUIRE(code != nullptr);
    const auto checked = [&](const Agent::ToolDefinition* definition, const json& arguments) {
        return registry.validate_call(*definition, arguments.dump());
    };

    CHECK(checked(code, json{{"deviceId", "01P00A3B"}}).valid());
    CHECK(checked(host, json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}}).valid());
    // What the live model sent after "go ahead", before anyone gave an
    // address: refused here, so no card is drawn and the model is told to ask.
    const auto without_address = checked(host, json{{"hostType", "moonraker"}});
    REQUIRE_FALSE(without_address.valid());
    CHECK_THAT(without_address.error->message, ContainsSubstring("ask the person"));
    CHECK_FALSE(checked(host, json{{"address", ""}}).valid());
    CHECK_FALSE(checked(host, json{{"hostType", "moonraker"}, {"address", ""}}).valid());
    CHECK_FALSE(checked(host, json{{"address", "192.168.1.42"}}).valid());
    CHECK_FALSE(checked(host, json::object()).valid());
    CHECK_FALSE(checked(host, json{{"deviceId", "01P00A3B"}}).valid());
    CHECK_FALSE(checked(code, json::object()).valid());
    CHECK_FALSE(checked(code, json{{"deviceId", ""}}).valid());
    CHECK_FALSE(checked(code, json{{"deviceId", "01P00A3B"}, {"hostType", "moonraker"}, {"address", "192.168.1.42"}}).valid());
}

// Runs printer_bambu_connect, then submits the local credential form.
Agent::ToolExecutionCoordinator::ExtensionResult tap_connect(PrinterConversation& conversation, RecordingPanel&,
                                                              const json& arguments, const std::string& action,
                                                              const std::string& credential = {})
{
    Agent::ToolActivity activity = call("printer_bambu_connect", arguments);
    activity.action_id           = action;
    const auto result = conversation.execute_tool(Agent::ToolHandler::PrinterBambuConnect, activity);
    conversation.handle_page_message("printer_action",
                                     json{{"action", "connect"}, {"actionId", action}, {"credential", credential}});
    return result;
}

const json kHostArguments{{"printerName", "Lab Printer"}, {"hostType", "moonraker"}, {"address", "192.168.1.42"},
                          {"provider", "host"},           {"target", "192.168.1.42"}};
const json kBambuArguments{{"printerName", "Lab Printer"}, {"deviceId", "01P00A3B"}, {"provider", "bambu"}, {"target", "Workshop"}};

// Runs printer_host_connect as the coordinator would, after its preflight.
Agent::ToolExecutionCoordinator::ExtensionResult host_connect(PrinterConversation& conversation, const std::string& action,
                                                               const json& arguments = kHostArguments)
{
    Agent::ToolActivity activity = call("printer_host_connect", arguments);
    activity.action_id           = action;
    return conversation.execute_tool(Agent::ToolHandler::PrinterHostConnect, activity);
}

// What the model reads from a call the panel ended.
json finished_output(const RecordingPanel& panel, const std::string& action)
{
    REQUIRE_FALSE(panel.finished.empty());
    CHECK(panel.finished.back().first == action);
    const auto& outcome = panel.finished.back().second;
    INFO((outcome.error ? outcome.error->message : std::string()));
    REQUIRE_FALSE(outcome.error.has_value());
    const json output = json::parse(outcome.result_json);
    CHECK(Agent::ToolRegistry::instance().validate_output(*Agent::ToolRegistry::instance().find("printer_host_connect"), output));
    return output;
}

// The person stops a waiting call, as the coordinator reports it.
void stopped(PrinterConversation& conversation, const char* tool, const std::string& action)
{
    Agent::ToolActivity activity = call(tool, json::object());
    activity.action_id           = action;
    activity.state               = Agent::ToolState::Cancelled;
    conversation.tool_settled(activity);
}

void form_answer(PrinterConversation& conversation, const std::string& action, const std::string& answer)
{
    conversation.handle_page_message("printer_action", json{{"action", answer}, {"actionId", action}});
}

TEST_CASE("printer_host_connect tests during the call and ends it with what the test saw", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const auto result = host_connect(conversation, "a-1");
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.pending);
    // No key: the backend sends the one it holds for that address, if any.
    CHECK(backend.connects.back() == std::vector<std::string>{"Lab Printer", "moonraker", "192.168.1.42", ""});
    CHECK(panel.finished.empty());
    REQUIRE(backend.tested);
    const std::string refused = "Moonraker at 192.168.1.42 refused the test: HTTP 401: Unauthorized. The test sent no API key.";

    SECTION("verified: the call ends with the test's words, and nothing waits")
    {
        const std::string said = "Moonraker at 192.168.1.42 accepted the test, and this printer now connects there. The test sent no "
                                 "API key. Moonraker reports Klipper's state as shutdown.";
        backend.tested(HostTestOutcome{"verified", said, "none"});
        CHECK(finished_output(panel, "a-1") == json{{"state", "verified"}, {"message", said}});
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
        CHECK(panel.refreshes == 1);
        CHECK(panel.updates.empty());
        CHECK_FALSE(conversation.state_json().contains("credentialRequest"));
    }
    SECTION("needs_credential: the call waits for the form, and ends with the test the key ran")
    {
        backend.tested(HostTestOutcome{"needs_credential", refused, "none"});
        CHECK(panel.finished.empty());
        CHECK(panel.updates == std::vector<std::tuple<std::string, std::string, bool>>{
                                   {"a-1", "Waiting for an API key for 192.168.1.42", false}});
        CHECK(conversation.state_json().at("credentialRequest").at("actionId") == "a-1");

        backend.tested = nullptr;
        conversation.handle_page_message("printer_action", json{{"action", "connect"}, {"actionId", "a-1"}, {"credential", "key123"}});
        CHECK(backend.connects.back() == std::vector<std::string>{"Lab Printer", "moonraker", "192.168.1.42", "key123"});
        CHECK(std::get<1>(panel.updates.back()) == "Checking 192.168.1.42");
        // Its own work runs now, and the app's dialogs are part of it.
        CHECK(std::get<2>(panel.updates.back()));
        CHECK_FALSE(conversation.state_json().contains("credentialRequest"));
        CHECK(panel.finished.empty());
        REQUIRE(backend.tested);
        backend.tested(HostTestOutcome{"verified", "Moonraker at 192.168.1.42 accepted the test. The test sent the API key typed in the form.",
                                       "typed"});
        CHECK(finished_output(panel, "a-1") ==
              json{{"state", "verified"},
                   {"message", refused + " The person typed a key in the form, and the app tested again: Moonraker at 192.168.1.42 "
                                         "accepted the test. The test sent the API key typed in the form."}});
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
    }
    SECTION("a typed key the host refuses ends the call failed, with both answers")
    {
        backend.tested(HostTestOutcome{"needs_credential", refused, "none"});
        conversation.handle_page_message("printer_action", json{{"action", "connect"}, {"actionId", "a-1"}, {"credential", "wrong"}});
        backend.tested(HostTestOutcome{"needs_credential", "Moonraker refused the test. The test sent the API key typed in the form.", "typed"});
        const json output = finished_output(panel, "a-1");
        CHECK(output.at("state") == "failed");
        CHECK_THAT(output.at("message").get<std::string>(), StartsWith(refused));
        CHECK_THAT(output.at("message").get<std::string>(), EndsWith("The test sent the API key typed in the form."));
    }
    SECTION("a saved key that was refused asks for a different one")
    {
        backend.tested(HostTestOutcome{"needs_credential", "Moonraker refused the test. The test sent the API key saved for this printer.",
                                       "saved"});
        CHECK(std::get<1>(panel.updates.back()) == "Waiting for a different API key for 192.168.1.42");
    }
    SECTION("the person has no key, or closes the form: the call says so, and nothing is sent")
    {
        backend.tested(HostTestOutcome{"needs_credential", refused, "none"});
        const bool missing = GENERATE(true, false);
        form_answer(conversation, "a-1", missing ? "credential_help" : "credential_dismissed");
        const json output = finished_output(panel, "a-1");
        CHECK(output.at("state") == (missing ? "help_requested" : "dismissed"));
        CHECK(output.at("message") ==
              refused + (missing ? " The person tapped \"Help me find the key\" in the form: they need help finding the API key for 192.168.1.42. Help them find it." :
                                   " The person closed the form for the API key for 192.168.1.42 without connecting.") +
                  " Nothing was sent.");
        CHECK(backend.connects.size() == 1);
        CHECK_FALSE(conversation.state_json().contains("credentialRequest"));
        CHECK(panel.turns == 0);
    }
    SECTION("failed: the host's words, and at the chat's second failure the report card")
    {
        backend.tested(HostTestOutcome{"failed", "The print host test failed: curl:Couldn't connect to server.", "none"});
        CHECK(finished_output(panel, "a-1") ==
              json{{"state", "failed"}, {"message", "The print host test failed: curl:Couldn't connect to server."}});
        CHECK_FALSE(conversation.state_json().contains("credentialRequest"));
        host_connect(conversation, "a-2");
        backend.tested(HostTestOutcome{"failed", "The print host test failed: HTTP 404: Not Found", "none"});
        const std::string second = finished_output(panel, "a-2").at("message");
        CHECK_THAT(second, StartsWith("The print host test failed: HTTP 404: Not Found "));
        CHECK_THAT(second, ContainsSubstring("send a report of this chat"));
        CHECK(panel.notes.empty());
    }
    SECTION("a call the person stops while the form waits drops the form")
    {
        backend.tested(HostTestOutcome{"needs_credential", refused, "none"});
        stopped(conversation, "printer_host_connect", "a-1");
        CHECK_FALSE(conversation.state_json().contains("credentialRequest"));
        CHECK(panel.finished.empty());
        // A Connect the page sent before it redrew starts nothing.
        conversation.handle_page_message("printer_action", json{{"action", "connect"}, {"actionId", "a-1"}, {"credential", "key"}});
        CHECK(backend.connects.size() == 1);
    }
    SECTION("a second call while one runs replaces it, and ends the first with why")
    {
        host_connect(conversation, "a-2", [] {
            json second       = kHostArguments;
            second["address"] = second["target"] = "192.168.1.43";
            return second;
        }());
        CHECK(backend.cancelled == std::vector<std::string>{"Lab Printer"});
        REQUIRE(panel.finished.size() == 1);
        CHECK(panel.finished.front().first == "a-1");
        REQUIRE(panel.finished.front().second.error.has_value());
        CHECK(panel.finished.front().second.error->code == "cancelled");
        backend.tested(HostTestOutcome{"verified", "Moonraker at 192.168.1.43 accepted the test.", "none"});
        CHECK(finished_output(panel, "a-2").at("state") == "verified");
    }
    SECTION("a panel that closes mid-test drops it and ends the call, and the next session ends nothing")
    {
        conversation.start(ConversationMode::Change, "Lab Printer");
        CHECK(backend.cancelled == std::vector<std::string>{"Lab Printer"});
        REQUIRE(panel.finished.size() == 1);
        REQUIRE(panel.finished.back().second.error.has_value());
        CHECK(panel.finished.back().second.error->code == "cancelled");
        conversation.start(ConversationMode::Connect, "Lab Printer");
        CHECK(backend.cancelled.size() == 1);
        CHECK(panel.finished.size() == 1);
    }
}

TEST_CASE("a connection call still waiting when the chat closes ends saying the person left", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    {
        PrinterConversation conversation(backend, panel);
        conversation.start(ConversationMode::Connect, "Lab Printer");
        Agent::ToolActivity activity = call("printer_bambu_connect", kBambuArguments);
        activity.action_id           = "a-1";
        CHECK(conversation.execute_tool(Agent::ToolHandler::PrinterBambuConnect, activity).pending);
    }
    REQUIRE(panel.finished.size() == 1);
    REQUIRE(panel.finished.back().second.error.has_value());
    CHECK(panel.finished.back().second.error->code == "cancelled");
    CHECK(panel.finished.back().second.error->message == "The person closed the printer chat before connecting was finished.");
}

TEST_CASE("a host asking for a key is no failed attempt: the report card comes at the person's second failed form",
          "[printer-conversation][connection-report]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");
    const std::string refused = "Moonraker at 192.168.1.42 refused the test: HTTP 401: Unauthorized";
    const auto ask_then_submit = [&](const std::string& action) {
        host_connect(conversation, action);
        backend.tested(HostTestOutcome{"needs_credential", refused + " The test sent no API key.", "none"});
        conversation.handle_page_message("printer_action", json{{"action", "connect"}, {"actionId", action}, {"credential", "wrong"}});
        backend.tested(HostTestOutcome{"needs_credential", refused + " The test sent the API key typed in the form.", "typed"});
        return finished_output(panel, action).at("message").get<std::string>();
    };

    CHECK_THAT(ask_then_submit("a-1"), !ContainsSubstring("send a report"));
    CHECK_THAT(ask_then_submit("a-2"), ContainsSubstring("send a report of this chat"));
    CHECK(panel.notes.empty());
}

TEST_CASE("printer_host_connect refused before its test starts says so in the call", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved            = {lab_printer()};
    backend.connection_error = "Use an HTTP or HTTPS address.";
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const auto result = host_connect(conversation, "a-1");
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "invalid_arguments");
    CHECK(result.error->message == "Use an HTTP or HTTPS address.");
    CHECK_FALSE(result.pending);
    CHECK(panel.notes.empty());
}

TEST_CASE("printer_host_connect and printer_bambu_connect each refuse the other kind of printer", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const auto bambu = connect_refusal(conversation, json{{"hostType", "moonraker"}, {"address", "192.168.1.30"}});
    REQUIRE(bambu.has_value());
    CHECK(bambu->code == "unknown_device");
    CHECK_THAT(bambu->message, StartsWith("This is a Bambu Lab printer, connected with printer_bambu_connect."));

    backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    const auto host = connect_refusal(conversation, json{{"deviceId", "01P00A3B"}});
    REQUIRE(host.has_value());
    CHECK(host->code == "address_needed");
    CHECK_THAT(host->message, ContainsSubstring("printer_host_connect"));
}

TEST_CASE("a Bambu Lab connection waits for the form and the printer, then ends with what code it sent", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const auto result = tap_connect(conversation, panel, kBambuArguments, "a-1", "12345678");
    CHECK(result.pending);
    CHECK(panel.updates == std::vector<std::tuple<std::string, std::string, bool>>{{"a-1", "Checking Workshop", true}});
    CHECK(backend.connects.back() == std::vector<std::string>{"Lab Printer", "01P00A3B", "12345678"});

    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::now();
    backend.connection_info.state = "connecting";
    conversation.tick(start);
    CHECK(panel.finished.empty());

    backend.connection_info.state   = "failed";
    backend.connection_info.message = "The printer did not respond.";
    conversation.tick(start + 500ms); // not yet a second since it last looked
    CHECK(panel.finished.empty());
    conversation.tick(start + 1100ms);
    CHECK(finished_output(panel, "a-1") ==
          json{{"state", "failed"}, {"message", "The printer did not respond. The app sent the access code typed in the form."}});
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);
    conversation.tick(start + 5s);
    CHECK(panel.finished.size() == 1);

    // An empty form reconnects with the code the app already holds.
    tap_connect(conversation, panel, kBambuArguments, "a-2");
    backend.connection_info.state = "verified";
    conversation.tick(start + 7s);
    CHECK(finished_output(panel, "a-2") ==
          json{{"state", "verified"},
               {"message", "The app connected to Lab Printer. The form was left empty, so the app used the access code it already "
                           "holds for this printer."}});

    // The person has no code: nothing is sent to the printer.
    Agent::ToolActivity third = call("printer_bambu_connect", kBambuArguments);
    third.action_id           = "a-3";
    conversation.execute_tool(Agent::ToolHandler::PrinterBambuConnect, third);
    form_answer(conversation, "a-3", "credential_help");
    CHECK(finished_output(panel, "a-3") ==
          json{{"state", "help_requested"},
               {"message", "The person tapped \"Help me find the code\" in the form: they need help finding the printer's access code. Help them find it. Nothing was sent."}});
    CHECK(backend.connects.size() == 2);
}

TEST_CASE("an attempt the last session left waiting is dropped when the next one starts", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");
    tap_connect(conversation, panel, kBambuArguments, "a-1");

    // The panel closed mid-attempt, and opens again.
    conversation.start(ConversationMode::Change, "Lab Printer");
    CHECK(backend.cancelled == std::vector<std::string>{"Lab Printer"});
    CHECK_FALSE(conversation.state_json().contains("credentialRequest"));
    REQUIRE(panel.finished.size() == 1);
    CHECK(panel.finished.back().second.error->code == "cancelled");
    backend.connection_info.state = "verified";
    conversation.tick(std::chrono::steady_clock::now());
    CHECK(panel.finished.size() == 1);
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);
    // Nothing was waiting the second time.
    conversation.start(ConversationMode::Connect, "Lab Printer");
    CHECK(backend.cancelled.size() == 1);
}

// -- OrcaSlicer's own screens, and closing -------------------------------------

// -- Undo on an added printer's receipt ----------------------------------------

void undo_add(PrinterConversation& conversation, const std::string& block_id)
{
    conversation.handle_page_message("printer_action", json{{"action", "undo_add"}, {"blockId", block_id}});
}

TEST_CASE("Undo on the receipt removes the printer, tells the model, and the right one can be added", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}, "m-4");
    const std::string receipt = conversation.state_json().at("blocks").back().at("id");

    // A tap naming no receipt removes nothing.
    undo_add(conversation, "b99");
    CHECK(backend.removed.empty());

    undo_add(conversation, receipt);
    CHECK(backend.removed == std::vector<std::string>{"Prusa MK3S"});
    CHECK(backend.saved.empty());
    // The receipt stays where it was, now saying so.
    const json block = conversation.state_json().at("blocks").back();
    CHECK(block.at("id") == receipt);
    CHECK(block.at("afterMessageId") == "m-4");
    CHECK(block.at("removed") == true);
    // Nothing is about that printer any more; the printer list is back.
    CHECK(conversation.printer_name().empty());
    CHECK_FALSE(conversation.state_json().at("context").contains("printer"));
    CHECK(conversation.state_json().at("context").contains("printers"));
    // Home forgets it, and the model hears it from the app and answers.
    CHECK(panel.added_printers.back().empty());
    REQUIRE(panel.notes == std::vector<std::string>{
                               "The person tapped Undo on the receipt: Prusa MK3S is removed and is no longer one of their printers."});
    check_is_a_statement(panel.notes.front());
    CHECK(panel.turns == 1);

    // A second tap that crossed the redraw is the same tap.
    undo_add(conversation, receipt);
    CHECK(backend.removed.size() == 1);
    CHECK(panel.notes.size() == 1);
    CHECK(panel.turns == 1);

    // "Already added" no longer holds for it.
    ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}});
    CHECK(backend.added.size() == 2);
    CHECK(conversation.printer_name() == "Prusa MK3S");
}

TEST_CASE("an Undo Orca refuses leaves the printer and its receipt as they were", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}});
    const std::string receipt = conversation.state_json().at("blocks").back().at("id");
    const int         refreshes = panel.refreshes;

    backend.refusal = "Other printer profiles are based on \"Prusa MK3S\".";
    undo_add(conversation, receipt);
    CHECK(backend.saved.size() == 1);
    CHECK_FALSE(conversation.state_json().at("blocks").back().contains("removed"));
    CHECK(conversation.printer_name() == "Prusa MK3S");
    CHECK(panel.refreshes == refreshes);
    CHECK(panel.notes == std::vector<std::string>{"Undo did not remove Prusa MK3S: Other printer profiles are based on \"Prusa MK3S\"."});
    CHECK(panel.turns == 0);
}

TEST_CASE("Undo while the added printer is connecting drops the attempt", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.connection_info = PrinterConnectionInfo{"host", "connecting"};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}});
    const std::string receipt = conversation.state_json().at("blocks").back().at("id");
    REQUIRE(host_connect(conversation, "a-1",
                         json{{"printerName", "Prusa MK3S"}, {"hostType", "moonraker"}, {"address", "192.168.1.42"},
                              {"provider", "host"}, {"target", "192.168.1.42"}})
                .pending);
    undo_add(conversation, receipt);
    CHECK(backend.cancelled == std::vector<std::string>{"Prusa MK3S"});
    // The call ends saying why; the test's own end is never read.
    REQUIRE(panel.finished.size() == 1);
    REQUIRE(panel.finished.front().second.error.has_value());
    CHECK(panel.finished.front().second.error->code == "cancelled");
    CHECK_THAT(panel.finished.front().second.error->message, ContainsSubstring("Undo"));
    CHECK(panel.notes.size() == 1);
    CHECK(panel.turns == 1);
}

TEST_CASE("Undo drops the search for the added printer", "[printer-conversation]")
{
    using namespace std::chrono_literals;
    FakeBackend    backend;
    RecordingPanel panel;
    backend.connection_info = bambu_nothing_found();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    ran(conversation, "printer_add", json{{"catalogId", "BBL/Bambu Lab A1 mini"}});
    const std::string receipt = conversation.state_json().at("blocks").back().at("id");
    REQUIRE(run(conversation, "printer_connection_status", json::object()).pending);
    undo_add(conversation, receipt);
    REQUIRE(panel.notes.size() == 1);
    REQUIRE(panel.finished.size() == 1);
    REQUIRE(panel.finished.front().second.error.has_value());
    CHECK(panel.finished.front().second.error->code == "cancelled");
    conversation.tick(std::chrono::steady_clock::now() + 31s);
    CHECK(panel.notes.size() == 1);
    CHECK(panel.finished.size() == 1);
}

TEST_CASE("OrcaSlicer's printer list, where the page could not load, reports what the person added", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    // Closed without adding anything.
    conversation.handle_page_message("printer_action", json{{"action", "orca_printer_list"}});
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);

    backend.manual_result = ManualPrinterResult{true, {lab_printer()}};
    conversation.handle_page_message("printer_action", json{{"action", "orca_printer_list"}});
    CHECK(conversation.printer_name() == "Lab Printer");
    REQUIRE(panel.notes == std::vector<std::string>{"The person added Lab Printer from the printer list."});
    check_is_a_statement(panel.notes.front());
    CHECK(panel.turns == 1);
    CHECK(backend.manual_setups == 2);
}

// -- The printer form ---------------------------------------------------------

namespace {

SetupOptions mini_options()
{
    SetupOptions options;
    options.plates           = {PlateOption{3, "Smooth High Temp Plate"}, PlateOption{4, "Textured PEI Plate"}};
    options.default_plate    = 4;
    options.filaments        = {FilamentOption{"Bambu PLA Basic @BBL A1M", "Bambu PLA Basic", "Bambu Lab", "PLA"},
                                FilamentOption{"Generic PETG", "Generic PETG", "Generic", "PETG"}};
    options.suggested        = {"Bambu PLA Basic @BBL A1M"};
    options.default_filament = "Bambu PLA Basic @BBL A1M";
    options.others           = {FilamentOption{"Bambu PLA Basic @BBL H2D", "Bambu PLA Silk", "Bambu Lab", "PLA", "the Bambu Lab H2D"}};
    options.own              = {FilamentOption{"PETG fast walls", "PETG fast walls", "Generic", "PETG", "", "Generic PETG"},
                                FilamentOption{"Matte PLA tuned", "Matte PLA tuned", "Bambu Lab", "PLA", "the Bambu Lab X1 Carbon",
                                               "Bambu PLA Matte"}};
    return options;
}

// No plate choice, as for most printers that are not Bambu Lab's.
SetupOptions mk3s_options()
{
    SetupOptions options;
    options.filaments        = {FilamentOption{"Prusa Generic PLA", "Prusa Generic PLA", "Generic", "PLA"}};
    options.default_filament = "Prusa Generic PLA";
    return options;
}

json form_of(const PrinterConversation& conversation) { return conversation.state_json().at("form"); }

void action(PrinterConversation& conversation, json payload) { conversation.handle_page_message("printer_action", payload); }

// The options are read on a thread of their own; the pump picks them up.
void settle(PrinterConversation& conversation)
{
    for (int i = 0; i < 500 && form_of(conversation).at("loading").get<bool>(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        conversation.tick(std::chrono::steady_clock::now());
    }
    REQUIRE_FALSE(form_of(conversation).at("loading").get<bool>());
}

// A form open on the A1 mini with a 0.4 mm nozzle, its options read.
void open_on_mini(FakeBackend& backend, PrinterConversation& conversation)
{
    backend.options[{"BBL/Bambu Lab A1 mini", 0.4}] = mini_options();
    conversation.start(ConversationMode::Add);
    action(conversation, json{{"action", "manual_setup"}});
    action(conversation, json{{"action", "form_options"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}});
    settle(conversation);
}

} // namespace

TEST_CASE("the printer form says what reading a printer's options failed with", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    backend.options[{"BBL/Bambu Lab A1 mini", 0.4}] = mini_options();
    backend.options[{"BBL/Bambu Lab A1 mini", 0.4}].error = "The printer's profile could not be read.";
    conversation.start(ConversationMode::Add);
    action(conversation, json{{"action", "manual_setup"}});
    action(conversation, json{{"action", "form_options"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}});
    settle(conversation);
    action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4}});
    CHECK(form_of(conversation).at("error") == "The printer's profile could not be read.");
    CHECK(backend.added.empty());
}

TEST_CASE("the printer form offers the catalogue and the found printers that are not set up yet", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   linked = lab_printer();
    linked.device_id      = "SAVED1";
    backend.saved         = {linked};
    DiscoveredPrinter set_up = found_a1();
    set_up.stable_id         = "SAVED1";
    DiscoveredPrinter unknown = found_a1();
    unknown.stable_id         = "X9X9";
    unknown.device_model_id   = "X9";
    backend.nearby            = {found_a1(), set_up, unknown};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    CHECK_FALSE(conversation.state_json().contains("form"));

    action(conversation, json{{"action", "manual_setup"}});
    const json form = form_of(conversation);
    REQUIRE(form.at("catalog").size() == backend.catalogue.size());
    CHECK(form.at("catalog").front() == json{{"id", "BBL/Bambu Lab A1 mini"}, {"vendorId", "BBL"}, {"brand", "Bambu Lab"},
                                              {"model", "A1 mini"}, {"nozzles", {0.2, 0.4, 0.6, 0.8}}});
    // One already set up is that printer; a model the catalogue lacks
    // cannot be set up here.
    CHECK(form.at("nearby") == json::array({json{{"deviceId", "01P00A3B"}, {"name", "Bambu Lab A1 mini"},
                                                 {"catalogId", "BBL/Bambu Lab A1 mini"}}}));
    CHECK_FALSE(form.contains("options"));
    CHECK(form.at("error") == "");
    // Filling in a form is not news for the model.
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);

    // Printers are added in an Add session only.
    PrinterConversation change(backend, panel);
    change.start(ConversationMode::Change, "Lab Printer");
    action(change, json{{"action", "manual_setup"}});
    CHECK_FALSE(change.state_json().contains("form"));
}

TEST_CASE("the printer form reads one model's plates and filaments, and only for what it offers", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    const json options = form_of(conversation).at("options");
    CHECK(options.at("catalogId") == "BBL/Bambu Lab A1 mini");
    CHECK(options.at("nozzle") == 0.4);
    CHECK(options.at("plates") == json::array({json{{"type", 3}, {"label", "Smooth High Temp Plate"}},
                                               json{{"type", 4}, {"label", "Textured PEI Plate"}}}));
    CHECK(options.at("defaultPlate") == 4);
    CHECK(options.at("filaments").at(1) ==
          json{{"name", "Generic PETG"}, {"label", "Generic PETG"}, {"vendor", "Generic"}, {"material", "PETG"}});
    CHECK(options.at("suggested") == json::array({"Bambu PLA Basic @BBL A1M"}));
    CHECK(options.at("defaultFilament") == "Bambu PLA Basic @BBL A1M");

    // A model the catalogue lacks, or a nozzle the model does not ship, is
    // not asked about; the options already read stay.
    action(conversation, json{{"action", "form_options"}, {"catalogId", "BBL/Bambu Lab X9"}, {"nozzle", 0.4}});
    action(conversation, json{{"action", "form_options"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.5}});
    CHECK(form_of(conversation).at("options").at("nozzle") == 0.4);
    CHECK_FALSE(form_of(conversation).at("loading").get<bool>());
}

TEST_CASE("finishing the printer form saves the printer it describes, and the model hears of it", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.nearby = {found_a1()};
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);

    action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4},
                              {"filament", "Generic PETG"}, {"deviceId", "01P00A3B"}});
    REQUIRE(backend.added.size() == 1);
    const AddPrinterRequest& request = backend.added.front();
    CHECK(request.vendor_id == "BBL");
    CHECK(request.model_id == "Bambu Lab A1 mini");
    CHECK(request.variant == "0.4");
    CHECK(request.material == "Generic PETG");
    CHECK(request.name == "Bambu Lab A1 mini");
    CHECK(request.device_id == "01P00A3B");
    CHECK(request.plate == std::optional<int>(4));

    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() == "The person added Bambu Lab A1 mini themselves with the printer form: Bambu Lab A1 mini, 0.4 mm "
                                 "nozzle, Textured PEI Plate, starting with Generic PETG. It is linked to the printer Bambu Lab "
                                 "A1 mini, which is not connected yet.");
    check_is_a_statement(panel.notes.front());
    CHECK(panel.turns == 1);
    CHECK(panel.added_printers.back() == "Bambu Lab A1 mini");
    CHECK(conversation.printer_name() == "Bambu Lab A1 mini");
    CHECK_FALSE(conversation.state_json().contains("form"));
    // The same model again, from the model's tool, is the printer just added.
    CHECK(refused(conversation, "printer_add", json{{"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}}) == "already_added");
}

TEST_CASE("the printer form starts with the profile's filament unless one is chosen, and sends no plate where none is offered",
          "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.options[{"Prusa/Prusa MK3S", 0.4}] = mk3s_options();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    action(conversation, json{{"action", "manual_setup"}});
    action(conversation, json{{"action", "form_options"}, {"catalogId", "Prusa/Prusa MK3S"}, {"nozzle", 0.4}});
    settle(conversation);

    action(conversation, json{{"action", "form_finish"}, {"catalogId", "Prusa/Prusa MK3S"}, {"nozzle", 0.4}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().material == "Prusa Generic PLA");
    CHECK_FALSE(backend.added.front().plate.has_value());
    CHECK(backend.added.front().device_id.empty());
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() ==
          "The person added Prusa MK3S themselves with the printer form: Prusa MK3S, 0.4 mm nozzle, starting with Prusa Generic PLA.");
}

TEST_CASE("the printer form offers filaments made for other printers and the person's own, saying which do not fit",
          "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    const json options = form_of(conversation).at("options");
    CHECK(options.at("others") == json::array({json{{"name", "Bambu PLA Basic @BBL H2D"}, {"label", "Bambu PLA Silk"}, {"vendor", "Bambu Lab"},
                                                    {"material", "PLA"}, {"madeFor", "the Bambu Lab H2D"}}}));
    CHECK(options.at("own").at(0) == json{{"name", "PETG fast walls"}, {"label", "PETG fast walls"}, {"vendor", "Generic"},
                                          {"material", "PETG"}, {"base", "Generic PETG"}});
    CHECK(options.at("own").at(1).at("madeFor") == "the Bambu Lab X1 Carbon");
    // A filament that fits says nothing about other printers.
    CHECK_FALSE(options.at("filaments").at(0).contains("madeFor"));
}

TEST_CASE("the person's own preset that fits goes in slot 1 as it is", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4},
                              {"filament", "PETG fast walls"}});
    REQUIRE(backend.added.size() == 1);
    // Installed with a filament that fits; the person's own goes in after.
    CHECK(backend.added.front().material == "Bambu PLA Basic @BBL A1M");
    CHECK(backend.starts == std::vector<std::tuple<std::string, std::string, bool>>{{"Bambu Lab A1 mini", "PETG fast walls", false}});
    REQUIRE(panel.notes.size() == 1);
    CHECK_THAT(panel.notes.front(), ContainsSubstring("Textured PEI Plate, starting with their own preset PETG fast walls."));
}

TEST_CASE("a filament made for other printers is copied for the new one", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4},
                              {"filament", "Bambu PLA Basic @BBL H2D"}});
    CHECK(backend.added.front().material == "Bambu PLA Basic @BBL A1M");
    CHECK(backend.starts ==
          std::vector<std::tuple<std::string, std::string, bool>>{{"Bambu Lab A1 mini", "Bambu PLA Basic @BBL H2D", true}});
    REQUIRE(panel.notes.size() == 1);
    CHECK_THAT(panel.notes.front(), ContainsSubstring("starting with a copy of Bambu PLA Silk saved for this printer as "
                                                      "Bambu PLA Basic @BBL H2D @Bambu Lab A1 mini."));
    check_is_a_statement(panel.notes.front());

    // When the copy cannot be made, the printer is still added, and the note
    // says what it starts with instead.
    FakeBackend    failing;
    RecordingPanel told;
    failing.start_problem = "A copy of Matte PLA tuned for Bambu Lab A1 mini could not be saved.";
    PrinterConversation second(failing, told);
    open_on_mini(failing, second);
    action(second, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4},
                        {"filament", "Matte PLA tuned"}});
    REQUIRE(failing.added.size() == 1);
    REQUIRE(told.notes.size() == 1);
    CHECK_THAT(told.notes.front(), ContainsSubstring("Textured PEI Plate. Matte PLA tuned could not be used: A copy of Matte PLA tuned "
                                                     "for Bambu Lab A1 mini could not be saved. It starts with Bambu PLA Basic."));
}

TEST_CASE("creating a filament preset adds the printer first, then puts what was made in slot 1", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.created = "Silk blue @Bambu Lab A1 mini";
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4},
                              {"create", true}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().material == "Bambu PLA Basic @BBL A1M");
    CHECK(backend.creates == std::vector<std::string>{"Bambu Lab A1 mini"});
    CHECK(backend.starts.empty());
    REQUIRE(panel.notes.size() == 1);
    CHECK_THAT(panel.notes.front(), ContainsSubstring("Textured PEI Plate. The person made the filament preset Silk blue @Bambu Lab A1 mini "
                                                      "in the Create filament window; it is in slot 1."));
    CHECK(panel.turns == 1);

    // Closing OrcaSlicer's window without making one leaves the default.
    FakeBackend    empty;
    RecordingPanel told;
    PrinterConversation second(empty, told);
    open_on_mini(empty, second);
    action(second, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4}, {"create", true}});
    REQUIRE(told.notes.size() == 1);
    CHECK_THAT(told.notes.front(), ContainsSubstring("no new preset for this printer is in slot 1, so it starts with Bambu PLA Basic."));
}

TEST_CASE("the printer form saves nothing it did not offer, and says why", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.nearby = {found_a1()};
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    const json finish{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4}};

    const auto refused_with = [&](json payload, const std::string& why) {
        action(conversation, payload);
        CHECK(form_of(conversation).at("error") == why);
        CHECK(backend.added.empty());
        CHECK(panel.notes.empty());
        CHECK(panel.turns == 0);
    };
    json without_plate = finish;
    without_plate.erase("plate");
    refused_with(without_plate, "Choose the plate on this printer.");
    json other_plate = finish;
    other_plate["plate"] = 1;
    refused_with(other_plate, "Choose the plate on this printer.");
    json other_filament = finish;
    other_filament["filament"] = "Prusa Generic PLA";
    refused_with(other_filament, "That filament is no longer on the list. Choose another.");
    json other_device = finish;
    other_device["deviceId"] = "ELSEWHERE";
    refused_with(other_device, "That printer is no longer on the list. Choose it again.");
    json other_nozzle = finish;
    other_nozzle["nozzle"] = 0.6;
    refused_with(other_nozzle, "The form was out of date. Choose the printer again.");

    // What stops the save is the app's to say, and the form stays open.
    backend.refusal = "This printer could not be installed.";
    action(conversation, finish);
    CHECK(form_of(conversation).at("error") == "This printer could not be installed.");
    CHECK(backend.saved.empty());
    CHECK(panel.notes.empty());

    // Asking about another printer clears the reason.
    backend.options[{"BBL/Bambu Lab A1 mini", 0.6}] = mini_options();
    action(conversation, json{{"action", "form_options"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.6}});
    CHECK(form_of(conversation).at("error") == "");
}

TEST_CASE("closing the printer form, or starting another session, saves nothing", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    open_on_mini(backend, conversation);
    action(conversation, json{{"action", "form_close"}});
    CHECK_FALSE(conversation.state_json().contains("form"));

    open_on_mini(backend, conversation);
    conversation.start(ConversationMode::Add);
    CHECK_FALSE(conversation.state_json().contains("form"));
    // Finishing a form that is gone does nothing.
    action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4}, {"plate", 4}});
    CHECK(backend.added.empty());
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);
}

TEST_CASE("printer settings open for the printer the conversation is about", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = PrinterConnectionInfo{"host", "unknown"};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    // Nothing to open before there is a printer.
    conversation.handle_page_message("printer_action", json{{"action", "open_printer_settings"}});
    CHECK(backend.settings_opened.empty());

    conversation.start(ConversationMode::Change, "Lab Printer");
    conversation.handle_page_message("printer_action", json{{"action", "open_printer_settings"}});
    CHECK(backend.settings_opened == std::vector<std::string>{"Lab Printer"});
}

TEST_CASE("a printer conversation writes nothing into the project", "[printer-conversation]")
{
    Workspace::FakeWorkspace workspace;
    const std::filesystem::path project = workspace.auxiliary_data_dir();

    Agent::ProjectPersistence::Config config;
    config.in_memory = true;
    Agent::ProjectPersistence persistence(workspace, std::move(config));
    persistence.attach();

    REQUIRE(persistence.document().has_identity());
    persistence.document().append_message("c-1", Agent::ConversationMessage{"m-1"}, persistence.timestamp());
    persistence.commit();
    persistence.flush();
    REQUIRE(persistence.write_attachment_blob("attachments/a-1/photo.png", "bytes"));

    CHECK(persistence.read_attachment_blob("attachments/a-1/photo.png") == "bytes");
    CHECK_FALSE(std::filesystem::exists(project / "JusPrin"));
}

// -- Inside a real host --------------------------------------------------------

namespace {

// Answers each turn with text, or with the one tool call it was given, and
// records every request and tool result the host hands it. It keeps what it
// sent in the chat's record the way a provider agent does: the person's side
// once sent, then each reply and result, so a later request carries them.
class ScriptedAgent final : public Agent::IAgentService
{
public:
    std::optional<Agent::ToolRequest>   call;
    std::string                         reply{"Understood."};
    std::vector<Agent::AgentRequest>    requests;
    std::vector<Agent::AgentToolResult> results;

    bool ready() const override { return true; }
    bool busy() const override { return m_active; }
    bool start(const Agent::AgentRequest& request) override
    {
        requests.push_back(request);
        m_active = true;
        m_turn   = request;
        if (request.purpose == Agent::AgentRequest::Purpose::ConversationTitle) {
            m_events.push_back(Agent::AgentEvent::delta("Printer"));
            m_events.push_back(Agent::AgentEvent::completed());
            return true;
        }
        std::vector<Agent::RecordItem> sent = JusPrinTest::kept_person_side(request);
        if (call) {
            const std::string call_id = "call-" + std::to_string(requests.size());
            sent.push_back(JusPrinTest::kept_call(call_id, call->tool, call->arguments_json));
            m_events.push_back(Agent::AgentEvent::tool_call({call_id, *call, true}));
            call.reset();
        } else {
            sent.push_back(JusPrinTest::kept_reply(reply));
            m_events.push_back(Agent::AgentEvent::delta(reply));
            m_events.push_back(Agent::AgentEvent::completed());
        }
        JusPrinTest::keep(request, std::move(sent));
        return true;
    }
    bool continue_after_tool(const Agent::AgentToolResult& result) override
    {
        results.push_back(result);
        JusPrinTest::keep(m_turn, {JusPrinTest::kept_result(result), JusPrinTest::kept_reply("Done.")});
        m_events.push_back(Agent::AgentEvent::delta("Done."));
        m_events.push_back(Agent::AgentEvent::completed());
        return true;
    }
    void cancel() override
    {
        m_active = false;
        m_events.clear();
    }
    std::optional<Agent::AgentEvent> poll() override
    {
        if (m_events.empty())
            return std::nullopt;
        Agent::AgentEvent event = std::move(m_events.front());
        m_events.pop_front();
        if (event.kind == Agent::AgentEventKind::Completed || event.kind == Agent::AgentEventKind::Failed)
            m_active = false;
        return event;
    }

private:
    std::deque<Agent::AgentEvent> m_events;
    Agent::AgentRequest           m_turn;
    bool                          m_active{false};
};

// The words of a record's items as the model read them: the person's text,
// the app's notes and the model's replies, without tool calls or results.
std::vector<std::string> record_words(const Agent::ChatRecord& record)
{
    std::vector<std::string> words;
    for (const Agent::RecordItem& item : record.items) {
        for (const Agent::RecordPart& part : item.parts)
            words.push_back(part.text);
        if (!item.text.empty())
            words.push_back(item.text);
        if (item.output.is_object() && item.output.value("type", "") == "message")
            words.push_back(item.output.at("content").get<std::string>());
    }
    return words;
}

// Every string a request hands the model, whatever its channel.
std::string everything_sent(const Agent::AgentRequest& request)
{
    std::string sent = request.user_text;
    for (const std::string& words : record_words({{}, request.unsent}))
        sent += "\n" + words;
    for (const Agent::RecordItem& item : request.record.items) {
        for (const std::string& words : record_words({{}, {item}}))
            sent += "\n" + words;
        sent += "\n" + item.output.dump();
        for (const Agent::RecordResult& result : item.results)
            sent += "\n" + result.output_json;
    }
    return sent;
}

// Where the record's first item of a kind is that `match` accepts, or its size.
template<class Match> std::size_t find_item(const Agent::ChatRecord& record, Match match)
{
    std::size_t i = 0;
    while (i < record.items.size() && !match(record.items[i]))
        ++i;
    return i;
}

bool says(const Agent::RecordItem& item, const std::string& words)
{
    const std::vector<std::string> all = record_words({{}, {item}});
    return std::find(all.begin(), all.end(), words) != all.end();
}

bool is_results(const Agent::RecordItem& item) { return item.kind == Agent::RecordItem::Kind::Results; }

// What PrinterPanel does, without wx: the conversation's host is the
// AgentHost, through the same seams.
class HostedPanel final : public IConversationHost
{
public:
    Agent::AgentHost*          host{nullptr};
    Agent::ProjectPersistence* persistence{nullptr};
    PrinterConversation*       conversation{nullptr};
    int                        closes{0};

    nlohmann::json chat_record() const override
    {
        const Agent::ProjectStateDocument& document = persistence->document();
        return connection_chat_record(document.messages(document.active_conversation_id()), host->tools().activities());
    }
    std::string post_note(const std::string& text) override { return host->post_note(text); }
    std::string post_opening(const std::string& text) override { return host->post_assistant_message(text); }
    void        start_turn() override { host->start_turn(); }
    bool        finish_tool(const std::string& action_id, const Agent::ToolExecutionCoordinator::ExtensionResult& outcome) override
    {
        return host->tools().finish_pending(action_id, outcome);
    }
    bool        update_tool(const std::string& action_id, const std::string& title, bool answer_dialogs) override
    {
        Agent::ToolExecutionCoordinator& tools = host->tools();
        return tools.retitle_pending(action_id, title) && (!answer_dialogs || tools.answer_dialogs_while_pending(action_id));
    }
    void        session_changed() override { profile_changed(); }
    void        profile_changed() override
    {
        if (host != nullptr && conversation != nullptr)
            host->set_session_profile(conversation->profile());
    }
    void close_panel() override { ++closes; }
    void printers_changed(const std::string&) override {}
    std::vector<std::string> opened;
    bool open_in_browser(const std::string& url) override
    {
        opened.push_back(url);
        return true;
    }
    std::string app_version() const override { return "02.06.00.51"; }
};

Agent::ProjectPersistence::Config in_memory()
{
    Agent::ProjectPersistence::Config config;
    config.in_memory = true;
    config.clock     = [] { return "2026-09-18T00:00:00Z"; };
    config.uuid      = [] {
        static int next = 0;
        return "printer-test-" + std::to_string(++next);
    };
    return config;
}

struct PrinterHost
{
    Workspace::FakeWorkspace  workspace;
    Agent::ProjectPersistence persistence{workspace, in_memory()};
    ScriptedAgent*            agent{nullptr};
    Agent::AgentHost          host;
    FakeBackend               backend;
    HostedPanel               panel;
    PrinterConversation       conversation{backend, panel};
    std::vector<std::string>  sent;
    Agent::ToolActivitySubscription settled;

    explicit PrinterHost(ConversationMode mode, bool printer_session = true)
        : host(workspace, persistence, Agent::AgentAvailability::Ready, false, make_agent())
    {
        persistence.attach();
        host.set_send([this](const std::string& envelope) { sent.push_back(envelope); });
        backend.saved = {lab_printer()};
        panel.host         = &host;
        panel.persistence  = &persistence;
        panel.conversation = &conversation;
        if (printer_session) {
            host.set_page_message_handler([this](const std::string& type, const json& payload) {
                return conversation.handle_page_message(type, payload);
            });
            host.set_session_state_provider([this] { return conversation.state_json(); });
            host.set_session_tool_executor([this](Agent::ToolHandler handler, const Agent::ToolActivity& activity) {
                return conversation.execute_tool(handler, activity);
            });
            host.set_session_tool_preflight([this](Agent::ToolHandler handler, Agent::ToolActivity& activity) {
                return conversation.preflight_tool(handler, activity);
            });
            host.set_session_tool_output([this](const Agent::ToolActivity& activity) { return conversation.tool_output(activity); });
            host.set_session_skill([this] { return conversation.connection_skill(); });
            settled = host.tools().subscribe([this](const Agent::ToolActivity& activity) { conversation.tool_settled(activity); });
            conversation.start(mode, "Lab Printer");
            // What the page does once it has the session's facts.
            conversation.handle_page_message("printer_instructions", json{{"text", "You are the printer assistant."}});
        }
        page("hello", {{"protocolVersions", {Agent::Protocol::kVersion}}, {"capabilities", {"streaming"}}});
        // A project conversation's instructions come from its page as well.
        if (!printer_session) {
            page("project_instructions", {{"text", "You are the project assistant."}});
            page("skills", {{"skills", json::array()}});
        }
    }

    Agent::AgentServicePtr make_agent()
    {
        auto scripted = std::make_unique<ScriptedAgent>();
        agent         = scripted.get();
        return scripted;
    }

    void page(const std::string& type, const json& payload)
    {
        static int next = 0;
        host.on_page_message(json{{"protocol", Agent::Protocol::kName},
                                  {"version", Agent::Protocol::kVersion},
                                  {"id", "w-" + std::to_string(++next)},
                                  {"type", type},
                                  {"payload", payload}}
                                 .dump());
    }

    void say(const std::string& text)
    {
        static int next = 0;
        page("user_message", {{"clientMessageId", "c-" + std::to_string(++next)}, {"text", text}});
    }

    void pump()
    {
        for (int i = 0; i < 200; ++i) {
            host.pump_stream();
            host.pump_tools();
        }
    }

    std::vector<Agent::ToolActivity> activities() const { return host.tools().activities(); }

    std::vector<json> of_type(const std::string& type) const
    {
        std::vector<json> found;
        for (const std::string& envelope : sent)
            if (json::parse(envelope).value("type", "") == type)
                found.push_back(json::parse(envelope));
        return found;
    }
};

} // namespace

TEST_CASE("printer_add and printer_change run on the person's yes", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Change);
    harness.agent->call = Agent::ToolRequest{"printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}.dump()};
    harness.say("yes, set 0.6 mm");
    harness.pump();

    auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    CHECK(activities.front().state == Agent::ToolState::Succeeded);
    CHECK(harness.backend.saved.front().nozzle == 0.6);
    REQUIRE(harness.agent->results.size() == 1);
    const json output = json::parse(harness.agent->results.front().output_json);
    CHECK(output.at("changed")[0].at("after") == 0.6);
    CHECK_FALSE(output.contains("workspace"));
    CHECK_FALSE(harness.agent->requests.front().session.include_workspace);

    // Adding is an Add session's.
    PrinterHost adding(ConversationMode::Add);
    adding.say("a prusa mk3s");
    adding.pump();
    adding.agent->call = Agent::ToolRequest{"printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}.dump()};
    adding.say("yes, add it");
    adding.pump();
    activities = adding.activities();
    REQUIRE(activities.size() == 1);
    CHECK(adding.backend.added.size() == 1);

    // The receipt reaches the page after the reply that added the printer...
    const json receipt = adding.conversation.state_json().at("blocks").back();
    CHECK(receipt.at("kind") == "added");
    CHECK(receipt.at("afterMessageId") == activities.back().correlation_id);
    CHECK_FALSE(activities.back().correlation_id.empty());
    // ...and never the model: the next request carries the thread's words,
    // and the receipt is not one of them.
    adding.agent->call.reset();
    adding.say("thanks");
    adding.pump();
    const Agent::AgentRequest& next = adding.agent->requests.back();
    CHECK(next.user_text == "thanks");
    CHECK(next.record.items.size() >= 4);
    for (const std::string& words : record_words(next.record)) {
        INFO(words);
        CHECK(words.find("added") == std::string::npos);
        CHECK(words.find("Prusa MK3S") == std::string::npos);
    }
}

TEST_CASE("adding from a Change session reaches the model as a refusal that says where adding is", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Change);
    harness.agent->call = Agent::ToolRequest{"printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}.dump()};
    harness.say("add my prusa mk3s too");
    harness.pump();

    const auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    CHECK(activities.front().state == Agent::ToolState::Failed);
    CHECK(harness.backend.added.empty());
    CHECK(harness.conversation.printer_name() == "Lab Printer");
    REQUIRE(harness.agent->results.size() == 1);
    const json error = json::parse(harness.agent->results.front().output_json).at("error");
    CHECK(error.at("code") == "not_in_this_conversation");
    CHECK_THAT(error.at("message").get<std::string>(), ContainsSubstring("+ Add printer on Home"));
}

TEST_CASE("a refused printer tool reaches the model as its error, with nothing saved", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Change);
    harness.agent->call = Agent::ToolRequest{"printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.3}}.dump()};
    harness.say("i put a 0.3 on it");
    harness.pump();

    REQUIRE(harness.agent->results.size() == 1);
    CHECK(json::parse(harness.agent->results.front().output_json).at("error").at("code") == "unknown_nozzle");
    CHECK(harness.backend.changed.empty());
}

namespace {

// Drives printer_bambu_connect to its local input form in a Connect session about a
// Bambu Lab printer on the network.
Agent::ToolActivity submit_connect(PrinterHost& harness)
{
    harness.backend.connection_info = bambu_on_network();
    harness.agent->call = Agent::ToolRequest{"printer_bambu_connect", json{{"deviceId", "01P00A3B"}}.dump()};
    harness.say("yes, connect it");
    harness.pump();
    const auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    REQUIRE(activities.front().state == Agent::ToolState::Running);
    REQUIRE(harness.conversation.state_json().at("credentialRequest").at("actionId") == activities.front().action_id);
    return activities.front();
}

} // namespace

TEST_CASE("the credential reaches the printer and nothing else", "[printer-conversation][host]")
{
    const std::string secret = "S3cretCode";
    PrinterHost harness(ConversationMode::Connect);
    const Agent::ToolActivity request = submit_connect(harness);
    CHECK(request.title == "Waiting for the access code for Workshop");

    harness.page("printer_action", {{"action", "connect"}, {"actionId", request.action_id}, {"credential", secret}});
    harness.pump();
    REQUIRE(harness.backend.connects.size() == 1);
    CHECK(harness.backend.connects.front() == std::vector<std::string>{"Lab Printer", "01P00A3B", secret});

    // The printer settles, the app says so, and the model answers.
    harness.backend.connection_info.state = "verified";
    harness.conversation.tick(std::chrono::steady_clock::now());
    harness.pump();

    for (const Agent::ToolActivity& activity : harness.activities()) {
        CHECK_THAT(activity.arguments_json, !ContainsSubstring(secret));
        CHECK_THAT(activity.result_json, !ContainsSubstring(secret));
    }
    for (const std::string& envelope : harness.sent)
        CHECK_THAT(envelope, !ContainsSubstring(secret));
    for (const Agent::AgentRequest& request : harness.agent->requests)
        CHECK_THAT(everything_sent(request), !ContainsSubstring(secret));
    for (const Agent::AgentToolResult& result : harness.agent->results)
        CHECK_THAT(result.output_json, !ContainsSubstring(secret));
    CHECK_THAT(harness.persistence.document().dump(), !ContainsSubstring(secret));
    CHECK_FALSE(harness.conversation.state_json().contains("credentialRequest"));
}

TEST_CASE("printer_bambu_connect stays open through the form and the attempt, and its outcome ends the same turn",
          "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Connect);
    const Agent::ToolActivity request = submit_connect(harness);
    CHECK(harness.agent->results.empty());
    CHECK(harness.agent->busy());
    const std::size_t requests = harness.agent->requests.size();

    harness.page("printer_action", {{"action", "connect"}, {"actionId", request.action_id}, {"credential", "1234"}});
    harness.pump();
    CHECK(harness.activities().front().title == "Checking Workshop");
    CHECK(harness.activities().front().state == Agent::ToolState::Running);
    harness.backend.connection_info.state = "verified";
    harness.conversation.tick(std::chrono::steady_clock::now());
    harness.pump();

    CHECK(harness.activities().front().state == Agent::ToolState::Succeeded);
    REQUIRE(harness.agent->results.size() == 1);
    CHECK(json::parse(harness.agent->results.front().output_json) ==
          json{{"state", "verified"}, {"message", "The app connected to Lab Printer. The app sent the access code typed in the form."}});
    // The same turn: no note and no new request.
    CHECK(harness.agent->requests.size() == requests);
    CHECK_FALSE(harness.agent->busy());
}

TEST_CASE("printer_host_connect stays open while its test runs, and its result reaches the model in the same turn",
          "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Connect);
    harness.backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    harness.agent->call = Agent::ToolRequest{"printer_host_connect", json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}}.dump()};
    harness.say("connect it at 192.168.1.42");
    harness.pump();

    // The coordinator keeps pumping; the call waits for the test.
    REQUIRE(harness.activities().size() == 1);
    CHECK(harness.activities().front().state == Agent::ToolState::Running);
    CHECK(harness.activities().front().title == "Checking 192.168.1.42");
    CHECK(harness.agent->results.empty());
    CHECK(harness.agent->busy());
    const std::size_t requests = harness.agent->requests.size();
    REQUIRE(harness.backend.tested);

    harness.backend.tested(HostTestOutcome{"verified", "Moonraker at 192.168.1.42 accepted the test. The test sent no API key.", "none"});
    harness.pump();
    CHECK(harness.activities().front().state == Agent::ToolState::Succeeded);
    REQUIRE(harness.agent->results.size() == 1);
    CHECK(json::parse(harness.agent->results.front().output_json) ==
          json{{"state", "verified"}, {"message", "Moonraker at 192.168.1.42 accepted the test. The test sent no API key."}});
    // The same turn: no note and no new request.
    CHECK(harness.agent->requests.size() == requests);
    CHECK_FALSE(harness.agent->busy());
}

TEST_CASE("a pending connection call the person stops ends cancelled, and stops its test", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Connect);
    harness.backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    harness.agent->call = Agent::ToolRequest{"printer_host_connect", json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}}.dump()};
    harness.say("connect it at 192.168.1.42");
    harness.pump();
    REQUIRE(harness.activities().size() == 1);
    const std::string action = harness.activities().front().action_id;
    REQUIRE(harness.host.tools().cancel(action));
    harness.pump();
    CHECK(harness.activities().front().state == Agent::ToolState::Cancelled);
    CHECK(harness.backend.cancelled == std::vector<std::string>{"Lab Printer"});
    // Nothing is left to stop when the next session starts.
    harness.conversation.start(ConversationMode::Connect, "Lab Printer");
    CHECK(harness.backend.cancelled.size() == 1);
}

TEST_CASE("notes reach the model as the app's words, in a printer session and a project's", "[printer-conversation][host]")
{
    SECTION("printer session") {
        PrinterHost harness(ConversationMode::Add);
        harness.host.post_note("The person said Bambu Lab A1 mini is not their printer.");
        harness.host.start_turn();
        harness.pump();

        REQUIRE_FALSE(harness.agent->requests.empty());
        const Agent::AgentRequest& request = harness.agent->requests.front();
        // A turn the app started carries no words of the person's.
        CHECK(request.user_text.empty());
        const auto note = std::find_if(request.unsent.begin(), request.unsent.end(), [](const auto& item) {
            return item.text == "The person said Bambu Lab A1 mini is not their printer.";
        });
        REQUIRE(note != request.unsent.end());
        CHECK(note->kind == Agent::RecordItem::Kind::Operator);
    }
    SECTION("project session") {
        PrinterHost harness(ConversationMode::Add, /*printer_session=*/false);
        harness.host.post_note("Printer changed to Bambu Lab A1 mini.");
        harness.say("hello");
        harness.pump();

        REQUIRE_FALSE(harness.agent->requests.empty());
        const auto& unsent = harness.agent->requests.front().unsent;
        CHECK(std::any_of(unsent.begin(), unsent.end(), [](const Agent::RecordItem& item) {
            return item.kind == Agent::RecordItem::Kind::Operator && item.text == "Printer changed to Bambu Lab A1 mini.";
        }));
    }
}

TEST_CASE("a session sends its earlier tool calls again as sent, printer or project", "[printer-conversation][host]")
{
    SECTION("printer session") {
        PrinterHost harness(ConversationMode::Connect);
        harness.backend.connection_info = bambu_on_network();
        harness.agent->call             = Agent::ToolRequest{"printer_connection_status", "{}"};
        harness.say("is it online?");
        harness.pump();
        REQUIRE(harness.agent->results.size() == 1);
        const Agent::AgentToolResult live = harness.agent->results.front();
        CHECK(live.output_json.find("01P00A3B") != std::string::npos);

        harness.say("connect it");
        harness.pump();
        const Agent::AgentRequest& request = harness.agent->requests.back();
        REQUIRE(request.user_text == "connect it");
        const Agent::ChatRecord& record = request.record;
        const std::size_t results = find_item(record, is_results);
        REQUIRE(results < record.items.size());
        REQUIRE(record.items[results].results.size() == 1);
        CHECK(record.items[results].results.front().call_id == live.call_id);
        // The very words the model read when the call ran, so the next turn
        // has the printer's id from it.
        CHECK(record.items[results].results.front().output_json == live.output_json);
        // After the question and the call that led to it, before the reply
        // that read it.
        const std::size_t asked   = find_item(record, [](const auto& item) { return says(item, "is it online?"); });
        const std::size_t replied = find_item(record, [](const auto& item) { return says(item, "Done."); });
        REQUIRE(results > 0);
        CHECK(asked < results - 1);
        CHECK(record.items[results - 1].output.at("call_id") == live.call_id);
        CHECK(results < replied);
        CHECK(replied < record.items.size());
        CHECK(std::count_if(record.items.begin(), record.items.end(), is_results) == 1);
    }
    SECTION("project session") {
        PrinterHost harness(ConversationMode::Add, /*printer_session=*/false);
        harness.agent->call = Agent::ToolRequest{"printer_list", "{}"};
        harness.say("which printers are there?");
        harness.pump();
        REQUIRE(harness.agent->results.size() == 1);
        CHECK(harness.agent->results.front().state == "succeeded");

        harness.say("and now?");
        harness.pump();
        const Agent::AgentRequest& request = harness.agent->requests.back();
        REQUIRE(request.user_text == "and now?");
        const std::size_t results = find_item(request.record, is_results);
        REQUIRE(results < request.record.items.size());
        // The project's tools answer with the workspace beside the result,
        // and it stays: an earlier request is never changed.
        const std::string& sent = request.record.items[results].results.front().output_json;
        CHECK(sent == harness.agent->results.front().output_json);
        CHECK(json::parse(sent).contains("workspace"));
    }
}

TEST_CASE("a large earlier result is sent again whole", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Connect);
    harness.backend.connection_info = bambu_on_network();
    std::string address(20 * 1024, 'x');
    harness.backend.connection_info.message = address;
    harness.agent->call = Agent::ToolRequest{"printer_connection_status", "{}"};
    harness.say("is it online?");
    harness.pump();
    REQUIRE(harness.agent->results.size() == 1);
    REQUIRE(harness.agent->results.front().output_json.size() > 20 * 1024);

    harness.say("connect it");
    harness.pump();
    const Agent::ChatRecord& record = harness.agent->requests.back().record;
    const std::size_t results = find_item(record, is_results);
    REQUIRE(results < record.items.size());
    CHECK(record.items[results].results.front().output_json == harness.agent->results.front().output_json);
}

TEST_CASE("a long chat is sent again whole, every word and every result", "[printer-conversation][host][history]")
{
    // Twelve turns that each bring back a result of 8 KiB: about 100 KiB of
    // history, all of it sent on the next turn.
    PrinterHost harness(ConversationMode::Connect);
    for (int turn = 1; turn <= 12; ++turn) {
        harness.backend.connection_info         = bambu_on_network();
        harness.backend.connection_info.message = std::string(8 * 1024, 'x');
        harness.agent->call                     = Agent::ToolRequest{"printer_connection_status", "{}"};
        harness.say("check " + std::to_string(turn));
        harness.pump();
    }
    REQUIRE(harness.agent->results.size() == 12);
    harness.say("and now?");
    harness.pump();
    const auto& items = harness.agent->requests.back().record.items;

    std::vector<std::size_t> results;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (is_results(items[i]))
            results.push_back(i);
    REQUIRE(results.size() == 12);
    for (int turn = 1; turn <= 12; ++turn) {
        const std::size_t at = results[turn - 1];
        REQUIRE(at >= 2);
        REQUIRE(at + 1 < items.size());
        CHECK(says(items[at - 2], "check " + std::to_string(turn)));
        CHECK(items[at - 1].kind == Agent::RecordItem::Kind::Assistant);
        CHECK(items[at].results.front().output_json == harness.agent->results[turn - 1].output_json);
        CHECK(says(items[at + 1], "Done."));
    }
}

TEST_CASE("a printer session spends no request on a chat title, so the app's turn is never stuck behind one",
          "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Add);
    harness.say("the small bambu one");
    // The reply is done; a project conversation would now ask for a title.
    for (int i = 0; i < 20 && harness.host.stream_active(); ++i)
        harness.host.pump_stream();
    harness.host.post_note("The person said Bambu Lab A1 mini is not their printer.");
    harness.host.start_turn();
    harness.pump();

    for (const Agent::AgentRequest& request : harness.agent->requests)
        CHECK(request.purpose != Agent::AgentRequest::Purpose::ConversationTitle);
    REQUIRE(harness.agent->requests.size() == 2);
    CHECK(harness.agent->requests.back().user_text.empty());
}

TEST_CASE("an app-started turn in a project conversation cancels a pending title rather than wait for it",
          "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Add, /*printer_session=*/false);
    harness.say("hello");
    // Pump only until the reply completes, which is when the title starts.
    for (int i = 0; i < 20 && harness.host.stream_active(); ++i)
        harness.host.pump_stream();
    REQUIRE(harness.agent->requests.back().purpose == Agent::AgentRequest::Purpose::ConversationTitle);

    harness.host.start_turn();
    harness.pump();
    std::vector<const Agent::AgentRequest*> replies;
    for (const Agent::AgentRequest& request : harness.agent->requests)
        if (request.purpose == Agent::AgentRequest::Purpose::Reply)
            replies.push_back(&request);
    REQUIRE(replies.size() == 2);
    CHECK(replies.back()->user_text.empty());
}

TEST_CASE("a printer session never answers with the project assistant's instructions", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Add);
    // As if the page had not written its instructions yet.
    harness.conversation.start(ConversationMode::Add);
    harness.host.set_session_profile(harness.conversation.profile());
    harness.say("the small bambu one");
    harness.pump();

    CHECK(harness.agent->requests.empty());
    const auto failed = harness.of_type("assistant_failed");
    REQUIRE(failed.size() == 1);
    CHECK(failed.front()["payload"]["error"]["code"] == "instructions_missing");

    // Once the page has written them, the next turn goes.
    harness.conversation.handle_page_message("printer_instructions", json{{"text", "You add printers."}});
    harness.say("the small bambu one");
    harness.pump();
    REQUIRE(harness.agent->requests.size() == 1);
    CHECK(harness.agent->requests.front().session.instructions == "You add printers.");
}

// -- The shipped profiles, and what goes over the wire ----------------------

namespace {

class CatalogBackend final : public IPrinterBackend
{
public:
    CatalogBackend()
        : printers(PrinterCatalog::load(std::string(JUSPRIN_SOURCE_DIR) + "/resources").panel_printers())
    {}
    std::vector<CatalogPrinter> printers;
    std::vector<SavedPrinter>   saved;

    const std::vector<CatalogPrinter>& catalog() const override { return printers; }
    std::vector<DiscoveredPrinter>     network_printers() const override { return {}; }
    SetupOptions setup_options(const std::string&, double) const override { return {}; }
    std::vector<SavedPrinter>          saved_printers() const override { return saved; }
    SavedPrinter                       stock_printer(const std::string&) const override { return {}; }
    std::string add_printer(const AddPrinterRequest&, SavedPrinter&) override { return {}; }
    std::string change_printer(const ChangePrinterRequest&, SavedPrinter&, std::vector<std::string>&) override { return {}; }
    std::string remove_printer(const std::string&) override { return {}; }
    ManualPrinterResult run_manual_setup() override { return {}; }
    void        open_printer_settings(const std::string&) override {}

    const CatalogPrinter* find(const std::string& id) const
    {
        const auto found = std::find_if(printers.begin(), printers.end(), [&id](const CatalogPrinter& printer) { return printer.id == id; });
        return found == printers.end() ? nullptr : &*found;
    }
};

class CapturingTransport final : public Agent::IAgentHttpTransport
{
public:
    std::vector<Agent::AgentHttpRequest> requests;
    bool post(Agent::AgentHttpRequest request, EventFn) override
    {
        requests.push_back(std::move(request));
        return true;
    }
    void cancel() override {}
};

// The body the app would post to OpenAI for this session's first turn.
json request_body(const Agent::AgentSessionProfile& profile, const std::string& user_text, Agent::ChatRecord record = {},
                  std::vector<Agent::RecordItem> unsent = {})
{
    auto                transport = std::make_unique<CapturingTransport>();
    CapturingTransport* capture   = transport.get();
    Agent::OpenAIResponsesAgent agent({"key", "gpt-5.4-mini", "https://api.openai.com/v1/responses"}, std::move(transport));
    Agent::AgentRequest request;
    request.request_id   = "printer-1";
    request.user_text    = user_text;
    request.session      = profile;
    request.record       = std::move(record);
    request.unsent       = std::move(unsent);
    request.on_recorded  = [](const std::string&, std::vector<Agent::RecordItem>) {};
    request.keep_blob    = [](const std::string&) -> std::optional<std::string> { return "chats/c/blob"; };
    REQUIRE(agent.start(request));
    REQUIRE(capture->requests.size() == 1);
    return json::parse(capture->requests.front().body);
}

} // namespace

// Reads resources/profiles as shipped, on purpose: the names, plates and
// filaments below are what those files say today. A profile sync from
// OrcaSlicer that changes one fails here without a JusPrin change; check the
// catalogue reader still reads it right, then take the new value from the file.
TEST_CASE("the shipped profiles give the panel the printers people name", "[printer-conversation][catalog]")
{
    CatalogBackend backend;
    CHECK(backend.printers.size() > 250);

    const CatalogPrinter* a1 = backend.find("BBL/Bambu Lab A1 mini");
    REQUIRE(a1 != nullptr);
    // The vendor file says "Bambulab"; the model's own spelling is the brand.
    CHECK(a1->vendor_name == "Bambu Lab");
    CHECK(a1->model_name == "A1 mini");
    CHECK(a1->display_name() == "Bambu Lab A1 mini");
    CHECK(a1->device_model_id == "N1");
    CHECK(a1->default_plate == "Textured PEI Plate");
    CHECK(a1->nozzles == std::vector<double>{0.2, 0.4, 0.6, 0.8});
    CHECK(a1->filament_for(0.4) == "Bambu PLA Basic @BBL A1M");

    const CatalogPrinter* mk3s = backend.find("Prusa/Prusa MK3S");
    REQUIRE(mk3s != nullptr);
    CHECK(mk3s->display_name() == "Prusa MK3S");
    CHECK(mk3s->default_plate.empty());
    // The profile's own default, not the wizard's first tick (ABS).
    CHECK(mk3s->filament_for(0.4) == "Prusa Generic PLA");

    CHECK(backend.find("Voron/Voron 2.4 350") != nullptr);
    CHECK(std::none_of(backend.printers.begin(), backend.printers.end(),
                       [](const CatalogPrinter& printer) { return printer.vendor_id == "OrcaArena"; }));
    // Every id is unique, so the model can name any of them.
    std::set<std::string> ids;
    for (const CatalogPrinter& printer : backend.printers)
        CHECK(ids.insert(printer.id).second);
}

TEST_CASE("the model is offered every printer tool, the settings tools, and no project", "[printer-conversation][openai]")
{
    CatalogBackend backend;
    backend.saved = {lab_printer()};
    RecordingPanel panel;

    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    adding.handle_page_message("printer_instructions", json{{"text", "You add printers."}});
    const json body = request_body(adding.profile(), "the small bambu one");
    std::vector<std::string> names;
    const std::vector<std::string> settings = PrinterConversation::settings_tools();
    for (const json& tool : body.at("tools")) {
        // The provider's web search is not the app's.
        if (tool.at("type") != "function")
            continue;
        names.push_back(tool.at("name"));
        // No tool takes a credential from the model.
        for (const char* secret : {"accessCode", "apiKey", "credential", "password"})
            CHECK_FALSE(tool.at("parameters").at("properties").contains(secret));
    }
    std::vector<std::string> offered = PrinterConversation::session_tools();
    offered.insert(offered.end(), settings.begin(), settings.end());
    std::sort(names.begin(), names.end());
    std::sort(offered.begin(), offered.end());
    CHECK(names == offered);
    CHECK(body.at("instructions") == "You add printers.");
    const json& user = body.at("input").back();
    CHECK(user.at("role") == "user");
    CHECK(user.at("content")[0].at("text") == "the small bambu one");
}

TEST_CASE("a turn the app started ends on its note, with no empty message from the person", "[printer-conversation][openai]")
{
    CatalogBackend      backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    Agent::ChatRecord record{"responses"};
    record.items.push_back({Agent::RecordItem::Kind::Assistant, "m-1", {}, "What printer do you have?"});
    record.items.push_back({Agent::RecordItem::Kind::User, "m-2", {{Agent::RecordPart::Kind::Text, "the small bambu one"}}});
    const json body = request_body(conversation.profile(), "", std::move(record),
                                   {{Agent::RecordItem::Kind::Operator, "m-3", {}, "The person said Bambu Lab A1 mini is not their printer."}});
    const json& input = body.at("input");
    REQUIRE_FALSE(input.empty());
    CHECK(input.back() == json{{"role", "developer"}, {"content", "The person said Bambu Lab A1 mini is not their printer."}});
    // The person's one message, from the record; the turn adds none.
    CHECK(std::count_if(input.begin(), input.end(), [](const json& item) { return item.value("role", "") == "user"; }) == 1);
}

// Not a check: writes what the evaluation replays against the model the app
// ships. Run with PRINTER_EVAL_OUT=<dir> agent_bridge_tests "[.printer-eval]".
TEST_CASE("write the printer session requests for the evaluation", "[.printer-eval]")
{
    const char* out = std::getenv("PRINTER_EVAL_OUT");
    REQUIRE(out != nullptr);
    CatalogBackend backend;
    backend.saved = {lab_printer()};
    RecordingPanel panel;

    // The page writes the instructions (printerInstructions.ts); its
    // eval writer fills them in from these sessions' state.
    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    adding.handle_page_message("printer_instructions", json{{"text", "__INSTRUCTIONS__"}});
    std::ofstream(std::string(out) + "/add_request.json") << request_body(adding.profile(), "__USER__").dump(2);
    std::ofstream(std::string(out) + "/add_session.json") << adding.state_json().dump(2);

    PrinterConversation changing(backend, panel);
    changing.start(ConversationMode::Change, "Lab Printer");
    changing.handle_page_message("printer_instructions", json{{"text", "__INSTRUCTIONS__"}});
    std::ofstream(std::string(out) + "/change_request.json") << request_body(changing.profile(), "__USER__").dump(2);
    std::ofstream(std::string(out) + "/change_session.json") << changing.state_json().dump(2);

    // Every printer the model may name, as printer_identify returns it with
    // nothing said about the nozzle.
    json identified_all = json::object();
    for (const CatalogPrinter& printer : backend.printers)
        identified_all[printer.id] = ran(adding, "printer_identify", json{{"catalogIds", {printer.id}}})["printers"][0];
    std::ofstream(std::string(out) + "/identify_results.json") << identified_all.dump(2);
}

// -- The skill for a printer's connection type ---------------------------------

namespace {

// The Bambu Lab skill as the page bundles it: the file's text after its
// frontmatter (skillParser.ts).
std::string skill_text(const std::string& name)
{
    std::ifstream file(std::string(JUSPRIN_SOURCE_DIR) + "/src/slic3r/GUI/JusPrin/AgentUI/src/skills/" + name + "/SKILL.md");
    REQUIRE(file.good());
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string end = "\n---\n";
    const std::size_t body = text.find(end, 3);
    REQUIRE(body != std::string::npos);
    text = text.substr(text.find_first_not_of('\n', body + end.size()));
    return text.substr(0, text.find_last_not_of("\n") + 1);
}

std::string connect_bambu_text() { return skill_text("connect-bambu"); }

SavedPrinter bambu_printer()
{
    SavedPrinter printer = lab_printer();
    printer.vendor_id    = "BBL";
    return printer;
}

std::optional<Agent::ToolError> preflight(PrinterConversation& conversation, const char* tool, const json& arguments)
{
    Agent::ToolActivity activity = call(tool, arguments, "m-1");
    return conversation.preflight_tool(Agent::ToolRegistry::instance().find(tool)->handler, activity);
}

std::optional<std::string> trigger(const PrinterConversation& conversation)
{
    const std::optional<Agent::SessionSkill> skill = conversation.connection_skill();
    return skill ? std::optional<std::string>(skill->trigger) : std::nullopt;
}

bool carries_skill(const std::vector<Agent::AgentSkillUse>& skills)
{
    return std::any_of(skills.begin(), skills.end(), [](const Agent::AgentSkillUse& skill) {
        return skill.trigger == Agent::Protocol::SkillTrigger::kConnectBambu && skill.stays && skill.text == connect_bambu_text();
    });
}

} // namespace

TEST_CASE("the connection skill is in force once the chat has a saved printer, chosen by its vendor", "[printer-conversation][connection-skill]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    const std::optional<std::string> bambu = std::string("connect_bambu");
    const std::optional<std::string> host  = std::string("connect_print_host");

    // Before anything is said: what the person says first about connecting
    // is read with it (2026-11-04).
    SECTION("a chat opened to connect a printer") {
        conversation.start(ConversationMode::Connect, "Lab Printer");
        CHECK(trigger(conversation) == bambu);
    }
    SECTION("a chat about a printer, before any connection tool") {
        conversation.start(ConversationMode::Change, "Lab Printer");
        CHECK(trigger(conversation) == bambu);
    }
    SECTION("any other vendor gets the one print host skill, whatever kind it turns out to be") {
        backend.saved           = {lab_printer()};
        backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
        conversation.start(ConversationMode::Connect, "Lab Printer");
        CHECK(trigger(conversation) == host);
        REQUIRE_FALSE(preflight(conversation, "printer_host_connect", json{{"hostType", "octoprint"}, {"address", "192.168.1.42"}}));
        CHECK(trigger(conversation) == host);
    }
    SECTION("a chat that adds a printer has none until printer_add has added it") {
        backend.saved.clear();
        conversation.start(ConversationMode::Add);
        CHECK_FALSE(trigger(conversation));
        CHECK(refused(conversation, "printer_connection_status", json::object()) == "no_printer");
        CHECK_FALSE(trigger(conversation));
        ran(conversation, "printer_add", json{{"catalogId", "BBL/Bambu Lab A1 mini"}});
        REQUIRE(backend.added.size() == 1);
        CHECK(trigger(conversation) == bambu);
    }
    SECTION("the printer form adds one too, with a model alone") {
        backend.saved.clear();
        open_on_mini(backend, conversation);
        CHECK_FALSE(trigger(conversation));
        action(conversation, json{{"action", "form_finish"}, {"catalogId", "BBL/Bambu Lab A1 mini"}, {"nozzle", 0.4},
                                  {"plate", 4}, {"filament", "Generic PETG"}});
        REQUIRE(backend.added.size() == 1);
        CHECK(trigger(conversation) == bambu);
    }
    SECTION("the project's stock printer has no connection to make") {
        SavedPrinter shipped = bambu_printer();
        shipped.name         = "Bambu Lab A1 mini 0.4 nozzle";
        backend.saved.clear();
        backend.stock = {shipped};
        conversation.start(ConversationMode::Change, "Bambu Lab A1 mini 0.4 nozzle");
        CHECK_FALSE(trigger(conversation));
    }
}

TEST_CASE("what reaches the model: an added printer's skill comes with printer_add's result and stays", "[printer-conversation][connection-skill]")
{
    PrinterHost printer(ConversationMode::Add);
    printer.backend.saved.clear();
    printer.page("skills", {{"skills", json::array({json{{"name", "connect-bambu"}, {"trigger", "connect_bambu"}, {"text", connect_bambu_text()}}})}});

    printer.agent->call = Agent::ToolRequest{"printer_add", json{{"catalogId", "BBL/Bambu Lab A1 mini"}}.dump()};
    printer.say("I have an A1 mini");
    printer.pump();
    REQUIRE(printer.backend.added.size() == 1);
    REQUIRE(printer.backend.saved.front().vendor_id == "BBL");
    // The request that made the call was sent before there was a printer.
    REQUIRE(printer.agent->requests.size() == 1);
    CHECK(printer.agent->requests.front().skills.empty());
    REQUIRE(printer.agent->results.size() == 1);
    CHECK(carries_skill(printer.agent->results.back().skills));

    printer.say("Yes, connect it");
    printer.pump();
    CHECK(carries_skill(printer.agent->requests.back().skills));
}

TEST_CASE("what reaches the model: a chat about a saved printer has the skill on its first request", "[printer-conversation][connection-skill]")
{
    for (const ConversationMode mode : {ConversationMode::Change, ConversationMode::Connect}) {
        CAPTURE(mode == ConversationMode::Change ? "change" : "connect");
        PrinterHost printer(mode);
        printer.backend.saved = {bambu_printer()};
        printer.page("skills", {{"skills", json::array({json{{"name", "connect-bambu"}, {"trigger", "connect_bambu"}, {"text", connect_bambu_text()}}})}});
        printer.say("What nozzle is on it?");
        printer.pump();
        REQUIRE_FALSE(printer.agent->requests.empty());
        CHECK(carries_skill(printer.agent->requests.front().skills));
    }
}

TEST_CASE("what reaches the model: a print host chat has the print host skill from its first request", "[printer-conversation][connection-skill]")
{
    PrinterHost printer(ConversationMode::Change);
    printer.backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    const std::string text = skill_text("connect-print-host");
    printer.page("skills", {{"skills", json::array({json{{"name", "connect-bambu"}, {"trigger", "connect_bambu"}, {"text", connect_bambu_text()}},
                                                    json{{"name", "connect-print-host"}, {"trigger", "connect_print_host"}, {"text", text}}})}});
    printer.say("Can I connect it?");
    printer.pump();
    REQUIRE_FALSE(printer.agent->requests.empty());
    const std::vector<Agent::AgentSkillUse>& skills = printer.agent->requests.front().skills;
    REQUIRE(skills.size() == 1);
    CHECK(skills.front().trigger == "connect_print_host");
    CHECK(skills.front().stays);
    CHECK(skills.front().text == text);
    CHECK(skills.front().tools == std::vector<std::string>{"issue_report_open"});
}

// -- An issue for the developers ------------------------------------------------

namespace {

// The tools a request offers, by name, as the app would post them to OpenAI.
std::vector<std::string> offered(Agent::AgentRequest request)
{
    auto                transport = std::make_unique<CapturingTransport>();
    CapturingTransport* capture   = transport.get();
    Agent::OpenAIResponsesAgent agent({"key", "gpt-5.4-mini", "https://api.openai.com/v1/responses"}, std::move(transport));
    request.on_recorded = [](const std::string&, std::vector<Agent::RecordItem>) {};
    request.keep_blob   = [](const std::string&) -> std::optional<std::string> { return "chats/c/blob"; };
    REQUIRE(agent.start(request));
    REQUIRE(capture->requests.size() == 1);
    const json body = json::parse(capture->requests.front().body);
    std::vector<std::string> names;
    // The app's tools by name; the provider's web search has only a type.
    for (const json& tool : body.value("tools", json::array()))
        if (tool.at("type") == "function")
            names.push_back(tool.at("name"));
    return names;
}

bool offers(const Agent::AgentRequest& request, const std::string& tool)
{
    const std::vector<std::string> names = offered(request);
    return std::find(names.begin(), names.end(), tool) != names.end();
}

json connect_bambu_skills()
{
    return json{{"skills", json::array({json{{"name", "connect-bambu"}, {"trigger", "connect_bambu"}, {"text", connect_bambu_text()}}})}};
}

std::string encoded(const std::string& text)
{
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
            out += static_cast<char>(c);
        else {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

} // namespace

TEST_CASE("what reaches the model: issue_report_open is offered from the connection skill on, and never before",
          "[printer-conversation][connection-skill][issue-report]")
{
    PrinterHost printer(ConversationMode::Add);
    printer.backend.saved.clear();
    printer.page("skills", connect_bambu_skills());

    printer.say("Hi");
    printer.pump();
    REQUIRE(printer.agent->requests.size() == 1);
    CHECK(offers(printer.agent->requests.back(), "printer_host_connect"));
    CHECK(offers(printer.agent->requests.back(), "printer_bambu_connect"));
    CHECK_FALSE(offers(printer.agent->requests.back(), "issue_report_open"));

    printer.agent->call = Agent::ToolRequest{"printer_add", json{{"catalogId", "BBL/Bambu Lab A1 mini"}}.dump()};
    printer.say("I have an A1 mini");
    printer.pump();
    // The request that made the call was sent before the printer was added.
    CHECK_FALSE(offers(printer.agent->requests.back(), "issue_report_open"));
    // The result that brings the skill brings the tool with it, for the
    // continuation of that response (test_provider_agent.cpp).
    REQUIRE(printer.agent->results.size() == 1);
    const std::vector<Agent::AgentSkillUse>& skills = printer.agent->results.back().skills;
    REQUIRE(carries_skill(skills));
    CHECK(skills.back().tools == std::vector<std::string>{"issue_report_open"});

    printer.say("Connect it");
    printer.pump();
    CHECK(offers(printer.agent->requests.back(), "issue_report_open"));
}

TEST_CASE("issue_report_open is never offered outside a printer chat's connection skill", "[printer-conversation][issue-report]")
{
    const Agent::ToolDefinition* tool = Agent::ToolRegistry::instance().find("issue_report_open");
    REQUIRE(tool != nullptr);
    // Not the project chat's, and not an MCP client's.
    CHECK(tool->exposure == Agent::ToolExposure::Printer);
    // Not on any session's own list.
    for (const std::vector<std::string>& list : {PrinterConversation::session_tools(), PrinterConversation::settings_tools()})
        CHECK(std::find(list.begin(), list.end(), "issue_report_open") == list.end());

    SECTION("a project chat") {
        PrinterHost project(ConversationMode::Add, false);
        project.page("skills", connect_bambu_skills());
        project.say("My printer will not connect, please report it");
        project.pump();
        const auto reply = std::find_if(project.agent->requests.begin(), project.agent->requests.end(), [](const Agent::AgentRequest& request) {
            return request.purpose == Agent::AgentRequest::Purpose::Reply;
        });
        REQUIRE(reply != project.agent->requests.end());
        const std::vector<std::string> names = offered(*reply);
        CHECK(std::find(names.begin(), names.end(), "workspace_inspect") != names.end());
        CHECK(std::find(names.begin(), names.end(), "issue_report_open") == names.end());
    }
    // Kenneth, 2026-11-06: offered for all printer connections.
    SECTION("a printer chat about a print host, whose skill brings it too") {
        PrinterHost host(ConversationMode::Connect);
        json skills = connect_bambu_skills();
        skills["skills"].push_back(json{{"name", "connect-print-host"}, {"trigger", "connect_print_host"}, {"text", skill_text("connect-print-host")}});
        host.page("skills", skills);
        host.say("Connect it");
        host.pump();
        REQUIRE_FALSE(host.agent->requests.empty());
        CHECK(offers(host.agent->requests.back(), "issue_report_open"));
    }
}

TEST_CASE("issue_report_open opens a filled-in GitHub page with nothing that identifies the printer",
          "[printer-conversation][issue-report]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   printer = bambu_printer();
    printer.device_id      = "01P00A3B";
    backend.saved           = {printer};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const json output = ran(conversation, "issue_report_open",
                            json{{"title", "A1 mini fails & says code -1 #2"},
                                 {"body", "Tried 01P00A3B at 192.168.1.30 twice.\nThey typed 12345678, then 3f9a2b7c.\n"
                                          "They said: café wifi, 50% sure + more"}});
    CHECK(output.at("state") == "opened");
    CHECK(output.at("shortened") == false);
    // Opened, not filed: the person submits it.
    CHECK_THAT(output.at("message").get<std::string>(), ContainsSubstring("Nothing has been filed"));

    REQUIRE(panel.opened.size() == 1);
    const std::string& url = panel.opened.front();
    CAPTURE(url);
    const std::string page = "https://github.com/TheSpaghettiDetective/JusPrin/issues/new?title=";
    CHECK(url.rfind(page, 0) == 0);
    // Two parameters, each encoded whole: an ampersand, a hash, a plus, a
    // percent sign, a line break or a letter outside ASCII never ends one
    // early or changes what it says.
    CHECK(std::count(url.begin(), url.end(), '&') == 1);
    CHECK(url.find_first_of(" #+\n") == std::string::npos);
    CHECK_THAT(url, ContainsSubstring("?title=A1%20mini%20fails%20%26%20says%20code%20-1%20%232&body="));
    CHECK_THAT(url, ContainsSubstring(encoded("They said: café wifi, 50% sure + more")));
    CHECK_THAT(url, ContainsSubstring("caf%C3%A9"));
    // The app's own facts close the body.
    const std::string footer = encoded("\n\n---\nApp version: 02.06.00.51\nConnection: Bambu Lab, LAN mode\nPrinter: Bambu Lab A1 mini");
    REQUIRE(url.size() > footer.size());
    CHECK(url.substr(url.size() - footer.size()) == footer);
    // Neither the serial, nor the address, nor a code the person typed.
    for (const char* secret : {"01P00A3B", "192.168.1.30", "12345678", "3f9a2b7c"}) {
        CAPTURE(secret);
        CHECK(url.find(secret) == std::string::npos);
    }
    CHECK_THAT(url, ContainsSubstring(encoded("Tried [hidden] at [hidden] twice.")));
}

TEST_CASE("an issue link stays under the cap: the body is cut at a whole character, and the app's footer stays",
          "[printer-conversation][issue-report]")
{
    const std::string footer = "\n\n---\nApp version: 02.06.00.51";
    const IssueLink   small  = issue_link("Title", "Short", footer);
    CHECK_FALSE(small.shortened);
    CHECK(small.url == "https://github.com/TheSpaghettiDetective/JusPrin/issues/new?title=Title&body=Short" + encoded(footer));

    // Every letter takes six bytes in the link.
    std::string body;
    for (int i = 0; i < 2000; ++i)
        body += "é";
    const IssueLink cut = issue_link("Title", body, footer);
    CHECK(cut.shortened);
    CHECK(cut.url.size() <= kIssueLinkLimit);
    CHECK(cut.url.size() > kIssueLinkLimit - 6);
    const std::string end = encoded("\n\n(Cut short to fit the link.)") + encoded(footer);
    REQUIRE(cut.url.size() > end.size());
    CHECK(cut.url.substr(cut.url.size() - end.size()) == end);
    const std::string start = "https://github.com/TheSpaghettiDetective/JusPrin/issues/new?title=Title&body=";
    const std::string kept  = cut.url.substr(start.size(), cut.url.size() - start.size() - end.size());
    REQUIRE_FALSE(kept.empty());
    CHECK(kept.size() % 6 == 0);
    for (std::size_t at = 0; at < kept.size(); at += 6)
        CHECK(kept.substr(at, 6) == "%C3%A9");

    SECTION("through the tool, which says it cut the body") {
        FakeBackend    backend;
        RecordingPanel panel;
        backend.saved           = {bambu_printer()};
        backend.connection_info = bambu_on_network();
        PrinterConversation conversation(backend, panel);
        conversation.start(ConversationMode::Connect, "Lab Printer");
        std::string longest;
        for (int i = 0; i < 4096; ++i)
            longest += "é";
        const json output = ran(conversation, "issue_report_open", json{{"title", "Too long"}, {"body", longest}});
        CHECK(output.at("shortened") == true);
        CHECK_THAT(output.at("message").get<std::string>(), ContainsSubstring("its end was cut"));
        REQUIRE(panel.opened.size() == 1);
        CHECK(panel.opened.front().size() <= kIssueLinkLimit);
    }
}

TEST_CASE("issue_report_open says when no browser opened, and names a print host's program",
          "[printer-conversation][issue-report]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {bambu_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    const json arguments{{"title", "Stuck"}, {"body", "It does not connect."}};

    SECTION("no browser") {
        conversation.start(ConversationMode::Connect, "Lab Printer");
        panel.browser_opens = false;
        CHECK(refused(conversation, "issue_report_open", arguments) == "failed");
    }
    SECTION("a print host's issue names its program, and not its address") {
        backend.saved                     = {lab_printer()};
        backend.connection_info           = PrinterConnectionInfo{"host", "unknown"};
        backend.connection_info.address   = "http://192.168.1.40:7125";
        backend.connection_info.host_type = "moonraker";
        conversation.start(ConversationMode::Change, "Lab Printer");
        ran(conversation, "issue_report_open", json{{"title", "Stuck"}, {"body", "It does not answer at 192.168.1.40."}});
        REQUIRE(panel.opened.size() == 1);
        CHECK_THAT(panel.opened.front(), ContainsSubstring(encoded("\nConnection: Moonraker\n")));
        CHECK_THAT(panel.opened.front(), !ContainsSubstring("192.168"));
    }
}

// The evidence rule: a state gets a line in the skill only after it was made
// to happen for real, so a new line fails here until the run it rests on is
// named. printer-connect-live-check is the untracked note of those runs;
// connection-skill-report the one this skill was built with.
TEST_CASE("the connection skill names only states the tools report, each seen for real, and leaves the model free to reason", "[printer-conversation][connection-skill]")
{
    const std::map<std::string, std::string> seen_for_real{
        {"listening", "P1S, 2026-11-01, printer-connect-live-check"},
        {"port_taken", "P1S with a script holding UDP 2021, 2026-11-01, printer-connect-live-check"},
        {"not_listening", "P1S with the port let go, 2026-11-01, printer-connect-live-check"},
        {"cloud_mode", "P1S in cloud mode, 2026-11-01, printer-connect-live-check"},
        {"linked_elsewhere", "P1S as a second saved printer, 2026-11-02, printer-connect-live-check"},
        {"failed", "P1S, plug-in code -1 with a wrong code and with the right code on a slow network, 2026-11-02, connection-skill-report"},
        {"unknown_device", "P1S before any search, 2026-11-01, printer-connect-live-check"},
        {"unavailable", "no plug-in in the data folder, 2026-11-02, connection-skill-report"},
        {"not_started", "no plug-in in the data folder, 2026-11-02, connection-skill-report"},
        {"not_configured", "P1S with no connection saved, 2026-11-02, connection-skill-report"},
    };

    // What the tools can report, from the registry itself.
    const Agent::ToolDefinition* status  = Agent::ToolRegistry::instance().find("printer_connection_status");
    const Agent::ToolDefinition* connect = Agent::ToolRegistry::instance().find("printer_bambu_connect");
    REQUIRE(status != nullptr);
    REQUIRE(connect != nullptr);
    const json& properties = status->output_schema.at("properties");
    std::set<std::string> states;
    for (const json* list : {&properties.at("state").at("enum"), &properties.at("search").at("properties").at("state").at("enum"),
                             &properties.at("seen").at("items").at("properties").at("reason").at("enum"),
                             &connect->output_schema.at("properties").at("state").at("enum")})
        for (const json& state : *list)
            states.insert(state.get<std::string>());
    states.insert("unknown_device"); // preflight_tool's refusal
    // Not states: the tools, and the result's fields, named as they are.
    // web_search is the provider's tool, offered beside the app's.
    std::set<std::string> names{"printer_connection_status", "printer_bambu_connect", "web_search", "issue_report_open"};
    for (const auto& [field, schema] : properties.items())
        names.insert(field);
    for (const auto& [field, schema] : properties.at("candidates").at("items").at("properties").items())
        names.insert(field);
    names.insert("detail");

    const std::string text = connect_bambu_text();
    std::istringstream lines(text);
    std::string line;
    std::set<std::string> named;
    while (std::getline(lines, line)) {
        std::vector<std::string> spans;
        for (std::size_t open = line.find('`'); open != std::string::npos;) {
            const std::size_t close = line.find('`', open + 1);
            REQUIRE(close != std::string::npos);
            spans.push_back(line.substr(open + 1, close - open - 1));
            open = line.find('`', close + 1);
        }
        CAPTURE(line);
        for (const std::string& span : spans) {
            CAPTURE(span);
            CHECK((states.count(span) == 1 || names.count(span) == 1));
            if (states.count(span) == 1) {
                named.insert(span);
                CHECK(seen_for_real.count(span) == 1);
            }
        }
    }
    // Every state with a run behind it is explained, so nothing rests on the
    // list alone.
    for (const auto& [state, run] : seen_for_real) {
        CAPTURE(state, run);
        CHECK(named.count(state) == 1);
    }
    // No menu wording stated as fact, and nothing that forbids working out why:
    // "add no cause" kept the model repeating a false reason to someone whose
    // code was right (P1S, 2026-11-02).
    for (const char* banned : {" > ", "→", "Settings >", "gear icon", "WLAN", "add no cause", "never say which"})
        CHECK_THAT(text, !ContainsSubstring(banned));
}

// The same rule for the print host skill. Its states come from
// --print-host-check against real hosts in Docker (connection-skill-report)
// and the live Moonraker chats of 2026-11-04; an address, a scheme or a file
// in backquotes is an example, not a state, and so are the two Moonraker
// settings it names from Mainsail's FAQ.
TEST_CASE("the print host skill names only states seen from a real host", "[printer-conversation][connection-skill]")
{
    const std::map<std::string, std::string> seen_for_real{
        {"not_configured", "a saved printer before any attempt, 2026-11-02, --print-host-check"},
        {"unknown", "a host saved by a verified attempt, in a new session, 2026-11-02, --print-host-check"},
        {"failed", "Moonraker, OctoPrint and Mainsail in Docker, 2026-11-02, --print-host-check"},
        {"verified", "Moonraker in Docker and on a Raspberry Pi, OctoPrint in Docker, 2026-11-04, --print-host-check"},
    };
    const std::set<std::string> names{"printer_host_connect", "printer_connection_status", "issue_report_open", "web_search",
                                      "trusted_clients", "[authorization]"};
    {
        const std::string text = skill_text("connect-print-host");
        std::set<std::string> named;
        for (std::size_t open = text.find('`'); open != std::string::npos;) {
            const std::size_t close = text.find('`', open + 1);
            REQUIRE(close != std::string::npos);
            const std::string span = text.substr(open + 1, close - open - 1);
            open                   = text.find('`', close + 1);
            CAPTURE(span);
            if (span.find_first_of(":./") != std::string::npos)
                continue;
            CHECK((seen_for_real.count(span) == 1 || names.count(span) == 1));
            named.insert(span);
        }
        for (const auto& [state, run] : seen_for_real) {
            CAPTURE(state, run);
            CHECK(named.count(state) == 1);
        }
        for (const char* banned : {" > ", "→", "Settings >", "gear icon", "add no cause", "never say which"})
            CHECK_THAT(text, !ContainsSubstring(banned));
    }
}

// -- A connection report --------------------------------------------------------

namespace {

// A Bambu Lab printer's connect, refused by the plug-in, `times` times, with
// the code in the form.
void fail_connecting(PrinterHost& printer, int times, const std::string& code)
{
    printer.backend.connection_info = bambu_on_network();
    printer.backend.connection_error = "Check the access code.";
    for (int i = 0; i < times; ++i) {
        printer.agent->call = Agent::ToolRequest{"printer_bambu_connect", json{{"deviceId", "01P00A3B"}}.dump()};
        printer.say("Connect it, please");
        printer.pump();
        const std::vector<Agent::ToolActivity> activities = printer.activities();
        REQUIRE_FALSE(activities.empty());
        REQUIRE(activities.back().tool == "printer_bambu_connect");
        REQUIRE(activities.back().state == Agent::ToolState::Running);
        printer.page("printer_action", {{"action", "connect"}, {"actionId", activities.back().action_id}, {"credential", code}});
        printer.pump();
    }
}

std::vector<json> report_cards(const PrinterConversation& conversation)
{
    std::vector<json> cards;
    const json        state = conversation.state_json();
    for (const json& block : state.at("blocks"))
        if (block.value("kind", "") == "report")
            cards.push_back(block);
    return cards;
}

} // namespace

TEST_CASE("the second failed connection asks whether to send a report, once", "[printer-conversation][connection-report]")
{
    PrinterHost printer(ConversationMode::Connect);
    printer.backend.saved = {bambu_printer()};

    fail_connecting(printer, 1, "11112222");
    CHECK(report_cards(printer.conversation).empty());

    fail_connecting(printer, 1, "11112222");
    REQUIRE(report_cards(printer.conversation).size() == 1);
    const json card = report_cards(printer.conversation).front();
    const json report = json::parse(card.at("report").get<std::string>());
    CHECK(report.at("trigger") == "failed_twice");
    CHECK(report.at("connectionType") == "bambu");
    CHECK(report.at("chat").at("tools").size() == 2);
    CHECK(report.at("chat").at("tools").front().at("tool") == "printer_bambu_connect");
    CHECK(report.at("chat").at("tools").front().at("output").at("state") == "failed");
    // The call the card is about is still open; the report holds its failure.
    CHECK(report.at("chat").at("tools").back().at("output").is_null());
    CHECK_THAT(report.at("failure").get<std::string>(), ContainsSubstring("Check the access code."));
    CHECK_FALSE(report.at("chat").at("turns").empty());
    // Nothing is kept or sent by asking.
    CHECK(printer.backend.kept_reports.empty());

    fail_connecting(printer, 1, "11112222");
    CHECK(report_cards(printer.conversation).size() == 1);
}

TEST_CASE("a connection the printer failed afterwards counts the same", "[printer-conversation][connection-report]")
{
    PrinterHost printer(ConversationMode::Connect);
    printer.backend.saved = {bambu_printer()};
    fail_connecting(printer, 1, "11112222");
    printer.backend.connection_error.clear();
    printer.backend.connection_info = bambu_on_network();
    printer.agent->call = Agent::ToolRequest{"printer_bambu_connect", json{{"deviceId", "01P00A3B"}}.dump()};
    printer.say("Again");
    printer.pump();
    printer.page("printer_action", {{"action", "connect"}, {"actionId", printer.activities().back().action_id}, {"credential", "11112222"}});
    printer.pump();
    CHECK(report_cards(printer.conversation).empty());
    printer.backend.connection_info.state   = "failed";
    printer.backend.connection_info.message = "The printer did not answer (code -1).";
    printer.conversation.tick(std::chrono::steady_clock::now());
    printer.pump();
    REQUIRE(report_cards(printer.conversation).size() == 1);
    CHECK_THAT(report_cards(printer.conversation).front().at("report").get<std::string>(), ContainsSubstring("The printer did not answer"));
}

TEST_CASE("a print host's second failed connection asks too, and the report names its kind", "[printer-conversation][connection-report]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved            = {lab_printer()};
    backend.connection_info  = PrinterConnectionInfo{"host", "not_configured"};
    backend.connection_error = "Refused.";
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");
    REQUIRE_FALSE(preflight(conversation, "printer_host_connect", json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}}));
    CHECK(host_connect(conversation, "a-1").error->message == "Refused.");
    CHECK(report_cards(conversation).empty());
    // The model reads that the card is up in the refusal itself.
    CHECK_THAT(host_connect(conversation, "a-2").error->message, ContainsSubstring("send a report of this chat"));
    const std::vector<json> cards = report_cards(conversation);
    REQUIRE(cards.size() == 1);
    CHECK(json::parse(cards.front().at("report").get<std::string>()).at("connectionType") == "moonraker");
}

TEST_CASE("a report never holds the access code or an API key", "[printer-conversation][connection-report]")
{
    const std::string form_code = "F0rmCode9";
    const std::string typed_code = "87654321";
    const std::string typed_key = "sk-ant-abc_DEF-123456";
    PrinterHost printer(ConversationMode::Connect);
    printer.backend.saved = {bambu_printer()};
    printer.say("My code is " + typed_code + " and my key is " + typed_key);
    printer.pump();
    fail_connecting(printer, 2, form_code);

    REQUIRE(report_cards(printer.conversation).size() == 1);
    const std::string held = report_cards(printer.conversation).front().at("report").get<std::string>();
    CHECK_THAT(held, ContainsSubstring("My code is [hidden]"));
    CHECK_THAT(held, ContainsSubstring("my key is [hidden]"));
    CHECK_THAT(held, !ContainsSubstring(form_code));
    CHECK_THAT(held, !ContainsSubstring(typed_code));
    CHECK_THAT(held, !ContainsSubstring(typed_key));
    // A printer's serial goes too, and what the person needs to recognise stays.
    CHECK_THAT(held, !ContainsSubstring("01P00A3B"));
    CHECK_THAT(held, ContainsSubstring("Check the access code."));

    printer.page("printer_action", {{"action", "report_send"}, {"blockId", report_cards(printer.conversation).front().at("id")}});
    REQUIRE(printer.backend.kept_reports.size() == 1);
    CHECK_THAT(printer.backend.kept_reports.front(), !ContainsSubstring(form_code));
    CHECK_THAT(printer.backend.kept_reports.front(), !ContainsSubstring(typed_code));
    CHECK_THAT(printer.backend.kept_reports.front(), !ContainsSubstring(typed_key));
}

TEST_CASE("the person answers the report card, once", "[printer-conversation][connection-report]")
{
    PrinterHost printer(ConversationMode::Connect);
    printer.backend.saved = {bambu_printer()};
    fail_connecting(printer, 2, "11112222");
    REQUIRE(report_cards(printer.conversation).size() == 1);
    const std::string id = report_cards(printer.conversation).front().at("id");
    const auto notes = [&printer] {
        const Agent::ProjectStateDocument& document = printer.persistence.document();
        return document.messages(document.active_conversation_id()).back().text;
    };

    SECTION("Not now sends nothing, and the model is told") {
        printer.page("printer_action", {{"action", "report_decline"}, {"blockId", id}});
        CHECK(printer.backend.kept_reports.empty());
        CHECK(report_cards(printer.conversation).front().at("answer") == "declined");
        CHECK_THAT(notes(), ContainsSubstring("chose not to send"));
        // An answered card cannot be answered again.
        printer.page("printer_action", {{"action", "report_send"}, {"blockId", id}});
        CHECK(printer.backend.kept_reports.empty());
    }
    SECTION("Send report keeps it, and nothing claims it reached anyone") {
        printer.page("printer_action", {{"action", "report_send"}, {"blockId", id}});
        REQUIRE(printer.backend.kept_reports.size() == 1);
        CHECK(printer.backend.kept_reports.front() == report_cards(printer.conversation).front().at("report").get<std::string>());
        CHECK(report_cards(printer.conversation).front().at("answer") == "kept");
        CHECK(report_cards(printer.conversation).front().at("path") == printer.backend.keep_report_path);
        CHECK_THAT(notes(), ContainsSubstring("has not reached the developers"));
    }
    SECTION("a report that cannot be saved says so") {
        printer.backend.keep_report_path.clear();
        printer.page("printer_action", {{"action", "report_send"}, {"blockId", id}});
        CHECK(report_cards(printer.conversation).front().at("answer") == "failed");
        CHECK_THAT(notes(), ContainsSubstring("could not be saved"));
    }
}
