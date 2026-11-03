#pragma once

// The subject of the printer panel's conversation: what the model is told and
// may call there, and the cards the page draws in the thread.
//
// The conversation is the whole panel. The model does the understanding --
// which printer someone has, what changed on one they own, how to reach it --
// and acts through the session's tools, which check requests against the
// printer data and carry them out. printer_connect collects its credential in
// a local form so it never passes through the model. The other things to tap
// are Undo on an added printer's receipt and the printer form, where the
// person picks a printer from the lists themselves; the model hears about
// either in a note.
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
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentService.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <future>
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

    // The chat so far, for a report; see connection_chat_record.
    virtual nlohmann::json chat_record() const = 0;
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
    // The panel is done: restore its originating workspace.
    virtual void close_panel() = 0;
    // Opens `url` in the person's default browser. False when none opened.
    virtual bool open_in_browser(const std::string& url) = 0;
    // This app's version, as an issue report gives it.
    virtual std::string app_version() const = 0;
    // Home's printer list is out of date. `added` names a printer this
    // session just added, which Home leads with; empty for any other change.
    virtual void printers_changed(const std::string& added = {}) = 0;
};

// What a connection report says of a chat: its messages as {role, text} under
// "turns", and its connection tools' calls under "tools". The access code or
// API key goes into a form of its own and is in neither.
nlohmann::json connection_chat_record(const std::vector<Agent::ConversationMessage>& messages,
                                      const std::vector<Agent::ToolActivity>&        activities);

// The GitHub page for a new JusPrin issue, filled in with `title` and `body`
// and ending in the app's own `footer`. A body that would take the link past
// kIssueLinkLimit bytes is cut, never the footer, and `shortened` says so.
inline constexpr std::size_t kIssueLinkLimit = 8000;
struct IssueLink
{
    std::string url;
    bool        shortened{false};
};
IssueLink issue_link(const std::string& title, const std::string& body, const std::string& footer);

class PrinterConversation
{
public:
    PrinterConversation(IPrinterBackend& backend, IConversationHost& host);

    // Starts a session. `printer_name` names the printer a Change or Connect
    // session is about and is ignored when adding: a saved printer, or for a
    // change also the stock profile the project has selected. The caller has
    // checked that it is there.
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

    // Checks printer_connect before its input form is shown, and titles it.
    std::optional<Agent::ToolError> preflight_tool(Agent::ToolHandler handler, Agent::ToolActivity& activity);
    // The session's own tools. Returns an unhandled result for any other
    // handler, so the host's own extensions still run.
    Agent::ToolExecutionCoordinator::ExtensionResult execute_tool(Agent::ToolHandler         handler,
                                                                 const Agent::ToolActivity& activity);
    // What the model reads back for a call to this session's tools.
    std::optional<nlohmann::json> tool_output(const Agent::ToolActivity& activity) const;
    // The skill for this printer's connection type, once the person has
    // tried to connect it, and the tool that comes with it: issue_report_open,
    // for someone stuck who wants the developers to know. Nothing before
    // that, so someone who only picks a printer and prepares prints never
    // gets either, and nothing for a connection type that has no skill yet.
    std::optional<Agent::SessionSkill> connection_skill() const;

    // Called on the panel's pump. Picks up the printer form's options once
    // they are read. While a connection is being verified, looks at most once
    // a second, and once it settles says how in a note and has the model
    // answer. Does the same for a network search: when it first finds a
    // printer, and when it has found none after about 15 seconds, or cannot
    // hear any because this app does not hold the port they announce on.
    void tick(std::chrono::steady_clock::time_point now);

    // A settings tool this session ran has settled. A copy of the printer
    // saved in place of a stock profile is the printer from then on.
    void tool_settled(const Agent::ToolActivity& activity);

