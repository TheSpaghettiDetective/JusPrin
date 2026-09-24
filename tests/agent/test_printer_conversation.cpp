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

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <set>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::PrinterSetup;
using Catch::Matchers::ContainsSubstring;
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

    const std::vector<CatalogPrinter>& catalog() const override { return catalogue; }
    std::vector<DiscoveredPrinter>     network_printers() const override { return network; }
    std::vector<SavedPrinter>          saved_printers() const override { return saved; }

    // Saves the printer into `saved`, as the Orca backend does.
    std::string add_printer(const AddPrinterRequest& request, SavedPrinter& result) override
    {
        added.push_back(request);
        if (!refusal.empty())
            return refusal;
        result.name     = request.name;
        result.model    = request.name;
        result.model_id = request.model_id;
        result.nozzle   = std::stod(request.variant);
        saved.push_back(result);
        return {};
    }
    // Saves the change into `saved`, as the Orca backend does.
    std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& result) override
    {
        changed.push_back(request);
        if (!refusal.empty())
            return refusal;
        for (SavedPrinter& printer : saved)
            if (printer.name == request.name) {
                if (request.nozzle)
                    printer.nozzle = *request.nozzle;
                if (request.spools)
                    printer.spools = *request.spools;
                result = printer;
            }
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
    std::string connect_printer(const std::string& name, const std::string& device, const std::string& code) override
    {
        connects.push_back({name, device, code});
        return connection_error;
    }
    std::string connect_host(const std::string& name, const std::string& type, const std::string& address,
                             const std::string& key) override
    {
        connects.push_back({name, type, address, key});
        return connection_error;
    }
    ManualPrinterResult run_manual_setup() override { ++manual_setups; return manual_result; }
    void open_printer_settings(const std::string& name) override { settings_opened.push_back(name); }
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
    void session_changed() override { ++states; }
    void profile_changed() override { ++profiles; }
    void close_panel() override { ++closes; }
    std::vector<std::string> added_printers; // what each refresh named, empty for none
    void printers_changed(const std::string& added) override
    {
        ++refreshes;
        added_printers.push_back(added);
    }
    std::map<std::string, std::string> credentials; // by action id, as the card would hand them over
    std::optional<std::string> take_credential(const std::string& action_id) override
    {
        const auto found = credentials.find(action_id);
        if (found == credentials.end())
            return std::nullopt;
        std::string credential = found->second;
        credentials.erase(found);
        return credential;
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

TEST_CASE("every session offers every printer tool and nothing of the project", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    std::vector<std::string> printer_tools;
    for (const Agent::ToolDefinition& definition : Agent::ToolRegistry::instance().exposed(Agent::ToolExposure::Printer))
        printer_tools.push_back(definition.name);

    for (const ConversationMode mode : {ConversationMode::Add, ConversationMode::Change, ConversationMode::Connect}) {
        PrinterConversation conversation(backend, panel);
        conversation.start(mode, "Lab Printer");
        const Agent::AgentSessionProfile profile = conversation.profile();
        CHECK(profile.tool_names == printer_tools);
        CHECK_FALSE(profile.include_workspace);
        CHECK(profile.notes_in_context);
        CHECK(profile.reply_cancels_pending_card);
    }
}

TEST_CASE("only printer_connect asks for a card; the rest run on the person's yes", "[printer-conversation]")
{
    const auto& registry = Agent::ToolRegistry::instance();
    for (const std::string& name : PrinterConversation::session_tools()) {
        INFO(name);
        const Agent::ToolDefinition& definition = *registry.find(name);
        CHECK(registry.requires_approval(definition, "{}") == (name == "printer_connect"));
        // None joins a plan.
        CHECK_FALSE(definition.input_schema.at("properties").contains("planId"));
    }
    // The exemption is the printer panel's alone.
    for (const Agent::ToolDefinition& definition : registry.exposed(Agent::ToolExposure::InApp))
        CHECK_FALSE(definition.confirmed_in_conversation);
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

TEST_CASE("a printer this app has not saved is one to add", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(conversation.mode() == ConversationMode::Add);
    CHECK(conversation.printer_name().empty());
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
    CHECK(result.error->message == "JusPrin supports 0.25, 0.4, 0.6 and 0.8 mm nozzles for Prusa MK3S, but not 0.3 mm. Ask the person to check the nozzle marking or packaging; do not substitute a size.");
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

    const json output = ran(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}, {"nozzle", 0.6}});
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

    // A second yes is not a second printer.
    CHECK(refused(conversation, "printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}) == "already_added");
    CHECK(backend.added.size() == 1);
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
}

TEST_CASE("printer_change saves what changed and returns the printer as it now is", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const json spools_now = json::array({json{{"name", "PLA Matte"}, {"material", "PLA"}, {"colour", "#5f7d4f"}},
                                         json{{"name", "PETG"}, {"material", "PETG"}, {"colour", "#204080"}}});
    const json output = ran(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}, {"spools", spools_now}});
    // The spools said are the spools there: only the nozzle is changed.
    REQUIRE(backend.changed.size() == 1);
    CHECK(backend.changed[0].nozzle == 0.6);
    CHECK_FALSE(backend.changed[0].spools.has_value());
    CHECK(output.at("changed") == json::array({json{{"field", "nozzle"}, {"before", 0.4}, {"after", 0.6}}}));
    CHECK(output.at("printer").at("nozzle") == 0.6);
    CHECK(conversation.state_json().at("context").at("printer").at("nozzle") == 0.6);
    CHECK(panel.refreshes == 1);

    // Putting it back is the same tool.
    ran(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.4}});
    CHECK(backend.saved[0].nozzle == 0.4);
}

