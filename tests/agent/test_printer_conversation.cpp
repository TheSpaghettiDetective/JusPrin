// The printer panel's session: what the model is told, what its one tool per
// mode checks and returns, and what the app does itself when the person taps.
// The first half drives the conversation alone, with a fake backend and a
// recording panel; the second runs it inside a real AgentHost with a scripted
// model, to pin what actually reaches the model.

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

    std::string add_printer(const AddPrinterRequest& request, SavedPrinter& result) override
    {
        added.push_back(request);
        if (!refusal.empty())
            return refusal;
        result.name = request.name;
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
    void run_manual_setup() override { ++manual_setups; }
    void open_printer_settings(const std::string& name) override { settings_opened.push_back(name); }
};

class RecordingPanel final : public IConversationHost
{
public:
    std::vector<std::string> notes;
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
    void start_turn() override { ++turns; }
    void session_changed() override { ++states; }
    void profile_changed() override { ++profiles; }
    void close_panel() override { ++closes; }
    void printers_changed() override { ++refreshes; }
};

Agent::ToolActivity call(const char* tool, const json& arguments, const std::string& message = "m-1")
{
    Agent::ToolActivity activity;
    activity.tool           = tool;
    activity.correlation_id = message;
    activity.arguments_json = arguments.dump();
    return activity;
}

Agent::ToolExecutionCoordinator::ExtensionResult identify(PrinterConversation& conversation, const json& arguments,
                                                          const std::string& message = "m-1")
{
    return conversation.execute_tool(Agent::ToolHandler::PrinterIdentify, call("printer_identify", arguments, message));
}

json identified(PrinterConversation& conversation, const json& arguments, const std::string& message = "m-1")
{
    const auto result = identify(conversation, arguments, message);
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.error.has_value());
    const json output = json::parse(result.result_json);
    // Whatever the tool returns is what the registry says it returns.
    CHECK(Agent::ToolRegistry::instance().validate_output(*Agent::ToolRegistry::instance().find("printer_identify"), output));
    return output;
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

TEST_CASE("each mode offers exactly its own tool and nothing of the project", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};

    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    const Agent::AgentSessionProfile add = adding.profile();
    CHECK(add.tool_names == std::vector<std::string>{"printer_identify"});
    CHECK_FALSE(add.include_workspace);
    CHECK(add.notes_in_context);

    PrinterConversation changing(backend, panel);
    changing.start(ConversationMode::Change, "Lab Printer");
    const Agent::AgentSessionProfile change = changing.profile();
    CHECK(change.tool_names == std::vector<std::string>{"printer_change"});
    CHECK_FALSE(change.include_workspace);
    CHECK(change.notes_in_context);
}

TEST_CASE("an Add session sends the page every printer and what is on the network", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    // The page writes the model's instructions from these facts.
    const json context = conversation.state_json().at("context");
    REQUIRE(context.at("printers").size() == backend.catalogue.size());
    CHECK(context["printers"][0] == json::array({"BBL/Bambu Lab A1 mini", "Bambu Lab A1 mini", "180 × 180 × 180 mm"}));
    CHECK(context.at("network") == json::array({json{{"name", "Bambu Lab A1 mini"}, {"serial", "01P00A3B"}}}));
    CHECK_FALSE(context.contains("printer"));
}

TEST_CASE("a Change session sends the page the printer as it is now", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const json context = conversation.state_json().at("context");
    CHECK_FALSE(context.contains("printers"));
    const json& printer = context.at("printer");
    CHECK(printer.at("name") == "Lab Printer");
    CHECK(printer.at("model") == "Bambu Lab A1 mini");
    CHECK(printer.at("nozzle") == 0.4);
    CHECK(printer.at("nozzles") == json::array({0.2, 0.4, 0.6, 0.8}));
    CHECK(printer.at("spools")[0] == json{{"name", "PLA Matte"}, {"material", "PLA"}, {"colour", "#5f7d4f"}});
    CHECK(printer.at("connected") == false);
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
    // The same words again change nothing.
    conversation.handle_page_message("printer_instructions", json{{"text", "You add printers."}});
    CHECK(panel.profiles == 1);
    // Nothing the size of no prompt the page means to send.
    conversation.handle_page_message("printer_instructions", json{{"text", std::string(300 * 1024, 'x')}});
    CHECK(conversation.profile().instructions == "You add printers.");

    conversation.start(ConversationMode::Add);
    CHECK(conversation.profile().instructions.empty());
}