    // The printer panel's own tools, and the settings tools a session also
    // offers for the printer it is about.
    static std::vector<std::string> session_tools();
    static std::vector<std::string> settings_tools();

private:
    using Result = Agent::ToolExecutionCoordinator::ExtensionResult;
    Result identify(const nlohmann::json& arguments, const std::string& message_id);
    Result add(const nlohmann::json& arguments, const std::string& message_id);
    Result change(const nlohmann::json& arguments);
    Result connection_status(const std::string& name, const std::string& message_id);
    Result request_credential(const nlohmann::json& arguments, const std::string& action_id);
    Result open_issue(const nlohmann::json& arguments);
    void   connect(const nlohmann::json& arguments, const std::string& credential);
    // Why printer_connect refused `asked`, from what the app has observed.
    std::string    unknown_device_text(const PrinterConnectionInfo& info, const std::string& asked) const;
    // How far the network search has got, for a refusal and for the status.
    std::string    search_text() const;
    nlohmann::json search_json(const PrinterConnectionInfo& info) const;
    void           watch_search(std::chrono::steady_clock::time_point now);
    // The notice that Bambu's network plug-in is missing, under this message:
    // drawn by the app, once a session, whatever the model says around it.
    void show_plugin_needed(const std::string& message_id);
    // Its Install button: runs the installer, and tells the model if the
    // plug-in is there afterwards.
    void install_network_plugin();
    // A connection attempt failed; `message_id` is the note that says so.
    // The second failure in a session puts the report card under it.
    void connection_failed(const std::string& message_id);
    // The person's answer on that card.
    void answer_report(const std::string& block_id, bool send);
    // Drops a connection attempt still waiting, so what the printer answers
    // later is never read.
    void abandon_connection();
    // Undo on an added printer's receipt: removes that printer.
    void undo_add(const std::string& block_id);
    // The printer form: opened from the header, then asked for one model's
    // options, then finished or closed. Only finishing saves anything.
    void open_form();
    void ask_form_options(const nlohmann::json& payload);
    void finish_form(const nlohmann::json& payload);
    nlohmann::json form_json() const;

    SavedPrinter          saved(const std::string& name) const;
    // The printer the session is about: a saved one, or the stock profile a
    // change was opened for.
    SavedPrinter          about() const;
    const CatalogPrinter* catalog_entry(const std::string& id) const;
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
    // The printer discovery was started for this session -- once: starting
    // again switches the printer agent and restarts discovery, so a model that
    // checks again would never let it find anything.
    std::string m_prepared;
    // That search, from when it could run. `said_none`, `port` and `found`
    // record what the model has been told, by a note or by reading the status;
    // once it has been told of a printer there is nothing left to watch for.
    struct NetworkSearch
    {
        std::chrono::steady_clock::time_point started;
        std::chrono::steady_clock::time_point last_look{};
        bool                                  said_none{false};
        DiscoveryPort                         port{DiscoveryPort::Ours};
        bool                                  found{false};
    };
    std::optional<NetworkSearch> m_search;
    // The printer this is about cannot be reached until Bambu's network
    // plug-in is installed.
    bool m_needs_plugin{false};
    // The person has tried to connect the printer this is about: opened the
    // chat for that, had the model call a connection tool, or linked a printer
    // found on the network in the form.
    bool m_connect_tried{false};
    // A print host's kind, "moonraker" or "octoprint", once the saved
    // connection or a connection attempt names one; it picks the skill.
    std::string m_host_type;
    int  m_failed_connections{0};
    // The local credential form and the call it will complete. The secret is
    // passed directly to connect and is never stored here.
    std::string    m_credential_action;
    nlohmann::json m_credential_arguments = nlohmann::json::object();
    // The connection waiting for the printer to answer.
    std::string                           m_connecting;
    std::chrono::steady_clock::time_point m_last_look{};

    // The printer form while it is open. Its options are read off the GUI
    // thread, one model and nozzle at a time; a read still running when the
    // form closes is waited for, and dropped.
    struct PrinterForm
    {
        // The found printers it offers, with the catalogue model each is.
        std::vector<std::pair<DiscoveredPrinter, std::string>> nearby;
        std::optional<SetupOptions> options;
        std::string                 asked_id;
        double                      asked_nozzle{0.};
        std::future<SetupOptions>   reading;
        // Why the last finish did not save, for the person.
        std::string error;
    };
    std::optional<PrinterForm> m_form;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