TEST_CASE("printer_change refuses what it cannot change, before saving anything", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    CHECK(refused(conversation, "printer_change", json{{"printerName", "Garage"}, {"nozzle", 0.6}}) == "unknown_printer");
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}}) == "nothing_to_change");
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.4}}) == "nothing_to_change");
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.3}}) == "unknown_nozzle");
    CHECK(backend.changed.empty());
    backend.refusal = "The profile is read-only.";
    CHECK(refused(conversation, "printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}) == "change_failed");
}

// -- Connecting ---------------------------------------------------------------

namespace {

PrinterConnectionInfo bambu_on_network()
{
    PrinterConnectionInfo info;
    info.provider   = "bambu";
    info.state      = "not_configured";
    info.candidates = {{"01P00A3B", "Workshop", "192.168.1.30", true}, {"CLOUD01", "Office", "", false}};
    return info;
}

} // namespace

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
    // Before a printer is saved there is nothing to connect.
    conversation.start(ConversationMode::Add);
    CHECK(refused(conversation, "printer_connection_status", json::object()) == "no_printer");
}

TEST_CASE("printer_connect is checked before its card, and titles it", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    auto preflight = [&](const json& arguments) {
        Agent::ToolActivity activity = call("printer_connect", arguments);
        return std::make_pair(conversation.preflight_tool(Agent::ToolHandler::PrinterConnect, activity), activity);
    };
    // A printer reached through a Bambu account is not offered.
    CHECK(preflight(json{{"deviceId", "CLOUD01"}}).first->code == "unknown_device");
    const auto [problem, activity] = preflight(json{{"deviceId", "01P00A3B"}});
    CHECK_FALSE(problem.has_value());
    CHECK(activity.title == "Connect to Workshop");
    CHECK(json::parse(activity.arguments_json).at("provider") == "bambu");
    CHECK(json::parse(activity.arguments_json).at("printerName") == "Lab Printer");
    // Named as the person sees it, it is the same printer; the card and the
    // connection use its id.
    const auto [by_name, named] = preflight(json{{"deviceId", "Workshop"}});
    CHECK_FALSE(by_name.has_value());
    CHECK(json::parse(named.arguments_json).at("deviceId") == "01P00A3B");
    CHECK_THAT(preflight(json{{"deviceId", "Nope"}}).first->message, ContainsSubstring("01P00A3B (Workshop)"));

    backend.connection_info = PrinterConnectionInfo{"host", "not_configured"};
    CHECK(preflight(json{{"deviceId", "01P00A3B"}}).first->code == "address_needed");
    CHECK(preflight(json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}}).second.title ==
          "Connect to 192.168.1.42");
    backend.connection_info = PrinterConnectionInfo{"unavailable", "unavailable", "Install the network plugin."};
    CHECK(preflight(json{{"deviceId", "01P00A3B"}}).first->message == "Install the network plugin.");
    // It connects the printer the conversation is about, and nothing before one is saved.
    conversation.start(ConversationMode::Add);
    CHECK(preflight(json{{"deviceId", "01P00A3B"}}).first->code == "no_printer");
}

