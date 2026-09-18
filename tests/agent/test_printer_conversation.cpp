// The printer panel's session: what it states, what its tools do, and what a
// tap on one of its cards asks for. The backend and the panel are fakes, so
// these are the conversation's own decisions, with no Orca and no model.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.hpp"
#include "../jusprin_support/FakeWorkspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::PrinterSetup;
using nlohmann::json;

namespace {

CatalogPrinter mini()
{
    CatalogPrinter printer;
    printer.id               = "BBL/Bambu Lab A1 mini";
    printer.vendor_id        = "BBL";
    printer.vendor_name      = "Bambu Lab";
    printer.model_id         = "Bambu Lab A1 mini";
    printer.model_name       = "A1 mini";
    printer.device_model_id  = "N1";
    printer.build_volume     = "180 × 180 × 180 mm";
    printer.default_plate    = "Textured PEI Plate";
    printer.default_material = "Bambu PLA Basic @BBL A1M";
    printer.nozzles          = {0.2, 0.4, 0.6, 0.8};
    return printer;
}

CatalogPrinter v2()
{
    CatalogPrinter printer = mini();
    printer.id          = "Creality/Ender-3 V2";
    printer.vendor_id   = "Creality";
    printer.vendor_name = "Creality";
    printer.model_id    = "Ender-3 V2";
    printer.model_name  = "Ender-3 V2";
    return printer;
}

class FakeBackend final : public IPrinterBackend
{
public:
    std::vector<CatalogPrinter>    catalogue{mini(), v2()};
    std::vector<DiscoveredPrinter> network;
    std::vector<SavedPrinter>      saved;
    std::string                    refusal;

    std::vector<AddPrinterRequest>    added;
    std::vector<ChangePrinterRequest> changed;
    int                               manual_setups{0};
    std::vector<std::string>          settings_opened;

    std::vector<CatalogPrinter> catalog_models() const override { return catalogue; }
    std::vector<DiscoveredPrinter> network_printers() const override { return network; }
    std::vector<SavedPrinter>      saved_printers() const override { return saved; }

    std::string add_printer(const AddPrinterRequest& request, SavedPrinter& result) override
    {
        added.push_back(request);
        if (!refusal.empty())
            return refusal;
        result.name = request.name;
        return {};
    }
    std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& result) override
    {
        changed.push_back(request);
        if (!refusal.empty())
            return refusal;
        result = saved.empty() ? SavedPrinter{} : saved.front();
        if (request.nozzle)
            result.nozzle = *request.nozzle;
        return {};
    }
    void run_manual_setup() override { ++manual_setups; }
    void open_printer_settings(const std::string& name) override { settings_opened.push_back(name); }
};

class RecordingPanel final : public IConversationHost
{
public:
    std::vector<std::string> notes;
    std::vector<std::string> prompts;
    int                      states{0};
    int                      closes{0};
    int                      refreshes{0};

    void post_note(const std::string& text) override { notes.push_back(text); }
    void ask_agent(const std::string& prompt) override { prompts.push_back(prompt); }
    void session_changed() override { ++states; }
    void close_panel() override { ++closes; }
    void printers_changed() override { ++refreshes; }
};

// One tool call, as the coordinator would hand it over.
Agent::ToolActivity call(Agent::ToolHandler handler, const json& arguments, const std::string& message = "m-1")
{
    Agent::ToolActivity activity;
    activity.correlation_id = message;
    activity.arguments_json = arguments.dump();
    (void) handler;
    return activity;
}

// A "propose" identify call for one printer, as the agent would send it once
// it has settled on a catalogId from the list the session gave it.
Agent::ToolExecutionCoordinator::ExtensionResult propose_one(PrinterConversation& conversation, const std::string& catalog_id,
                                                              json extra = json::object(), const std::string& message = "m-1")
{
    json arguments = json{{"action", "propose"}, {"catalogIds", json::array({catalog_id})}, {"say", "Assuming the usual."}};
    arguments.update(extra);
    return conversation.execute_tool(Agent::ToolHandler::PrinterIdentify,
                                     call(Agent::ToolHandler::PrinterIdentify, arguments, message));
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
    return printer;
}

} // namespace

