#pragma once

// The subject of the printer panel's conversation: what the model is told and
// may call there, what the panel pins above the thread, and what a tap on one
// of its cards does.
//
// The model does the understanding: which printer someone has, and what
// changed on one they own. Its one tool per mode checks that decision against
// the printer data, carries it out and returns facts. Every tap whose result
// the app already knows is handled here, and recorded in the thread as a note
// -- a plain statement of what happened, never an instruction -- which the
// model reads on its next turn.
//
// A session is opened for one printer question -- add a printer, or change
// this one -- and is discarded when the panel closes. Nothing here is written
// to the project: AgentHost keeps the thread in an in-memory document, and
// the printers themselves are saved through IPrinterBackend, which owns the
// only route to Orca.
//
// GUI-free and Orca-free: the panel supplies both interfaces, and the tests
// supply fakes.

#include "PrinterBackend.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentService.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// What the panel asks the person about.
enum class ConversationMode { Add, Change };

// One row of the pinned card. An empty value is the em dash the panel draws
// for a fact nobody has stated yet.
struct PinnedFact
{
    std::string value;
    // settled: known, from the printer or the person. assumed: the profile's
    // default, stated as such. changed: altered in this session.
    std::string provenance{"settled"};
    std::string swatch; // "#RRGGBB" of the first loaded spool, when there is one
};

// What "Add this printer" would save. Filled when one printer's card is
// drawn, spent by the person tapping Add, and never by the model.
struct PrinterProposal
{
    bool        valid{false};
    std::string block_id; // the card it was drawn on
    std::string vendor_id;
    std::string model_id;
    std::string model_name; // brand and model, as the card shows it
    std::string variant;
    std::string material;
    std::string device_id;
};

// Everything the panel needs from its owner. The panel is the only
// implementation; the tests use a recording fake.
class IConversationHost
{
public:
    virtual ~IConversationHost() = default;

    // A line in the thread stating what happened, which the model reads as
    // the app's own words. Returns its message id.
    virtual std::string post_note(const std::string& text) = 0;
    // The model answers next, from the conversation as it stands.
    virtual void start_turn() = 0;
    // The pinned card, the chips and the thread's own cards have changed, and
    // so may have what the model is told.
    virtual void session_changed() = 0;
    // The page sent new instructions for the model.
    virtual void profile_changed() = 0;
    // The panel is done: back to the printer list.
    virtual void close_panel() = 0;
    // A printer was saved; Home's list is out of date.
    virtual void printers_changed() = 0;
};

class PrinterConversation
{
public:
    PrinterConversation(IPrinterBackend& backend, IConversationHost& host);

    // Starts a session. `printer_name` names the printer a Change session is
    // about and is ignored when adding.
    void start(ConversationMode mode, const std::string& printer_name = {});

    ConversationMode   mode() const { return m_mode; }
    const std::string& printer_name() const { return m_printer_name; }

    // What the model is told and may call this session.
    Agent::AgentSessionProfile profile() const;
    // The panel's own opening line, which the agent is shown as having said.
    std::string opening_message() const;
    // Ties the cards drawn under the opening to it, once it has an id.
    void anchor_opening(const std::string& message_id);

    // The pinned card, the chips, the composer's placeholder and the cards
    // that sit in the thread, as the page reads them.
    nlohmann::json state_json() const;

    // A message from the panel's own page. Returns false for anything that is
    // not one of ours.
    bool handle_page_message(const std::string& type, const nlohmann::json& payload);

    // Checks a call before its card is shown, and states the change on it.
    std::optional<Agent::ToolError> preflight_tool(Agent::ToolHandler handler, Agent::ToolActivity& activity) const;
    // The session's own tools. Returns an unhandled result for any other
    // handler, so the host's own extensions still run.
    Agent::ToolExecutionCoordinator::ExtensionResult execute_tool(Agent::ToolHandler         handler,
                                                                 const Agent::ToolActivity& activity);
    // What the model reads back for a call to this session's tools: the
    // tool's own result, its error, or -- for a change the person kept as it
    // was -- that nothing changed. nullopt for anything else.
    std::optional<nlohmann::json> tool_output(const Agent::ToolActivity& activity) const;

    // The one tool each mode offers.
    static std::vector<std::string> session_tools(ConversationMode mode);

private:
    nlohmann::json identify(const nlohmann::json& arguments, const std::string& message_id,
                            std::optional<Agent::ToolError>& error);
    nlohmann::json change(const nlohmann::json& arguments, const std::string& message_id,
                          std::optional<Agent::ToolError>& error);

    // One printer, as printer_identify returns it.
    nlohmann::json identified_json(const CatalogPrinter& printer, double nozzle) const;
    // The printer a Change session is about, as the model is told it.
    nlohmann::json printer_json() const;
    // The facts the page's instructions for the model state: every printer
    // and what is on the network when adding, the printer when changing.
    nlohmann::json context_json() const;
    double         assumed_nozzle(const CatalogPrinter& printer) const;
    // Draws one printer's card with Add, fills the pinned card, and makes it
    // the printer "Add this printer" saves.
    void show_proposal(const CatalogPrinter& printer, double nozzle, bool nozzle_stated, const std::string& block_id,
                       const DiscoveredPrinter* device);
    void clear_proposal();
    // Every card that could still be tapped folds away: a newer answer, or a
    // refusal, has replaced it.
    void collapse_cards();
    std::string next_block_id() { return "b" + std::to_string(m_next_block++); }

    void add_proposed_printer(const std::string& access_code);
    void use_network_printer(const std::string& device_id);
    void choose_candidate(const std::string& catalog_id, const std::string& block_id);
    void reject_proposal();
    void undo_change(const std::string& block_id);
    void read_saved_printer();
    void refresh_network();

    const CatalogPrinter* catalog_entry(const std::string& id) const;
    const CatalogPrinter* model_of(const DiscoveredPrinter& device) const;
    nlohmann::json*       block(const std::string& id);

    IPrinterBackend&   m_backend;
    IConversationHost& m_host;

    ConversationMode m_mode{ConversationMode::Add};
    std::string      m_printer_name;
    SavedPrinter     m_printer; // the Change session's subject

    // Pinned facts, in the order the panel draws them.
    PinnedFact m_printer_fact, m_nozzle, m_plate, m_filament;

    PrinterProposal m_proposal;
    // The cards in the thread, oldest first.
    nlohmann::json  m_blocks = nlohmann::json::array();
    std::string     m_placeholder;
    unsigned        m_next_block{1};
    // The nozzle the model named for the candidates on a card, by card id,
    // so "This one" keeps it.
    std::map<std::string, double> m_stated_nozzle;

    // What Undo puts back: the printer as it was before the last change.
    struct UndoRecord
    {
        std::string                              block_id;
        std::optional<double>                    nozzle;
        std::optional<std::vector<PrinterSpool>> spools;
    };
    std::optional<UndoRecord> m_undo;

    std::vector<DiscoveredPrinter> m_network;
    // The model's instructions, as the page wrote them for this session.
    std::string m_instructions;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