TEST_CASE("printer_connect takes a found printer or an address, never part of one", "[printer-conversation]")
{
    const auto&                  registry   = Agent::ToolRegistry::instance();
    const Agent::ToolDefinition* definition = registry.find("printer_connect");
    REQUIRE(definition != nullptr);
    const auto checked = [&](const json& arguments) { return registry.validate_call(*definition, arguments.dump()); };

    CHECK(checked(json{{"deviceId", "01P00A3B"}}).valid());
    CHECK(checked(json{{"hostType", "moonraker"}, {"address", "192.168.1.42"}}).valid());
    // What the live model sent after "go ahead", before anyone gave an
    // address: refused here, so no card is drawn and the model is told to ask.
    const auto without_address = checked(json{{"hostType", "moonraker"}});
    REQUIRE_FALSE(without_address.valid());
    CHECK_THAT(without_address.error->message, ContainsSubstring("ask the person"));
    CHECK_FALSE(checked(json{{"address", ""}}).valid());
    CHECK_FALSE(checked(json{{"hostType", "moonraker"}, {"address", ""}}).valid());
    CHECK_FALSE(checked(json{{"address", "192.168.1.42"}}).valid());
    CHECK_FALSE(checked(json::object()).valid());
    CHECK_FALSE(checked(json{{"deviceId", ""}}).valid());
    CHECK_FALSE(checked(json{{"deviceId", "01P00A3B"}, {"hostType", "moonraker"}, {"address", "192.168.1.42"}}).valid());
}

TEST_CASE("a host connection's outcome is the tool's own result", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = PrinterConnectionInfo{"host", "verified"};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    const json arguments{{"printerName", "Lab Printer"}, {"hostType", "moonraker"}, {"address", "192.168.1.42"}, {"provider", "host"}};
    Agent::ToolActivity activity = call("printer_connect", arguments);
    activity.action_id           = "a-1";
    panel.credentials["a-1"]     = "key123";
    auto result = conversation.execute_tool(Agent::ToolHandler::PrinterConnect, activity);
    CHECK(json::parse(result.result_json) == json{{"state", "verified"}, {"message", ""}});
    CHECK(backend.connects.back() == std::vector<std::string>{"Lab Printer", "moonraker", "192.168.1.42", "key123"});
    CHECK(panel.credentials.empty());

    backend.connection_error = "Use an HTTP or HTTPS address.";
    result = conversation.execute_tool(Agent::ToolHandler::PrinterConnect, activity);
    CHECK(json::parse(result.result_json) == json{{"state", "failed"}, {"message", "Use an HTTP or HTTPS address."}});
}

TEST_CASE("a Bambu Lab connection reports once it settles: one note, one turn", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved           = {lab_printer()};
    backend.connection_info = bambu_on_network();
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Connect, "Lab Printer");

    Agent::ToolActivity activity = call("printer_connect", json{{"printerName", "Lab Printer"}, {"deviceId", "01P00A3B"}, {"provider", "bambu"}});
    activity.action_id       = "a-1";
    panel.credentials["a-1"] = "12345678";
    const auto result = conversation.execute_tool(Agent::ToolHandler::PrinterConnect, activity);
    CHECK(json::parse(result.result_json).at("state") == "connecting");
    CHECK(backend.connects.back() == std::vector<std::string>{"Lab Printer", "01P00A3B", "12345678"});

    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::now();
    backend.connection_info.state = "connecting";
    conversation.tick(start);
    CHECK(panel.notes.empty());
    // A second connection waits for this one.
    Agent::ToolActivity again = call("printer_connect", json{{"deviceId", "01P00A3B"}});
    CHECK(conversation.preflight_tool(Agent::ToolHandler::PrinterConnect, again)->code == "connection_in_progress");

    backend.connection_info.state   = "failed";
    backend.connection_info.message = "The printer did not respond.";
    conversation.tick(start + 500ms); // not yet a second since it last looked
    CHECK(panel.notes.empty());
    conversation.tick(start + 1100ms);
    REQUIRE(panel.notes == std::vector<std::string>{"Connection to Lab Printer failed: The printer did not respond."});
    check_is_a_statement(panel.notes.front());
    CHECK(panel.turns == 1);
    conversation.tick(start + 5s);
    CHECK(panel.notes.size() == 1);
    CHECK(panel.turns == 1);
}