TEST_CASE("an Add session opens with nothing stated and the ways in", "[printer-conversation]")
{
    FakeBackend     backend;
    RecordingPanel  panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const json state = conversation.state_json();
    CHECK(state.at("mode") == "add");
    CHECK(state.at("caption") == "NEW PRINTER");
    for (const char* fact : {"printer", "nozzle", "plate", "filament"})
        CHECK(state.at("facts").at(fact).at("value") == "");
    CHECK(state.at("placeholder").get<std::string>().find("bambu a1 mini") != std::string::npos);

    // The opening says how a photo gets in, and lists what is on the network.
    const json& blocks = state.at("blocks");
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].at("kind") == "tip");
    CHECK(blocks[1].at("kind") == "network");
    CHECK(blocks[1].at("printers")[0].at("serial") == "01P00A3B");
    CHECK(conversation.opening_message().find("What printer do you have?") != std::string::npos);
}

TEST_CASE("the session offers only its own tools and not the project", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const Agent::AgentSessionProfile profile = conversation.profile();
    CHECK(profile.tool_names == PrinterConversation::session_tools());
    CHECK_FALSE(profile.include_workspace);
    CHECK(profile.instructions.find("printer panel") != std::string::npos);
    CHECK(profile.instructions.find("Never say a printer has been added") != std::string::npos);
    // The whole catalogue the session was given, one model per line, so the
    // agent never has to search for it.
    CHECK(profile.instructions.find("BBL/Bambu Lab A1 mini | 180 × 180 × 180 mm") != std::string::npos);
    CHECK(profile.instructions.find("Creality/Ender-3 V2 | 180 × 180 × 180 mm") != std::string::npos);
}

TEST_CASE("a proposal fills the pinned card and draws the printer", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto proposed = propose_one(conversation, "BBL/Bambu Lab A1 mini", json{{"provenance", "assumed"}}, "m-7");
    REQUIRE(proposed.handled);
    REQUIRE_FALSE(proposed.error.has_value());

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "Bambu Lab A1 mini");
    CHECK(state.at("facts").at("printer").at("provenance") == "settled");
    // What nobody stated is the agent's guess, and says so.
    CHECK(state.at("facts").at("nozzle").at("value") == "0.4 mm");
    CHECK(state.at("facts").at("nozzle").at("provenance") == "assumed");
    CHECK(state.at("facts").at("plate").at("value") == "Textured PEI Plate");
    CHECK(state.at("facts").at("filament").at("value") == "PLA");

    const json& card = state.at("blocks").back();
    CHECK(card.at("kind") == "printers");
    CHECK(card.at("afterMessageId") == "m-7");
    CHECK(card.at("live") == true);
    CHECK(card.at("printers")[0].at("vendor") == "Bambu Lab");
    CHECK(card.at("printers")[0].at("model") == "A1 mini");
    CHECK(card.at("printers")[0].at("subline") == "180 × 180 × 180 mm");
    CHECK(card.at("printers")[0].at("action") == "add");

    // Adding and rejecting live on the card, not the chip row.
    CHECK(state.at("chips").empty());
}

TEST_CASE("rejecting a proposal collapses its card and clears the pinned one", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    propose_one(conversation, "BBL/Bambu Lab A1 mini");
    REQUIRE(conversation.state_json().at("facts").at("printer").at("value") == "Bambu Lab A1 mini");

    REQUIRE(conversation.handle_page_message("printer_action", json{{"action", "reject"}, {"id", "BBL/Bambu Lab A1 mini"}}));

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "");
    const json& blocks = state.at("blocks");
    const json& card    = blocks.back();
    CHECK(card.at("kind") == "printers");
    CHECK(card.at("live") == false);
    REQUIRE(panel.prompts.size() == 1);
    CHECK(panel.prompts.front().find("Not this one") != std::string::npos);
}

TEST_CASE("a newer answer collapses the printer it superseded", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto first = propose_one(conversation, "BBL/Bambu Lab A1 mini");
    REQUIRE_FALSE(first.error.has_value());
    const auto second = propose_one(conversation, "Creality/Ender-3 V2");
    REQUIRE_FALSE(second.error.has_value());

    const json blocks = conversation.state_json().at("blocks");
    const json& earlier = blocks[blocks.size() - 2];
    const json& latest  = blocks.back();
    CHECK(earlier.at("kind") == "printers");
    CHECK(earlier.at("live") == false);
    CHECK(latest.at("live") == true);
    // Only the newest is still settled.
    CHECK(conversation.state_json().at("facts").at("printer").at("value") == "Creality Ender-3 V2");
}

