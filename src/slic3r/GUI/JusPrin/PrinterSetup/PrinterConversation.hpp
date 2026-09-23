#pragma once

// The subject of the printer panel's conversation: what the model is told and
// may call there, and the picture cards the page draws in the thread.
//
// The conversation is the whole panel. The model does the understanding --
// which printer someone has, what changed on one they own, how to reach it --
// and acts through the session's tools, which check its decision against the
// printer data and carry it out. The person's yes in the conversation is the
// confirmation; the one card is printer_connect's, where a credential is typed
// so it never passes through the model.
//
// A session is opened for one printer question -- add a printer, change this
// one, connect this one -- and is discarded when the panel closes. Nothing
// here is written to the project: AgentHost keeps the thread in an in-memory
// document, and printers are saved through IPrinterBackend, which owns the
// only route to Orca.
//
// GUI-free and Orca-free: the panel supplies both interfaces, and the tests
// supply fakes.

#include "PrinterBackend.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentService.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// Which question the panel was opened for.
enum class ConversationMode { Add, Change, Connect };

// Everything the conversation needs from its owner. The panel is the only
// implementation; the tests use a recording fake.
class IConversationHost
{
public:
    virtual ~IConversationHost() = default;

    // A line in the thread stating what happened, which the model reads as
    // the app's own words. Returns its message id.
    virtual std::string post_note(const std::string& text) = 0;
    // The panel's first line, shown as the agent's. Returns its message id.
    virtual std::string post_opening(const std::string& text) = 0;
    // The model answers next, from the conversation as it stands.
    virtual void start_turn() = 0;
    // The cards in the thread, or what the model is told, have changed.
    virtual void session_changed() = 0;
    // The page sent new instructions for the model.
    virtual void profile_changed() = 0;
    // The panel is done: back to Home.
    virtual void close_panel() = 0;
    // Home's printer list is out of date. `added` names a printer this
    // session just added, which Home leads with; empty for any other change.
    virtual void printers_changed(const std::string& added = {}) = 0;
    // What the person typed into this action's card, taken once.
    virtual std::optional<std::string> take_credential(const std::string& action_id) = 0;
};

class PrinterConversation
{
public:
    PrinterConversation(IPrinterBackend& backend, IConversationHost& host);

    // Starts a session. `printer_name` names the printer a Change or Connect
    // session is about and is ignored when adding.
    void start(ConversationMode mode, const std::string& printer_name = {});

    ConversationMode   mode() const { return m_mode; }
    const std::string& printer_name() const { return m_printer_name; }

    // What the model is told and may call: every printer tool, always.
    Agent::AgentSessionProfile profile() const;
    // What the page draws the panel and writes the instructions from.
    nlohmann::json state_json() const;

    // A message from the panel's own page. Returns false for anything that is
    // not one of ours.
    bool handle_page_message(const std::string& type, const nlohmann::json& payload);

    // Checks printer_connect before its card is shown, and titles the card.
    std::optional<Agent::ToolError> preflight_tool(Agent::ToolHandler handler, Agent::ToolActivity& activity) const;
    // The session's own tools. Returns an unhandled result for any other
    // handler, so the host's own extensions still run.
    Agent::ToolExecutionCoordinator::ExtensionResult execute_tool(Agent::ToolHandler         handler,
                                                                 const Agent::ToolActivity& activity);
    // What the model reads back for a call to this session's tools.
    std::optional<nlohmann::json> tool_output(const Agent::ToolActivity& activity) const;

    // Called on the panel's pump. While a Bambu Lab connection is being
    // verified, looks at most once a second, and once it settles says how in
    // a note and has the model answer.
    void tick(std::chrono::steady_clock::time_point now);

    static std::vector<std::string> session_tools();

private:
    using Result = Agent::ToolExecutionCoordinator::ExtensionResult;
    Result identify(const nlohmann::json& arguments, const std::string& message_id);
    Result add(const nlohmann::json& arguments);
    Result change(const nlohmann::json& arguments);
    Result connection_status(const std::string& name);
    Result connect(const nlohmann::json& arguments, const std::string& action_id);
    Result manual_setup();

    SavedPrinter          saved(const std::string& name) const;
    const CatalogPrinter* catalog_entry(const std::string& id) const;
    std::string           unknown_printer_message(const std::string& name) const;
    nlohmann::json        printer_json(const SavedPrinter& printer) const;
    nlohmann::json        context_json() const;
    std::string           next_block_id() { return "b" + std::to_string(m_next_block++); }

    IPrinterBackend&   m_backend;
    IConversationHost& m_host;

    ConversationMode m_mode{ConversationMode::Add};
    // The printer the conversation is about: the one opened for, or the one
    // this session added.
    std::string m_printer_name;

    // The cards in the thread, oldest first.
    nlohmann::json m_blocks = nlohmann::json::array();
    unsigned       m_next_block{1};
    bool           m_opened{false};

    std::vector<DiscoveredPrinter> m_network;
    // The model's instructions, as the page wrote them for this session.
    std::string m_instructions;

    // What this session added, by catalogue id: the saved printer's name.
    std::map<std::string, std::string> m_added;
    // The printer discovery was started for this session.
    std::string m_prepared;
    // A Bambu Lab connection waiting for the printer to answer.
    std::string                           m_connecting;
    std::chrono::steady_clock::time_point m_last_look{};
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