// -- OrcaSlicer's own screens, and closing -------------------------------------

TEST_CASE("the full printer list reports what it added, as a tool and from the menu", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    // Closed without adding anything.
    CHECK(ran(conversation, "printer_manual_setup", json::object()) == json{{"applied", false}, {"added", json::array()}});
    conversation.handle_page_message("printer_action", json{{"action", "manual_setup"}});
    CHECK(panel.notes.empty());
    CHECK(panel.turns == 0);

    backend.manual_result = ManualPrinterResult{true, {lab_printer()}};
    CHECK(ran(conversation, "printer_manual_setup", json::object()).at("added") == json::array({"Lab Printer"}));
    CHECK(conversation.printer_name() == "Lab Printer");
    conversation.handle_page_message("printer_action", json{{"action", "manual_setup"}});
    REQUIRE(panel.notes == std::vector<std::string>{"The person added Lab Printer from OrcaSlicer's printer list."});
    check_is_a_statement(panel.notes.front());
    CHECK(panel.turns == 1);
    CHECK(backend.manual_setups == 4);
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
    CHECK(ran(conversation, "printer_manual_connection", json::object()) == json{{"state", "opened"}});
    CHECK(backend.settings_opened == std::vector<std::string>{"Lab Printer", "Lab Printer"});
}

TEST_CASE("printer_setup_finish closes the panel", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    CHECK(ran(conversation, "printer_setup_finish", json::object()) == json{{"state", "closed"}});
    CHECK(panel.closes == 1);
}

TEST_CASE("a printer conversation writes nothing into the project", "[printer-conversation]")
{
    Workspace::FakeWorkspace workspace;
    const std::filesystem::path project = workspace.auxiliary_data_dir();

    Agent::ProjectPersistence::Config config;
    config.in_memory     = true;
    config.recovery_root = (project / "recovery").string();
    Agent::ProjectPersistence persistence(workspace, std::move(config));
    persistence.attach();

    REQUIRE(persistence.document().has_identity());
    persistence.document().append_message("c-1", Agent::ConversationMessage{"m-1"}, persistence.timestamp());
    persistence.commit();
    persistence.flush();
    REQUIRE(persistence.write_attachment_blob("attachments/a-1/photo.png", "bytes"));

    CHECK(persistence.read_attachment_blob("attachments/a-1/photo.png") == "bytes");
    CHECK_FALSE(std::filesystem::exists(project / "JusPrin"));
    CHECK_FALSE(std::filesystem::exists(project / "recovery"));
    CHECK(persistence.recovery_dir().empty());
}

// -- Inside a real host --------------------------------------------------------

namespace {

// Answers each turn with text, or with the one tool call it was given, and
// records every request and tool result the host hands it.
class ScriptedAgent final : public Agent::IAgentService
{
public:
    std::optional<Agent::ToolRequest>   call;
    std::vector<Agent::AgentRequest>    requests;
    std::vector<Agent::AgentToolResult> results;

    bool ready() const override { return true; }
    bool busy() const override { return m_active; }
    bool start(const Agent::AgentRequest& request) override
    {
        requests.push_back(request);
        m_active = true;
        if (request.purpose == Agent::AgentRequest::Purpose::ConversationTitle) {
            m_events.push_back(Agent::AgentEvent::delta("Printer"));
            m_events.push_back(Agent::AgentEvent::completed());
        } else if (call) {
            m_events.push_back(Agent::AgentEvent::tool_call({"call-" + std::to_string(requests.size()), *call, true}));
            call.reset();
        } else {
            m_events.push_back(Agent::AgentEvent::delta("Understood."));
            m_events.push_back(Agent::AgentEvent::completed());
        }
        return true;
    }
    bool continue_after_tool(const Agent::AgentToolResult& result) override
    {
        results.push_back(result);
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
    bool                          m_active{false};
};

// What PrinterPanel does, without wx: the conversation's host is the
// AgentHost, through the same seams.
class HostedPanel final : public IConversationHost
{
public:
    Agent::AgentHost*    host{nullptr};
    PrinterConversation* conversation{nullptr};
    int                  closes{0};