TEST_CASE("two candidates settle nothing and ask on their own cards", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto proposed = conversation.execute_tool(
        Agent::ToolHandler::PrinterIdentify,
        call(Agent::ToolHandler::PrinterIdentify,
             json{{"action", "propose"},
                  {"catalogIds", json::array({"BBL/Bambu Lab A1 mini", "Creality/Ender-3 V2"})},
                  {"question", "Bambu or Creality?"},
                  {"say", "Two fit what you said."}}));
    REQUIRE_FALSE(proposed.error.has_value());

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "");
    CHECK(state.at("blocks").back().at("printers").size() == 2);
    CHECK(state.at("blocks").back().at("printers")[0].at("action") == "choose");
    // Nothing to add yet, so nothing offers to.
    CHECK(state.at("chips").empty());
}

TEST_CASE("asking with no candidates settles nothing and draws nothing new", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    const std::size_t opening_blocks = conversation.state_json().at("blocks").size();

    const auto asked = conversation.execute_tool(
        Agent::ToolHandler::PrinterIdentify,
        call(Agent::ToolHandler::PrinterIdentify,
             json{{"action", "ask"}, {"catalogIds", json::array()}, {"question", "What size is the bed?"}, {"say", "Not enough yet."}}));
    REQUIRE_FALSE(asked.error.has_value());

    const json state = conversation.state_json();
    CHECK(state.at("blocks").size() == opening_blocks);
    CHECK(state.at("facts").at("printer").at("value") == "");
    CHECK(state.at("chips").empty());
}

TEST_CASE("unsupported clears whatever was proposed before it", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    propose_one(conversation, "BBL/Bambu Lab A1 mini");
    REQUIRE(conversation.state_json().at("facts").at("printer").at("value") == "Bambu Lab A1 mini");

    const auto unsupported = conversation.execute_tool(
        Agent::ToolHandler::PrinterIdentify,
        call(Agent::ToolHandler::PrinterIdentify,
             json{{"action", "unsupported"}, {"catalogIds", json::array()}, {"reason", "not_listed"}, {"say", "Not one this app ships."}}));
    REQUIRE_FALSE(unsupported.error.has_value());

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("printer").at("value") == "");
    // The proposal's own chips are gone with it.
    CHECK(state.at("chips").empty());

    const json& blocks = state.at("blocks");
    const json& earlier = blocks[blocks.size() - 2];
    const json& latest  = blocks.back();
    CHECK(earlier.at("kind") == "printers");
    CHECK(earlier.at("live") == false);
    CHECK(latest.at("kind") == "unsupported");
    CHECK(latest.at("reason") == "not_listed");
}

TEST_CASE("unsupported for a non-FDM printer draws nothing, only says so", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    const std::size_t opening_blocks = conversation.state_json().at("blocks").size();

    const auto unsupported = conversation.execute_tool(
        Agent::ToolHandler::PrinterIdentify,
        call(Agent::ToolHandler::PrinterIdentify,
             json{{"action", "unsupported"}, {"catalogIds", json::array()}, {"reason", "not_fdm"}, {"say", "That's a resin printer."}}));
    REQUIRE_FALSE(unsupported.error.has_value());

    // No exit block: there is nothing to browse or set up by hand for a
    // printer this app cannot slice for at all.
    CHECK(conversation.state_json().at("blocks").size() == opening_blocks);
}

TEST_CASE("a catalogId that is not on the list this session was given is refused", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto proposed = propose_one(conversation, "BBL/Invented");
    REQUIRE(proposed.handled);
    REQUIRE(proposed.error.has_value());
    CHECK(proposed.error->code == "unknown_printer");
    // The opening's own tip stays; no printer was drawn.
    const json blocks = conversation.state_json().at("blocks");
    CHECK(std::none_of(blocks.begin(), blocks.end(),
                       [](const json& block) { return block.value("kind", "") == "printers"; }));
}

TEST_CASE("a nozzle the model does not ship is refused before it is offered", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto proposed = propose_one(conversation, "BBL/Bambu Lab A1 mini", json{{"nozzle", 0.7}});
    REQUIRE(proposed.error.has_value());
    CHECK(proposed.error->code == "unknown_nozzle");
    // It says which sizes there are, so the agent can pass that on.
    CHECK(proposed.error->message.find("0.4 mm") != std::string::npos);
    // Nothing was drawn and nothing can be added.
    const json blocks = conversation.state_json().at("blocks");
    CHECK(std::none_of(blocks.begin(), blocks.end(),
                       [](const json& block) { return block.value("kind", "") == "printers"; }));
    CHECK(conversation.state_json().at("chips").empty());
}