TEST_CASE("an Add session opens with nothing stated and the ways in", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json state = conversation.state_json();
    CHECK(state.at("mode") == "add");
    CHECK(state.at("caption") == "NEW PRINTER");
    for (const char* fact : {"printer", "nozzle", "plate", "filament"})
        CHECK(state.at("facts").at(fact).at("value") == "");
    CHECK(state.at("accessCode") == false);
    const json& blocks = state.at("blocks");
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].at("kind") == "tip");
    CHECK(blocks[1].at("kind") == "network");
    CHECK(blocks[1].at("printers")[0].at("serial") == "01P00A3B");
    CHECK(conversation.opening_message().find("What printer do you have?") != std::string::npos);
}

// -- printer_identify -------------------------------------------------------

TEST_CASE("one printer is checked, drawn, and returned as facts to mention", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json output = identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}}, "m-7");
    REQUIRE(output.at("printers").size() == 1);
    const json& printer = output["printers"][0];
    CHECK(printer.at("catalogId") == "BBL/Bambu Lab A1 mini");
    CHECK(printer.at("brand") == "Bambu Lab");
    CHECK(printer.at("model") == "A1 mini");
    CHECK(printer.at("buildVolume") == "180 × 180 × 180 mm");
    CHECK(printer.at("nozzles") == json::array({0.2, 0.4, 0.6, 0.8}));
    CHECK(printer.at("assumed").at("nozzle") == 0.4);
    CHECK(printer.at("assumed").at("plate") == "Textured PEI Plate");
    CHECK(printer.at("assumed").at("filament") == "Bambu PLA Basic @BBL A1M");
    CHECK(printer.at("alreadyYours") == false);

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "Bambu Lab A1 mini");
    CHECK(state.at("facts").at("printer").at("provenance") == "settled");
    CHECK(state.at("facts").at("nozzle").at("value") == "0.4 mm");
    CHECK(state.at("facts").at("nozzle").at("provenance") == "assumed");
    CHECK(state.at("facts").at("filament").at("value") == "Bambu PLA Basic @BBL A1M");

    const json& card = state.at("blocks").back();
    CHECK(card.at("kind") == "printers");
    CHECK(card.at("afterMessageId") == "m-7");
    CHECK(card.at("printers")[0].at("action") == "add");

    // Add and "Not this one" are the app's, not the model's words.
    const json& chips = state.at("chips");
    REQUIRE(chips.size() == 2);
    CHECK(chips[0].at("action") == "add");
    CHECK(chips[1].at("action") == "reject");
    CHECK_FALSE(chips[1].contains("say"));
    // Nothing was asked of the model, and nothing recorded.
    CHECK(panel.turns == 0);
    CHECK(panel.notes.empty());
}

TEST_CASE("a plate the profile does not name is null, never a guess", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json output = identified(conversation, json{{"catalogIds", {"Prusa/Prusa MK3S"}}});
    const json& assumed = output["printers"][0]["assumed"];
    CHECK(assumed.at("plate").is_null());
    // OrcaSlicer's own default for the profile, not the wizard's first tick.
    CHECK(assumed.at("filament") == "Prusa Generic PLA");
    CHECK(conversation.state_json().at("facts").at("plate").at("value") == "");
}

TEST_CASE("the assumed nozzle is 0.4, else the first size, else what was said", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    // Ships no 0.4: its first size, and that size's filament, which here is
    // none, so null.
    json output = identified(conversation, json{{"catalogIds", {"Creality/Creality Ender-3 V2"}}});
    CHECK(output["printers"][0]["assumed"]["nozzle"] == 0.6);
    CHECK(output["printers"][0]["assumed"]["filament"] == "Creality Generic PLA");

    output = identified(conversation, json{{"catalogIds", {"Creality/Creality Ender-3 V2"}}, {"nozzle", 0.8}});
    CHECK(output["printers"][0]["assumed"]["nozzle"] == 0.8);
    CHECK(output["printers"][0]["assumed"]["filament"].is_null());
    // Said, not assumed.
    CHECK(conversation.state_json().at("facts").at("nozzle").at("provenance") == "settled");

    // null and 0 are how the model leaves the nozzle unsaid.
    const auto& registry = Agent::ToolRegistry::instance();
    for (const json& unsaid : {json(nullptr), json(0)}) {
        const json arguments{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}, {"nozzle", unsaid}};
        CHECK(registry.validate_call(*registry.find("printer_identify"), arguments.dump()).valid());
        output = identified(conversation, arguments);
        CHECK(output["printers"][0]["assumed"]["nozzle"] == 0.4);
        CHECK(conversation.state_json().at("facts").at("nozzle").at("provenance") == "assumed");
    }
}