    std::string post_note(const std::string& text) override { return host->post_note(text); }
    std::string post_opening(const std::string& text) override { return host->post_assistant_message(text); }
    void        start_turn() override { host->start_turn(); }
    void        session_changed() override { profile_changed(); }
    void        profile_changed() override
    {
        if (host != nullptr && conversation != nullptr)
            host->set_session_profile(conversation->profile());
    }
    void close_panel() override { ++closes; }
    void printers_changed(const std::string&) override {}
    std::optional<std::string> take_credential(const std::string& action_id) override
    {
        const std::optional<json> input = host->take_decision_input(action_id);
        return input ? std::optional<std::string>(input->value("credential", std::string())) : std::nullopt;
    }
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

    explicit PrinterHost(ConversationMode mode, bool printer_session = true)
        : host(workspace, persistence, Agent::AgentAvailability::Ready, false, make_agent())
    {
        persistence.attach();
        host.set_send([this](const std::string& envelope) { sent.push_back(envelope); });
        backend.saved = {lab_printer()};
        panel.host         = &host;
        panel.conversation = &conversation;
        if (printer_session) {
            host.set_session_state_provider([this] { return conversation.state_json(); });
            host.set_session_tool_executor([this](Agent::ToolHandler handler, const Agent::ToolActivity& activity) {
                return conversation.execute_tool(handler, activity);
            });
            host.set_session_tool_preflight([this](Agent::ToolHandler handler, Agent::ToolActivity& activity) {
                return conversation.preflight_tool(handler, activity);
            });
            host.set_session_tool_output([this](const Agent::ToolActivity& activity) { return conversation.tool_output(activity); });
            conversation.start(mode, "Lab Printer");
            // What the page does once it has the session's facts.
            conversation.handle_page_message("printer_instructions", json{{"text", "You are the printer assistant."}});
        }
        page("hello", {{"protocolVersions", {Agent::Protocol::kVersion}}, {"capabilities", {"streaming"}}});
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

TEST_CASE("printer_add and printer_change run on the person's yes, with no card", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Change);
    harness.agent->call = Agent::ToolRequest{"printer_change", json{{"printerName", "Lab Printer"}, {"nozzle", 0.6}}.dump()};
    harness.say("yes, set 0.6 mm");
    harness.pump();

    auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    CHECK_FALSE(activities.front().requires_approval);
    CHECK(activities.front().state == Agent::ToolState::Succeeded);
    CHECK(harness.backend.saved.front().nozzle == 0.6);
    REQUIRE(harness.agent->results.size() == 1);
    const json output = json::parse(harness.agent->results.front().output_json);
    CHECK(output.at("changed")[0].at("after") == 0.6);
    CHECK_FALSE(output.contains("workspace"));
    CHECK_FALSE(harness.agent->requests.front().session.include_workspace);

    harness.agent->call = Agent::ToolRequest{"printer_add", json{{"catalogId", "Prusa/Prusa MK3S"}}.dump()};
    harness.say("yes, add it");
    harness.pump();
    activities = harness.activities();
    REQUIRE(activities.size() == 2);
    CHECK_FALSE(activities.back().requires_approval);
    CHECK(harness.backend.added.size() == 1);
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

// Drives printer_connect to its card in a Connect session about a Bambu Lab
// printer on the network.
Agent::ToolActivity propose_connect(PrinterHost& harness)
{
    harness.backend.connection_info = bambu_on_network();
    harness.agent->call = Agent::ToolRequest{"printer_connect", json{{"deviceId", "01P00A3B"}}.dump()};
    harness.say("yes, connect it");
    harness.pump();
    const auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    REQUIRE(activities.front().requires_approval);
    REQUIRE(activities.front().state == Agent::ToolState::Pending);
    return activities.front();
}

} // namespace

TEST_CASE("the credential reaches the printer and nothing else", "[printer-conversation][host]")
{
    const std::string secret = "S3cretCode";
    PrinterHost harness(ConversationMode::Connect);
    const Agent::ToolActivity card = propose_connect(harness);
    CHECK(card.title == "Connect to Workshop");

    harness.page("tool_decision", {{"actionId", card.action_id}, {"decision", "approve"}, {"input", {{"credential", secret}}}});
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
        for (const Agent::AgentConversationContext& entry : request.conversation)
            CHECK_THAT(entry.text, !ContainsSubstring(secret));
    for (const Agent::AgentToolResult& result : harness.agent->results)
        CHECK_THAT(result.output_json, !ContainsSubstring(secret));
    CHECK_THAT(harness.persistence.document().dump(), !ContainsSubstring(secret));
    // Handed over once, and gone.
    CHECK_FALSE(harness.host.take_decision_input(card.action_id).has_value());
}

TEST_CASE("writing instead of connecting cancels the card, and the message is answered", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Connect);
    const Agent::ToolActivity card = propose_connect(harness);
    const std::size_t results = harness.agent->results.size();