TEST_CASE("chips are specific actions, replaced each time", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    conversation.execute_tool(Agent::ToolHandler::PrinterSuggest,
                              call(Agent::ToolHandler::PrinterSuggest,
                                   json{{"actions", json::array({json{{"label", "Use 0.3 mm layers"}, {"suggested", true}},
                                                                 json{{"label", "Keep 0.2 mm"}}})},
                                        {"hint", "or just type"}}));
    json state = conversation.state_json();
    REQUIRE(state.at("chips").size() == 2);
    CHECK(state.at("chips")[0].at("style") == "suggested");
    CHECK(state.at("chips")[0].at("say") == "Use 0.3 mm layers");
    CHECK(state.at("chipHint") == "or just type");

    conversation.execute_tool(Agent::ToolHandler::PrinterSuggest,
                              call(Agent::ToolHandler::PrinterSuggest,
                                   json{{"actions", json::array({json{{"label", "Connect it"}}})}}));
    state = conversation.state_json();
    REQUIRE(state.at("chips").size() == 1);
    CHECK(state.at("chips")[0].at("label") == "Connect it");
}

TEST_CASE("Use this states what the printer reported and asks the agent", "[printer-conversation]")
{
    FakeBackend     backend;
    RecordingPanel  panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    REQUIRE(conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}}));
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() == "\"Use this\" · 01P00A3B");
    REQUIRE(panel.prompts.size() == 1);
    CHECK(panel.prompts.front().find("01P00A3B") != std::string::npos);
    CHECK(panel.prompts.front().find("settled rather than assumed") != std::string::npos);
    CHECK(conversation.state_json().at("placeholder") == "access code, optional");
}

TEST_CASE("the device Use this named is attached to the next proposal, not asked of the model", "[printer-conversation]")
{
    FakeBackend     backend;
    RecordingPanel  panel;
    backend.network = {found_a1()};
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}});
    // The tool's own arguments never carry a deviceId; the app remembers
    // which network find this answer is about.
    propose_one(conversation, "BBL/Bambu Lab A1 mini", json{{"provenance", "settled"}});

    conversation.handle_page_message("printer_action", json{{"action", "add"}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().device_id == "01P00A3B");

    // Spent: a printer proposed afterwards, with no network round trip in
    // between, is not linked to a device that has moved on.
    propose_one(conversation, "Creality/Ender-3 V2");
    conversation.handle_page_message("printer_action", json{{"action", "add"}});
    REQUIRE(backend.added.size() == 2);
    CHECK(backend.added.back().device_id.empty());
}

TEST_CASE("a printer that has left the network says so instead", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    conversation.handle_page_message("printer_action", json{{"action", "network_pick"}, {"id", "01P00A3B"}});
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front().find("no longer on the network") != std::string::npos);
    CHECK(panel.prompts.empty());
}

TEST_CASE("Add this printer saves it and hands Home back", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    propose_one(conversation, "BBL/Bambu Lab A1 mini", json{{"nozzle", 0.6}});

    conversation.handle_page_message("printer_action", json{{"action", "add"}, {"accessCode", "12345678"}});
    REQUIRE(backend.added.size() == 1);
    CHECK(backend.added.front().vendor_id == "BBL");
    CHECK(backend.added.front().model_id == "Bambu Lab A1 mini");
    // The nozzle the person stated is the profile variant it is saved on.
    CHECK(backend.added.front().variant == "0.6");
    CHECK(backend.added.front().access_code == "12345678");
    CHECK(panel.refreshes == 1);
    CHECK(panel.closes == 1);
}

TEST_CASE("a printer that could not be saved says why and stays open", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    backend.refusal = "That access code was not accepted.";
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);
    propose_one(conversation, "BBL/Bambu Lab A1 mini");

    conversation.handle_page_message("printer_action", json{{"action", "add"}});
    CHECK(panel.closes == 0);
    REQUIRE(panel.notes.size() == 1);
    CHECK(panel.notes.front() == "That access code was not accepted.");
}

TEST_CASE("a Change session states the printer it is about", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   saved;
    saved.name   = "Lab Printer";
    saved.model  = "Bambu Lab A1 Combo";
    saved.nozzle = 0.4;
    saved.plate  = "Textured PEI Plate";
    saved.ams    = "AMS lite";
    saved.spools = {PrinterSpool{"PLA Matte", "PLA", "#5f7d4f"}, PrinterSpool{"PETG", "PETG", "#204080"}};
    backend.saved = {saved};

    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    const json state = conversation.state_json();
    CHECK(state.at("mode") == "change");
    CHECK(state.at("caption") == "PRINTER");
    CHECK(state.at("facts").at("printer").at("value") == "Lab Printer");
    CHECK(state.at("facts").at("nozzle").at("value") == "0.4 mm");
    // The plate a printer ships with is a starting point, not a reading.
    CHECK(state.at("facts").at("plate").at("provenance") == "assumed");
    CHECK(state.at("facts").at("filament").at("value") == "AMS lite · PLA Matte + 1");
    CHECK(state.at("facts").at("filament").at("swatch") == "#5f7d4f");
    CHECK(conversation.opening_message().find("Lab Printer") != std::string::npos);
    CHECK(conversation.state_json().at("blocks").empty());
}