TEST_CASE("a printer the person already has says so", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json output = identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}});
    CHECK(output["printers"][0]["alreadyYours"] == true);
}

TEST_CASE("two or three candidates are a question on their own cards", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json output =
        identified(conversation, json{{"catalogIds", {"Creality/Creality Ender-3 V2", "Creality/Creality Ender-3 S1"}}});
    CHECK(output["printers"].size() == 2);

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "");
    CHECK(state.at("blocks").back().at("printers").size() == 2);
    CHECK(state.at("blocks").back().at("printers")[0].at("action") == "choose");
    CHECK(state.at("chips").empty());
}

TEST_CASE("more than three is refused with what to do instead", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto result = identify(conversation, json{{"catalogIds",
                                                     {"Creality/Creality Ender-3 V2", "Creality/Creality Ender-3 S1",
                                                      "Creality/Creality Ender-3 Pro", "Creality/Creality Ender-3 Max"}}});
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "too_many");
    CHECK(result.error->message == "More than three fit. Ask one question that narrows it down instead.");
    // A long list, as gpt-5.4-mini sends one for "my bed is 220x220", passes
    // the registry so that this is the answer it gets back.
    json many = json::array();
    for (int i = 0; i < 31; ++i)
        many.push_back("Vendor/Printer " + std::to_string(i));
    const auto& registry = Agent::ToolRegistry::instance();
    CHECK(registry.validate_call(*registry.find("printer_identify"), json{{"catalogIds", many}}.dump()).valid());
    const json blocks = conversation.state_json().at("blocks");
    CHECK(std::none_of(blocks.begin(), blocks.end(), [](const json& block) { return block.value("kind", "") == "printers"; }));
}

TEST_CASE("an id that is not on the list is named and refused", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto result = identify(conversation, json{{"catalogIds", {"BBL/N1"}}});
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "unknown_printer");
    CHECK_THAT(result.error->message, ContainsSubstring("\"BBL/N1\""));
    CHECK_THAT(result.error->message, ContainsSubstring("copy it exactly from the printer list"));
}

TEST_CASE("a nozzle the model does not ship lists the sizes it does", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto result = identify(conversation, json{{"catalogIds", {"Prusa/Prusa MK3S"}}, {"nozzle", 0.3}});
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "unknown_nozzle");
    CHECK(result.error->message == "The Prusa MK3S ships 0.25, 0.4, 0.6 and 0.8 mm nozzles, not 0.3 mm. Ask which of these is on the printer.");
    CHECK(conversation.state_json().at("chips").empty());
}

TEST_CASE("a newer answer folds the older cards away", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}}, "m-2");
    identified(conversation, json{{"catalogIds", {"Prusa/Prusa MK3S"}}}, "m-4");
    const json blocks = conversation.state_json().at("blocks");
    std::vector<json> cards;
    std::copy_if(blocks.begin(), blocks.end(), std::back_inserter(cards),
                 [](const json& block) { return block.value("kind", "") == "printers"; });
    REQUIRE(cards.size() == 2);
    CHECK(cards[0].value("collapsed", false));
    CHECK_FALSE(cards[1].value("collapsed", false));
    CHECK(conversation.state_json().at("facts").at("printer").at("value") == "Prusa MK3S");
}

// -- Taps the app answers itself ---------------------------------------------

TEST_CASE("This one draws that printer's card and records it, with no model turn", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini", "Prusa/Prusa MK3S"}}});
    const std::string card = conversation.state_json().at("blocks").back().at("id");

    REQUIRE(conversation.handle_page_message("printer_action",
                                             json{{"action", "candidate_pick"}, {"id", "Prusa/Prusa MK3S"}, {"blockId", card}}));
    CHECK(panel.turns == 0);
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() ==
          "The person chose Prusa MK3S. Its card offers Add, assuming a 0.4 mm nozzle and Prusa Generic PLA; the profile names no plate.");
    check_is_a_statement(panel.notes.front());

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "Prusa MK3S");
    CHECK(state.at("blocks").back().at("printers").size() == 1);
    CHECK(state.at("blocks").back().at("printers")[0].at("action") == "add");
    CHECK(state.at("chips")[0].at("action") == "add");
}

