#pragma once

// The subject of the printer panel's conversation: what the agent is told and
// may call there, what the panel pins above the thread, and what a tap on one
// of its cards does.
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
    // settled: known, from the printer or the person. assumed: the agent's
    // stated guess. changed: altered in this session, and the panel says so.
    std::string provenance{"settled"};
    std::string swatch; // "#RRGGBB" of the first loaded spool, when there is one
};

// What "Add this printer" would save. Filled by printer_identify, spent by the
// person tapping the chip, and never by the agent.
struct PrinterProposal
{
    bool                valid{false};
    std::string         vendor_id;
    std::string         model_id;
    std::string         model_name;
    std::string         variant;
    std::string         material;
    std::string         device_id;
    std::string         picture;
    std::string         subline;
};

// What Home's dismissible receipt strip says after "Add this printer": the
// model, and the three facts the way the pinned card stated them. An empty
// `name` means no printer was just added -- printers_changed asks only for
// a plain refresh, with nothing to draw or highlight.
struct AddedPrinterReceipt
{
    std::string name;
    std::string nozzle;
    std::string plate;
    std::string filament;
    // True when nozzle, plate and filament were the agent's guess rather
    // than something settled -- the strip says so, the way the pinned card
    // did.
    bool        assumed{false};
};

// Everything the panel needs from its owner. The panel is the only
// implementation; the tests use a recording fake.
class IConversationHost
{
public:
    virtual ~IConversationHost() = default;

    // A muted line in the thread, such as "Use this" · 01P00A3B.
    virtual void post_note(const std::string& text) = 0;
    // The agent answers next, with no user message in the thread: the person
    // tapped something rather than typing it.
    virtual void ask_agent(const std::string& prompt) = 0;
    // The pinned card, the chips and the thread's own cards have changed.
    virtual void session_changed() = 0;
    // The panel is done: back to the printer list.
    virtual void close_panel() = 0;
    // A printer was saved or changed; Home's list is out of date. A newly
    // added printer's receipt highlights its card once, puts it at the top
    // of Home's column, and draws the dismissible strip above the list.
    virtual void printers_changed(const AddedPrinterReceipt& added) = 0;
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

    // The session's own tools. Returns an unhandled result for any other
    // handler, so the host's own extensions still run.
    Agent::ToolExecutionCoordinator::ExtensionResult execute_tool(Agent::ToolHandler                handler,
                                                                 const Agent::ToolActivity&        activity);

    // A human summary of a pending printer_change's card, read from the
    // panel's current pinned facts before anything is applied -- the same
    // read execute_tool would do, without change_printer's side effect.
    // Nullopt for any other handler, or a change this session cannot yet
    // preview (see the .cpp): the generic card is what shows then.
    std::optional<Agent::ApprovalPreview> preview_approval(Agent::ToolHandler         handler,
                                                            const Agent::ToolActivity& activity) const;

    // The tool names this session offers, in registry order.
    static std::vector<std::string> session_tools();

private:
    nlohmann::json identify(const nlohmann::json& arguments, const std::string& message_id, std::optional<Agent::ToolError>& error);
    nlohmann::json suggest(const nlohmann::json& arguments);
    nlohmann::json change(const nlohmann::json& arguments, std::optional<Agent::ToolError>& error);

    void add_proposed_printer(const std::string& access_code);
    void use_network_printer(const std::string& device_id);
    void choose_candidate(const std::string& catalog_id);
    void reject_proposal();
    void read_saved_printer();
    void refresh_network();
    // Every earlier "printers" block stops offering a tap: superseded by a
    // newer answer, or by the person themselves rejecting the live one.
    void collapse_live_printer_blocks();

    // "Browse the full list": brands, then one vendor's models, entirely
    // inside the panel. Picking a model is the same tap as "This one" on an
    // agent-drawn candidate -- it answers through the agent, not around it,
    // so the assumptions it states are never duplicated in two places.
    void open_browse();
    void browse_into_vendor(const std::string& vendor_id);
    void browse_back();
    nlohmann::json browse_json() const;

    const CatalogPrinter* catalog_entry(const std::string& id) const;

    IPrinterBackend&   m_backend;
    IConversationHost& m_host;

    ConversationMode m_mode{ConversationMode::Add};
    std::string      m_printer_name;
    SavedPrinter     m_printer; // the Change session's subject

    // Pinned facts, in the order the panel draws them.
    PinnedFact m_printer_fact, m_nozzle, m_plate, m_filament;

    PrinterProposal m_proposal;
    // The cards the agent has drawn in the thread, oldest first.
    nlohmann::json  m_blocks   = nlohmann::json::array();
    nlohmann::json  m_chips    = nlohmann::json::array();
    std::string     m_chip_hint;
    std::string     m_placeholder;
    unsigned        m_next_block{1};

    // The whole catalogue the agent was given this session, for validating
    // the catalogIds it answers with.
    std::vector<CatalogPrinter>   m_catalog;
    std::vector<DiscoveredPrinter> m_network;

    // Set by "Use this" on a network find, read and cleared by the next
    // single-printer proposal: the app already knows the device id, so the
    // model is never asked to echo it back.
    std::string m_pending_device_id;

    // "Browse the full list": empty vendor id at the brands level, set once
    // a brand is opened. The panel shows this instead of the thread while
    // it is active; leaving it is the panel's own doing, not the agent's.
    bool        m_browsing{false};
    std::string m_browse_vendor_id;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