    harness.say("where do I find the access code?");
    harness.pump();

    CHECK(harness.activities().front().state == Agent::ToolState::Rejected);
    CHECK(harness.backend.connects.empty());
    REQUIRE(harness.agent->results.size() == results + 1);
    CHECK(json::parse(harness.agent->results.back().output_json) == json{{"state", "cancelled"}});
    // The question gets its own turn once the card's is answered.
    CHECK(harness.agent->requests.back().user_text == "where do I find the access code?");
}

TEST_CASE("a printer session's notes reach the model as the app's words; a project's do not", "[printer-conversation][host]")
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
        const auto note = std::find_if(request.conversation.begin(), request.conversation.end(), [](const auto& entry) {
            return entry.text == "The person said Bambu Lab A1 mini is not their printer.";
        });
        REQUIRE(note != request.conversation.end());
        CHECK(note->role == "developer");
    }
    SECTION("project session") {
        PrinterHost harness(ConversationMode::Add, /*printer_session=*/false);
        harness.host.post_note("Printer changed to Bambu Lab A1 mini.");
        harness.say("hello");
        harness.pump();

        REQUIRE_FALSE(harness.agent->requests.empty());
        for (const auto& entry : harness.agent->requests.front().conversation)
            CHECK(entry.text != "Printer changed to Bambu Lab A1 mini.");
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
    std::vector<SavedPrinter>          saved_printers() const override { return saved; }
    std::string add_printer(const AddPrinterRequest&, SavedPrinter&) override { return {}; }
    std::string change_printer(const ChangePrinterRequest&, SavedPrinter&) override { return {}; }
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
json request_body(const Agent::AgentSessionProfile& profile, const std::string& user_text,
                  std::vector<Agent::AgentConversationContext> history = {})
{
    auto                transport = std::make_unique<CapturingTransport>();
    CapturingTransport* capture   = transport.get();
    Agent::OpenAIResponsesAgent agent({"key", "gpt-5.4-mini", "https://api.openai.com/v1/responses"}, std::move(transport));
    Agent::AgentRequest request;
    request.request_id   = "printer-1";
    request.user_text    = user_text;
    request.session      = profile;
    request.conversation = std::move(history);
    REQUIRE(agent.start(request));
    REQUIRE(capture->requests.size() == 1);
    return json::parse(capture->requests.front().body);
}

} // namespace

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

TEST_CASE("the model is offered every printer tool and no project", "[printer-conversation][openai]")
{
    CatalogBackend backend;
    backend.saved = {lab_printer()};
    RecordingPanel panel;

    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    adding.handle_page_message("printer_instructions", json{{"text", "You add printers."}});
    const json body = request_body(adding.profile(), "the small bambu one");
    std::vector<std::string> names;
    for (const json& tool : body.at("tools")) {
        names.push_back(tool.at("name"));
        CHECK_FALSE(tool.at("parameters").at("properties").contains("planId"));
        // No tool takes a credential from the model.
        for (const char* secret : {"accessCode", "apiKey", "credential", "password"})
            CHECK_FALSE(tool.at("parameters").at("properties").contains(secret));
    }
    std::sort(names.begin(), names.end());
    CHECK(names == PrinterConversation::session_tools());
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

    const json body = request_body(conversation.profile(), "",
                                   {{"assistant", "What printer do you have?"},
                                    {"user", "the small bambu one"},
                                    {"developer", "The person said Bambu Lab A1 mini is not their printer."}});
    const json& input = body.at("input");
    REQUIRE_FALSE(input.empty());
    CHECK(input.back() == json{{"role", "developer"}, {"content", "The person said Bambu Lab A1 mini is not their printer."}});
    CHECK(std::none_of(input.begin(), input.end(), [](const json& item) {
        return item.value("role", "") == "user" && item.value("content", json()).is_array();
    }));
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