TEST_CASE("This one keeps the nozzle the model named for the card", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini", "Prusa/Prusa MK3S"}}, {"nozzle", 0.6}});
    const std::string card = conversation.state_json().at("blocks").back().at("id");

    conversation.handle_page_message("printer_action",
                                     json{{"action", "candidate_pick"}, {"id", "BBL/Bambu Lab A1 mini"}, {"blockId", card}});
    CHECK(panel.notes.front() == "The person chose Bambu Lab A1 mini. Its card offers Add, assuming a 0.6 mm nozzle, the Textured PEI "
                                 "Plate and Bambu PLA Basic @BBL A1M.");
    conversation.handle_page_message("printer_action", json{{"action", "add"}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().variant == "0.6");
}

TEST_CASE("Use this matches the reported model id and records what it reported", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    REQUIRE(conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}}));
    CHECK(panel.turns == 0);
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() == "The person chose the network printer 01P00A3B, a Bambu Lab A1 mini that reports a 0.4 mm nozzle.");
    check_is_a_statement(panel.notes.front());

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("nozzle").at("provenance") == "settled");
    CHECK(state.at("facts").at("filament").at("value") == "AMS lite · PLA Matte + 1");
    CHECK(state.at("facts").at("filament").at("provenance") == "settled");
    // The card sits under the note that records it, and offers the code.
    CHECK(state.at("blocks").back().at("afterMessageId") == "note-1");
    CHECK(state.at("blocks").back().at("printers")[0].at("deviceId") == "01P00A3B");
    CHECK(state.at("accessCode") == true);
    // Nothing asks for the code in the chat.
    CHECK_THAT(state.at("placeholder").get<std::string>(), !ContainsSubstring("access code"));
}

TEST_CASE("a network printer the list cannot match is the model's to ask about", "[printer-conversation]")
{
    FakeBackend       backend;
    RecordingPanel    panel;
    DiscoveredPrinter odd = found_a1();
    odd.device_model_id   = "Z9";
    backend.network       = {odd};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}});
    REQUIRE(panel.notes.size() == 1);
    CHECK_THAT(panel.notes.front(), ContainsSubstring("\"Z9\""));
    check_is_a_statement(panel.notes.front());
    CHECK(panel.turns == 1);
    CHECK(conversation.state_json().at("chips").empty());
}

TEST_CASE("a printer that has left the network is recorded and the model answers", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}});
    REQUIRE(panel.notes.size() == 1);
    CHECK_THAT(panel.notes.front(), ContainsSubstring("no longer on the network"));
    CHECK(panel.turns == 1);
}

TEST_CASE("Not this one folds the card, clears the pin and hands the turn to the model", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}});

    conversation.handle_page_message("printer_action", json{{"action", "reject"}});
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() == "The person said Bambu Lab A1 mini is not their printer.");
    CHECK(panel.turns == 1);
    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "");
    CHECK(state.at("chips").empty());
    CHECK(state.at("blocks").back().value("collapsed", false));
}

TEST_CASE("Add this printer saves it and hands Home back", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    identified(conversation, json{{"catalogIds", {"Prusa/Prusa MK3S"}}, {"nozzle", 0.25}});

    conversation.handle_page_message("printer_action", json{{"action", "add"}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().vendor_id == "Prusa");
    CHECK(backend.added.front().model_id == "Prusa MK3S");
    // The profile's own spelling of the size, and the default saved is the
    // one the card showed.
    CHECK(backend.added.front().variant == "0.25");
    CHECK(backend.added.front().material == "Prusa Generic PLA");
    CHECK(backend.added.front().access_code.empty());
    CHECK(panel.refreshes == 1);
    CHECK(panel.closes == 1);
}

TEST_CASE("an access code goes from its field to the app and only its existence is recorded", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.network = {found_a1()};
    backend.refusal = "That access code was not accepted.";
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}});

    conversation.handle_page_message("printer_action", json{{"action", "add"}, {"accessCode", "12345678"}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().access_code == "12345678");
    CHECK(backend.added.front().device_id == "01P00A3B");
    // Refused, so the panel stays, and the thread holds the record and the
    // reason -- never the code.
    CHECK(panel.closes == 0);
    CHECK(std::find(panel.notes.begin(), panel.notes.end(), "An access code was entered.") != panel.notes.end());
    for (const std::string& note : panel.notes)
        CHECK_THAT(note, !ContainsSubstring("12345678"));
    CHECK_THAT(conversation.state_json().dump(), !ContainsSubstring("12345678"));
}