TEST_CASE("a nozzle change goes through the backend and says it changed", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   saved;
    saved.name   = "Lab Printer";
    saved.nozzle = 0.4;
    backend.saved = {saved};

    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Change, "Lab Printer");

    // The store answers with the printer as it now is.
    backend.saved.front().nozzle = 0.6;
    const auto result = conversation.execute_tool(Agent::ToolHandler::PrinterChange,
                                                   call(Agent::ToolHandler::PrinterChange, json{{"nozzle", 0.6}}));
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(backend.changed.size() == 1);
    CHECK(backend.changed.front().name == "Lab Printer");
    CHECK(backend.changed.front().nozzle == Catch::Approx(0.6));

    const json state = conversation.state_json();
    CHECK(state.at("facts").at("nozzle").at("value") == "0.6 mm");
    CHECK(state.at("facts").at("nozzle").at("provenance") == "changed");
    CHECK(panel.refreshes == 1);
}

TEST_CASE("changing a printer that is not there fails rather than inventing one", "[printer-conversation]")
{
    FakeBackend         backend;
    RecordingPanel      panel;
    PrinterConversation conversation(backend, panel);
    conversation.start(ConversationMode::Add);

    const auto result = conversation.execute_tool(Agent::ToolHandler::PrinterChange,
                                                   call(Agent::ToolHandler::PrinterChange, json{{"nozzle", 0.6}}));
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == "no_printer");
    CHECK(backend.changed.empty());
}

TEST_CASE("Set it up myself leads to Orca's own screens", "[printer-conversation]")
{
    FakeBackend    backend;
    RecordingPanel panel;
    SavedPrinter   saved;
    saved.name    = "Lab Printer";
    backend.saved = {saved};

    PrinterConversation adding(backend, panel);
    adding.start(ConversationMode::Add);
    adding.handle_page_message("printer_action", json{{"action", "manual_setup"}});
    CHECK(backend.manual_setups == 1);
    CHECK(panel.closes == 1);

    PrinterConversation changing(backend, panel);
    changing.start(ConversationMode::Change, "Lab Printer");
    changing.handle_page_message("printer_action", json{{"action", "manual_setup"}});
    CHECK(backend.settings_opened == std::vector<std::string>{"Lab Printer"});
    // The settings window closes back into this session, which stays open.
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
    // The header menu names whatever printer profile is selected, which is a
    // system one until the person has added a printer of their own.
    conversation.start(ConversationMode::Change, "Bambu Lab A1 mini 0.4 nozzle");

    CHECK(conversation.mode() == ConversationMode::Add);
    CHECK(conversation.printer_name().empty());
    CHECK(conversation.state_json().at("caption") == "NEW PRINTER");
    CHECK(conversation.opening_message().find("What printer do you have?") != std::string::npos);
}

TEST_CASE("a printer conversation writes nothing into the project", "[printer-conversation]")
{
    Slic3r::GUI::JusPrin::Workspace::FakeWorkspace workspace;
    const std::filesystem::path project = workspace.auxiliary_data_dir();

    Agent::ProjectPersistence::Config config;
    config.in_memory = true;
    // A recovery root it must ignore: a session with no project has nothing
    // to recover into.
    config.recovery_root = (project / "recovery").string();
    Agent::ProjectPersistence persistence(workspace, std::move(config));
    persistence.attach();

    REQUIRE(persistence.document().has_identity());
    persistence.document().append_message("c-1", Agent::ConversationMessage{"m-1"}, persistence.timestamp());
    persistence.commit();
    persistence.flush();
    REQUIRE(persistence.write_attachment_blob("attachments/a-1/photo.png", "bytes"));

    // Nothing on disk, and the bytes still readable for as long as the
    // session lives.
    CHECK(persistence.read_attachment_blob("attachments/a-1/photo.png") == "bytes");
    CHECK_FALSE(std::filesystem::exists(project / "JusPrin"));
    CHECK_FALSE(std::filesystem::exists(project / "recovery"));
    CHECK(persistence.recovery_dir().empty());
}