TEST_CASE("a code with no network printer to go to is not sent", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}});

    conversation.handle_page_message("printer_action", json{{"action", "add"}, {"accessCode", "12345678"}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().access_code.empty());
    CHECK(panel.notes.empty());
}

// -- printer_change -----------------------------------------------------------

TEST_CASE("a change is checked before its card, and the card states it", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const auto preflight = [&](const json& arguments) {
        Agent::ToolActivity activity = call("printer_change", arguments);
        const std::optional<Agent::ToolError> error = conversation.preflight_tool(Agent::ToolHandler::PrinterChange, activity);
        return std::make_pair(error, activity);
    };

    SECTION("nothing named") {
        const auto [error, activity] = preflight(json::object());
        REQUIRE(error.has_value());
        CHECK(error->code == "nothing_to_change");
        CHECK(error->message == "Name the nozzle or the spools that changed.");
    }
    SECTION("a size this model does not ship") {
        const auto [error, activity] = preflight(json{{"nozzle", 0.3}});
        REQUIRE(error.has_value());
        CHECK(error->code == "unknown_nozzle");
        CHECK(error->message == "The Bambu Lab A1 mini ships 0.2, 0.4, 0.6 and 0.8 mm nozzles, not 0.3 mm. Ask which of these is on the printer.");
    }
    SECTION("what it already is") {
        const auto [error, activity] = preflight(json{{"nozzle", 0.4}});
        REQUIRE(error.has_value());
        CHECK(error->code == "nothing_to_change");
    }
    SECTION("the printer went while the card would wait") {
        backend.saved.clear();
        const auto [error, activity] = preflight(json{{"nozzle", 0.6}});
        REQUIRE(error.has_value());
        CHECK(error->code == "printer_gone");
        CHECK_THAT(error->message, ContainsSubstring("Lab Printer"));
    }
    SECTION("a real change is restated for the card") {
        const auto [error, activity] = preflight(json{{"nozzle", 0.6}});
        REQUIRE_FALSE(error.has_value());
        CHECK(activity.title == "Change nozzle");
        const json arguments = json::parse(activity.arguments_json);
        CHECK(arguments.at("confirm").at("printer") == "Bambu Lab A1 mini");
        CHECK(arguments.at("confirm").at("before").at("nozzle") == 0.4);
        CHECK(arguments.at("nozzle") == 0.6);
    }
    SECTION("an unchanged field is dropped from the card") {
        const auto [error, activity] =
            preflight(json{{"nozzle", 0.4}, {"spools", json::array({json{{"name", "Teal PLA"}, {"material", "PLA"}}})}});
        REQUIRE_FALSE(error.has_value());
        CHECK(activity.title == "Change spools");
        const json arguments = json::parse(activity.arguments_json);
        CHECK_FALSE(arguments.contains("nozzle"));
        CHECK(arguments.at("confirm").at("before").at("spools").size() == 2);
    }
}

TEST_CASE("an applied change returns what changed and the printer as it now is", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const auto result = conversation.execute_tool(Agent::ToolHandler::PrinterChange,
                                                  call("printer_change", json{{"nozzle", 0.6}}, "m-3"));
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.error.has_value());
    const json output = json::parse(result.result_json);
    CHECK(Agent::ToolRegistry::instance().validate_output(*Agent::ToolRegistry::instance().find("printer_change"), output));
    CHECK(output.at("state") == "applied");
    REQUIRE(output.at("changed").size() == 1);
    CHECK(output["changed"][0] == json{{"field", "nozzle"}, {"before", 0.4}, {"after", 0.6}});
    CHECK(output.at("printer").at("nozzle") == 0.6);
    CHECK(output.at("printer").at("nozzles") == json::array({0.2, 0.4, 0.6, 0.8}));
    CHECK(output.at("printer").at("connected") == false);
    REQUIRE(backend.changed.size() == 1);
    CHECK_FALSE(backend.changed.front().spools.has_value());

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("nozzle").at("value") == "0.6 mm");
    CHECK(state.at("facts").at("nozzle").at("provenance") == "changed");
    const json& undo = state.at("blocks").back();
    CHECK(undo.at("kind") == "undo");
    CHECK(undo.at("text") == "Nozzle set to 0.6 mm");
    CHECK(undo.at("afterMessageId") == "m-3");
    CHECK(panel.refreshes == 1);
    // What the model is told next is the printer as it now is.
    CHECK(conversation.state_json().at("context").at("printer").at("nozzle") == 0.6);
}

TEST_CASE("Undo reverses the last change directly, with no card and no model turn", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");
    conversation.execute_tool(Agent::ToolHandler::PrinterChange, call("printer_change", json{{"nozzle", 0.6}}));
    const std::string undo = conversation.state_json().at("blocks").back().at("id");

    conversation.handle_page_message("printer_action", json{{"action", "undo"}, {"id", undo}});
    REQUIRE(backend.changed.size() == 2);
    REQUIRE(backend.changed.back().nozzle.has_value());
    CHECK(*backend.changed.back().nozzle == 0.4);
    CHECK(backend.saved.front().nozzle == 0.4);
    CHECK(panel.turns == 0);
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() == "The person undid the change: the nozzle is 0.4 mm again.");
    check_is_a_statement(panel.notes.front());
    const json blocks = conversation.state_json().at("blocks");
    CHECK(std::none_of(blocks.begin(), blocks.end(), [](const json& block) { return block.value("kind", "") == "undo"; }));
    CHECK(conversation.state_json().at("facts").at("nozzle").at("provenance") == "settled");

    // A second tap has nothing left to undo.
    conversation.handle_page_message("printer_action", json{{"action", "undo"}, {"id", undo}});
    CHECK(backend.changed.size() == 2);
}

TEST_CASE("a change the person kept is declined, with the printer as it is", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    Agent::ToolActivity kept = call("printer_change", json{{"nozzle", 0.6}});
    kept.state              = Agent::ToolState::Rejected;
    const std::optional<json> output = conversation.tool_output(kept);
    REQUIRE(output.has_value());
    CHECK(output->at("state") == "declined");
    CHECK(output->at("changed").empty());
    CHECK(output->at("printer").at("nozzle") == 0.4);
    CHECK(Agent::ToolRegistry::instance().validate_output(*Agent::ToolRegistry::instance().find("printer_change"), *output));
    CHECK(backend.changed.empty());
}

TEST_CASE("a Change session states the printer it is about", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const json state = conversation.state_json();
    CHECK(state.at("mode") == "change");
    CHECK(state.at("caption") == "PRINTER");
    CHECK(state.at("facts").at("printer").at("value") == "Lab Printer");
    CHECK(state.at("facts").at("nozzle").at("value") == "0.4 mm");
    CHECK(state.at("facts").at("plate").at("provenance") == "assumed");
    CHECK(state.at("facts").at("filament").at("value") == "AMS lite · PLA Matte + 1");
    CHECK(state.at("facts").at("filament").at("swatch") == "#5f7d4f");
    CHECK(conversation.opening_message().find("Lab Printer") != std::string::npos);
    CHECK(state.at("blocks").empty());
}

TEST_CASE("Set it up myself leads to Orca's own screens", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    backend.saved = {lab_printer()};

    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    adding.handle_page_message("printer_action", json{{"action", "manual_setup"}});
    CHECK(backend.manual_setups == 1);
    CHECK(panel.closes == 1);

    PrinterConversation changing(backend, panel);
    changing.start(ConversationMode::Change, "Lab Printer");
    changing.handle_page_message("printer_action", json{{"action", "manual_setup"}});
    CHECK(backend.settings_opened == std::vector<std::string>{"Lab Printer"});
    CHECK(panel.closes == 1);
}

TEST_CASE("the panel answers only its own messages", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    CHECK_FALSE(conversation.handle_page_message("user_message", json{{"text", "hello"}}));
    CHECK(conversation.handle_page_message("printer_action", json{{"action", "close"}}));
    CHECK(panel.closes == 1);
}

TEST_CASE("a printer this app has not saved is one to add", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Bambu Lab A1 mini 0.4 nozzle");

    CHECK(conversation.mode() == ConversationMode::Add);
    CHECK(conversation.printer_name().empty());
    CHECK(conversation.state_json().at("caption") == "NEW PRINTER");
    CHECK(conversation.profile().tool_names == std::vector<std::string>{"printer_identify"});
}

TEST_CASE("the printer tools join no plans", "[printer-conversation]")
{
    for (const char* name : {"printer_identify", "printer_change"}) {
        INFO(name);
        const Agent::ToolDefinition* definition = Agent::ToolRegistry::instance().find(name);
        REQUIRE(definition != nullptr);
        CHECK_FALSE(Agent::joins_plans(*definition));
        CHECK_FALSE(definition->input_schema["properties"].contains("planId"));
        CHECK_FALSE(definition->input_schema["properties"].contains("accessCode"));
    }
    for (const char* gone : {"printer_catalog_search", "printer_propose", "printer_suggest"})
        CHECK(Agent::ToolRegistry::instance().find(gone) == nullptr);
    CHECK(Agent::ToolRegistry::instance().find("printer_identify")->description ==
          "You decide which printer this is. This tool checks the printers you name against the app's printer list, shows them to "
          "the person, and returns the details to mention. It never picks a printer.");
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

    std::string post_note(const std::string& text) override { return host->post_note(text); }
    void        start_turn() override { host->start_turn(); }
    void        session_changed() override { profile_changed(); }
    void        profile_changed() override
    {
        if (host != nullptr && conversation != nullptr)
            host->set_session_profile(conversation->profile());
    }
    void close_panel() override {}
    void printers_changed() override {}
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
    std::vector<json>         sent;

    explicit PrinterHost(ConversationMode mode, bool printer_session = true)
        : host(workspace, persistence, Agent::AgentAvailability::Ready, false, make_agent())
    {
        persistence.attach();
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
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
        host.on_page_message(json{{"protocol", Agent::Protocol::kName},
                                  {"version", Agent::Protocol::kVersion},
                                  {"id", "w-hello"},
                                  {"type", "hello"},
                                  {"payload", {{"protocolVersions", {Agent::Protocol::kVersion}}, {"capabilities", {"streaming"}}}}}
                                 .dump());
    }

    Agent::AgentServicePtr make_agent()
    {
        auto scripted = std::make_unique<ScriptedAgent>();
        agent         = scripted.get();
        return scripted;
    }

    void say(const std::string& text)
    {
        static int next = 0;
        host.on_page_message(json{{"protocol", Agent::Protocol::kName},
                                  {"version", Agent::Protocol::kVersion},
                                  {"id", "w-" + std::to_string(++next)},
                                  {"type", "user_message"},
                                  {"payload", {{"clientMessageId", "c-" + std::to_string(next)}, {"text", text}}}}
                                 .dump());
    }

    void decide(const std::string& action_id, const std::string& decision)
    {
        host.on_page_message(json{{"protocol", Agent::Protocol::kName},
                                  {"version", Agent::Protocol::kVersion},
                                  {"id", "w-decide-" + action_id + decision},
                                  {"type", "tool_decision"},
                                  {"payload", {{"actionId", action_id}, {"decision", decision}}}}
                                 .dump());
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
        for (const json& envelope : sent)
            if (envelope.value("type", "") == type)
                found.push_back(envelope);
        return found;
    }
};

} // namespace

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

TEST_CASE("a printer tool's result reaches the model as the tool's own facts, with no project", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Add);
    harness.agent->call = Agent::ToolRequest{"printer_identify", json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}}.dump()};
    harness.say("the small bambu one");
    harness.pump();

    REQUIRE(harness.agent->results.size() == 1);
    const json output = json::parse(harness.agent->results.front().output_json);
    CHECK_FALSE(output.contains("workspace"));
    CHECK_FALSE(output.contains("workspaceRevision"));
    CHECK(output.at("printers")[0].at("catalogId") == "BBL/Bambu Lab A1 mini");
    // And the turn itself carried no project either.
    CHECK_FALSE(harness.agent->requests.front().session.include_workspace);
    // No card to approve: the person's tap on Add is the decision.
    const auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    CHECK_FALSE(activities.front().requires_approval);
}

TEST_CASE("a change to a size the printer lacks fails before any card", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Change);
    harness.agent->call = Agent::ToolRequest{"printer_change", json{{"nozzle", 0.3}}.dump()};
    harness.say("i put a 0.3 on it");
    harness.pump();

    const auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    CHECK(activities.front().state == Agent::ToolState::Failed);
    REQUIRE(harness.agent->results.size() == 1);
    const json output = json::parse(harness.agent->results.front().output_json);
    CHECK(output.at("error").at("code") == "unknown_nozzle");
    CHECK(harness.backend.changed.empty());
}

TEST_CASE("a change waits for its card: kept is declined, set is applied", "[printer-conversation][host]")
{
    PrinterHost harness(ConversationMode::Change);
    harness.agent->call = Agent::ToolRequest{"printer_change", json{{"nozzle", 0.6}}.dump()};
    harness.say("i put a 0.6 nozzle on it");
    harness.pump();

    auto activities = harness.activities();
    REQUIRE(activities.size() == 1);
    CHECK(activities.front().state == Agent::ToolState::Pending);
    CHECK(activities.front().requires_approval);
    CHECK(activities.front().title == "Change nozzle");
    CHECK(harness.backend.changed.empty());

    SECTION("Keep 0.4 mm") {
        harness.decide(activities.front().action_id, "reject");
        harness.pump();
        REQUIRE(harness.agent->results.size() == 1);
        const json output = json::parse(harness.agent->results.front().output_json);
        CHECK(output.at("state") == "declined");
        CHECK(output.at("changed").empty());
        CHECK(harness.backend.changed.empty());
    }
    SECTION("Set 0.6 mm") {
        harness.decide(activities.front().action_id, "approve");
        harness.pump();
        REQUIRE(harness.agent->results.size() == 1);
        const json output = json::parse(harness.agent->results.front().output_json);
        CHECK(output.at("state") == "applied");
        CHECK(output.at("changed")[0].at("after") == 0.6);
        CHECK_FALSE(output.contains("workspace"));
        CHECK(harness.backend.saved.front().nozzle == 0.6);

        // Undo is the app's: no new card, no new tool call, no model turn.
        const std::size_t requests = harness.agent->requests.size();
        const std::string undo     = harness.conversation.state_json().at("blocks").back().at("id");
        harness.conversation.handle_page_message("printer_action", json{{"action", "undo"}, {"id", undo}});
        harness.pump();
        CHECK(harness.backend.saved.front().nozzle == 0.4);
        CHECK(harness.activities().size() == 1);
        CHECK(harness.agent->requests.size() == requests);
    }
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
    void        run_manual_setup() override {}
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

TEST_CASE("the worked examples' facts come from the shipped profiles", "[printer-conversation][catalog]")
{
    CatalogBackend      backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    // "the small bambu one"
    json output = identified(conversation, json{{"catalogIds", {"BBL/Bambu Lab A1 mini"}}});
    CHECK(output["printers"][0]["buildVolume"] == "180 × 180 × 180 mm");
    CHECK(output["printers"][0]["assumed"] ==
          json{{"nozzle", 0.4}, {"plate", "Textured PEI Plate"}, {"filament", "Bambu PLA Basic @BBL A1M"}});

    // "voron 2.4 350mm": no plate to talk about.
    output = identified(conversation, json{{"catalogIds", {"Voron/Voron 2.4 350"}}});
    CHECK(output["printers"][0]["assumed"]["plate"].is_null());

    // "the prusa, the 0.3 nozzle"
    const auto refused = identify(conversation, json{{"catalogIds", {"Prusa/Prusa MK3S"}}, {"nozzle", 0.3}});
    REQUIRE(refused.error.has_value());
    CHECK(refused.error->message == "The Prusa MK3S ships 0.25, 0.4, 0.6 and 0.8 mm nozzles, not 0.3 mm. Ask which of these is on the printer.");
}

TEST_CASE("the model is offered one tool per mode and no project", "[printer-conversation][openai]")
{
    CatalogBackend backend;
    backend.saved = {lab_printer()};
    RecordingPanel panel;

    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    adding.handle_page_message("printer_instructions", json{{"text", "You add printers."}});
    json body = request_body(adding.profile(), "the small bambu one");
    REQUIRE(body.at("tools").size() == 1);
    CHECK(body["tools"][0].at("name") == "printer_identify");
    CHECK_FALSE(body["tools"][0].at("parameters").at("properties").contains("planId"));
    CHECK(body.at("instructions") == adding.profile().instructions);
    const json& user = body.at("input").back();
    CHECK(user.at("role") == "user");
    CHECK(user.at("content")[0].at("text") == "the small bambu one");

    PrinterConversation changing(backend, panel);
    changing.start(ConversationMode::Change, "Lab Printer");
    changing.handle_page_message("printer_instructions", json{{"text", "You change printers."}});
    body = request_body(changing.profile(), "i put a 0.6 nozzle on it");
    CHECK(body.at("instructions") == "You change printers.");
    REQUIRE(body.at("tools").size() == 1);
    CHECK(body["tools"][0].at("name") == "printer_change");
    CHECK_FALSE(body["tools"][0].at("parameters").at("properties").contains("accessCode"));
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
        identified_all[printer.id] = identified(adding, json{{"catalogIds", {printer.id}}})["printers"][0];
    std::ofstream(std::string(out) + "/identify_results.json") << identified_all.dump(2);
}
