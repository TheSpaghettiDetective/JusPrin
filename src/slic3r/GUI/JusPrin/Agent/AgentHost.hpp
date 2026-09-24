#pragma once

// Native side of the typed Agent bridge. The host owns the runtime
// conversation flow, consumes the workspace contract for context, and talks
// to the page only through versioned JSON envelopes handed to a send
// callback. Conversation and timeline state is document-backed: the
// ProjectPersistence document is the durable record, and every mutation goes
// through it, so an explicit project save, a reload, or a crash recovery all
// reconstruct the same state. GUI-free and deterministic: streaming and tool
// execution advance only when the owner calls the pump methods.
//
// The page renders host state and submits typed requests; it never owns an
// editable copy of the conversation or project. Reload reconstruction works
// by re-running the handshake: every hello is answered with the complete
// state.

#include "AgentProtocol.hpp"
#include "AgentService.hpp"
#include "AgentSetup.hpp"
#include "ProjectPersistence.hpp"
#include "ToolExecutionCoordinator.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpConfigFile.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Mcp { class McpRuntime; struct CatalogItem; }

namespace Slic3r::GUI::JusPrin::Agent {

class AgentHost
{
public:
    using SendFn = std::function<void(const std::string& envelope_json)>;

    AgentHost(Workspace::IWorkspace& workspace,
              ProjectPersistence&    persistence,
              AgentAvailability      availability,
              bool                   dark_appearance,
              AgentServicePtr        agent = {},
              AgentSetupServicePtr   setup = {});
    ~AgentHost();

    AgentHost(const AgentHost&) = delete;
    AgentHost& operator=(const AgentHost&) = delete;

    void set_send(SendFn send);

    // -- Sessions with a subject of their own --------------------------------
    // The host is the project's conversation unless an owner says otherwise.
    // A panel whose conversation is about something else -- the printer panel
    // on Home -- supplies the four things that differ: what the model is told
    // and may call, the turn that opens the conversation, the page messages of
    // its own surface, and the state its surface pins above the thread.

    // Stamped on every request this host makes.
    void set_session_profile(AgentSessionProfile profile) { m_session_profile = std::move(profile); }
    // The model answers next, from the conversation as it stands, with no
    // user message: the person tapped something whose result the app has
    // already recorded in the thread. Queued behind a turn in flight.
    void start_turn();
    // Answers a page message this host does not know. Returning false leaves
    // it to the host, which reports it as a type outside the protocol.
    using PageMessageHandler = std::function<bool(const std::string& type, const nlohmann::json& payload)>;
    void set_page_message_handler(PageMessageHandler handler) { m_page_message_handler = std::move(handler); }
    // Carried in every `state` snapshot as `session`, so a page that reloads
    // is told what its surface shows without asking the subject itself.
    void set_session_state_provider(std::function<nlohmann::json()> provider)
    {
        m_session_state_provider = std::move(provider);
    }
    // Decodes an image attachment's raw bytes into a small thumbnail data
    // URL, for the composer/thread preview only -- the model still gets the
    // untouched original, read separately for its own context. Empty (the
    // default, and what every GUI-free test leaves it) means no preview:
    // decoding an arbitrary image needs wx, which this host's own build
    // does not link, so the real app wires this after construction and the
    // tests wire a fake.
    using ImageThumbnailFn = std::function<std::string(const std::string& bytes)>;
    void set_image_thumbnail_maker(ImageThumbnailFn maker) { m_image_thumbnail_maker = std::move(maker); }
    // Sends one envelope of a type this protocol declares. The subject uses
    // it to push its own state between snapshots.
    void send_page_envelope(const std::string& type, const nlohmann::json& payload);
    // Runs the session's own tools, inside the coordinator's approval and
    // state machine. Anything it leaves unhandled stays with the host.
    void set_session_tool_executor(ToolExecutionCoordinator::ExtensionExecutor executor)
    {
        m_session_tool_executor = std::move(executor);
    }
    // Checks a call to the session's own tools before its card or its run.
    void set_session_tool_preflight(ToolExecutionCoordinator::ExtensionPreflight preflight);
    // What the model reads back for a call to the session's own tools, in
    // place of the host's envelope; nullopt keeps the host's.
    using ToolOutputFormatter = std::function<std::optional<nlohmann::json>(const ToolActivity& activity)>;
    void set_session_tool_output(ToolOutputFormatter formatter) { m_session_tool_output = std::move(formatter); }
    // What the person typed into an approval card beside approving it -- a
    // credential the model must never see -- handed once to the executor of
    // that action and then forgotten. It is kept out of the activity, every
    // envelope, every note and the document.
    std::optional<nlohmann::json> take_decision_input(const std::string& action_id);
    // Appends a message the agent is shown as having said, without asking the
    // model for it: a panel whose opening line is always the same. Returns
    // its id so the owner can anchor what it draws underneath.
    std::string post_assistant_message(const std::string& text);

    // Invoked after every successful hello handshake (initial load and every
    // reload); the owner uses it to cancel its connection deadline.
    void set_handshake_listener(std::function<void()> listener) { m_handshake_listener = std::move(listener); }

    // Invoked once, right after pump_setup() persists a verified credential
    // and installs the newly connected agent. A throwaway setup-only host
    // (e.g. one embedded in a native dialog rather than the docked panel)
    // uses this to know when to hand control back to its owner instead of
    // polling availability().
    void set_setup_completed_listener(std::function<void()> listener) { m_setup_completed_listener = std::move(listener); }

    // Call when the page starts (re)loading; the host requires a new
    // handshake before any other message and pauses stream delivery until the
    // page reconnects. Conversation state is unaffected.
    void reset_page();

    // One JSON envelope from the page.
    void on_page_message(const std::string& envelope_json);

    void set_appearance(bool dark);
    void set_availability(AgentAvailability availability);
    void set_agent(AgentServicePtr agent, AgentAvailability availability);
    // Opens the page's setup flow; held until the next handshake when the
    // page is not connected yet.
    void request_setup();

    bool              handshake_complete() const { return m_handshake; }
    bool              stream_active() const { return m_stream.has_value(); }
    AgentAvailability availability() const { return m_availability; }

    // Emits the next assistant delta (or the terminal event) when a stream is
    // active and the page is connected. The owner paces this from a timer;
    // tests call it directly.
    void pump_stream();

    // Advances native execution while the page is connected or MCP is enabled,
    // drains the MCP mailbox, and gives dirty state its throttled flush.
    void pump_tools();

    // Releases a finished credential check. A verified credential is
    // persisted and its already-connected service installed here, so setup
    // succeeding and the Agent becoming available are one step.
    void pump_setup();

    ToolExecutionCoordinator&       tools() { return m_tools; }
    const ToolExecutionCoordinator& tools() const { return m_tools; }
    ProjectPersistence&             persistence() { return m_persistence; }
    // Opt-in local adapter; its lifetime is independent of the page handshake.
    void start_mcp(const std::string& discovery_path);
    // Where the facts a person states about their printers are kept: app
    // data, not the project. Without it the tools that read or write them say
    // the operation is unavailable.
    void set_printer_facts_path(std::string path) { m_product_state.set_facts_path(std::move(path)); }
    const Mcp::McpRuntime* mcp() const { return m_mcp.get(); }

    struct McpConnectSettings
    {
        std::string helper_path;
        std::string launcher_path;
        std::vector<std::string> launch_arguments;
        std::string startup_error;
        std::filesystem::path home;
        std::filesystem::path config_home;
        bool windows{false};
    };
    using McpCliDone = std::function<void(bool success, std::string diagnostic)>;
    using McpCliRunner = std::function<void(const std::vector<std::string>& arguments, McpCliDone done)>;
    void configure_mcp_connect(McpConnectSettings settings);
    void set_mcp_cli_runner(McpCliRunner runner);
    // Reveals one file in the desktop's file manager. The host stays GUI-free,
    // so the wx side supplies the implementation the way it does the CLI
    // runner above.
    using RevealPathFn = std::function<void(const std::string& path)>;
    void set_reveal_path_handler(RevealPathFn reveal);

    // The active conversation's messages, straight from the document.
    std::vector<ConversationMessage> conversation() const;

    // Appends one host-authored note to the active conversation: a short
    // factual line about something the shell just changed. It is durable like
    // any message, renders without a bubble, starts no reply, and is never
    // sent to the model. Returns the note's ID.
    std::string post_note(const std::string& text);

    // Diagnostics for the internal-connection error surface.
    std::uint64_t messages_sent() const { return m_messages_sent; }
    std::uint64_t messages_received() const { return m_messages_received; }

private:
    struct ActiveStream
    {
        std::string         conversation_id;
        ConversationMessage message; // authoritative in-progress copy
        int                 next_seq{0};
    };

    struct PendingToolContinuation
    {
        std::string call_id;
        std::string conversation_id;
        std::string user_message_id;
    };

    void send_envelope(const char* type, const std::string& payload_json, const std::string& correlation_id = {});
    void send_bridge_error(const std::string& code, const std::string& message, const std::string& correlation_id = {});
    void send_state(const std::string& correlation_id = {});
    void send_context();
    void send_conversations();

    // Reads one page envelope and routes it. Split from on_page_message so a
    // fault anywhere in reading or handling a message is caught in one place,
    // with that message's own type and id still in hand.
    void dispatch_page_message(const std::string& envelope_json, std::string& type, std::string& envelope_id);

    void handle_hello(const std::string& envelope_id, const std::string& payload_json);
    void handle_user_message(const std::string& envelope_id, const std::string& payload_json);
    void handle_stop(const std::string& payload_json);
    void handle_retry(const std::string& envelope_id, const std::string& payload_json);
    void handle_tool_decision(const std::string& envelope_id, const std::string& payload_json);
    void handle_tool_cancel(const std::string& envelope_id, const std::string& payload_json);
    void handle_create_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_switch_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_rename_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_delete_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_draft_update(const std::string& payload_json);
    void handle_attach_file(const std::string& envelope_id, const std::string& payload_json);
    void handle_remove_attachment(const std::string& envelope_id, const std::string& payload_json);
    void handle_setup_check_key(const std::string& envelope_id, const std::string& payload_json);
    void handle_setup_cancel();
    void handle_mcp_catalog(const std::string& envelope_id);
    void handle_mcp_preview(const std::string& envelope_id, const std::string& payload_json);
    void handle_mcp_connect(const std::string& envelope_id, const std::string& payload_json);
    void handle_reveal_path(const std::string& envelope_id, const std::string& payload_json);
    // The catalog as the connect settings currently describe it. Every MCP
    // handler resolves a tool id through this, so none of them takes a path
    // from the page.
    std::vector<Mcp::CatalogItem> mcp_catalog_items() const;
    void send_mcp_status(const std::string& phase, const std::string& tool_id, const std::string& correlation_id = {},
                         const std::string& backup = {}, const std::optional<AgentError>& error = std::nullopt,
                         const std::string& diagnostic = {});
    void send_setup_status(const std::string&               phase,
                           const std::string&               correlation_id = {},
                           std::optional<int>               elapsed_ms     = std::nullopt,
                           const std::optional<AgentError>& error          = std::nullopt,
                           const std::string&               warning        = {});
    void send_attachment_updated(const AttachmentRecord& record, const std::string& correlation_id = {});
    std::string attachment_preview_data_url(const AttachmentRecord& record) const;
    void send_tool_activity(const ToolActivity& activity, const std::string& correlation_id = {});
    ToolExecutionCoordinator::ExtensionResult execute_manufacturing_tool(ToolHandler handler,
                                                                         const ToolActivity& activity);
    void on_document_replaced();

    std::optional<ConversationMessage> find_stored_message(const std::string& id, std::string* conversation_id = nullptr) const;
    void begin_reply(const std::string& user_message_id);
    void begin_stream(ConversationMessage assistant, const std::string& conversation_id);
    AgentRequest make_agent_request(const ConversationMessage& assistant, const std::string& conversation_id) const;
    nlohmann::json tool_output_json(const ToolActivity& activity) const;
    void complete_stream();
    void fail_stream(AgentError error);
    void handle_agent_tool_call(AgentToolCall call);
    void continue_after_tool(const ToolActivity& activity);
    // Records the agent's one-line restatement of intent on the chat the
    // change came from, so the setup card can say what it heard.
    // Returns whether the write should reach the page now.
    bool remember_setup_intent(const ToolActivity& activity);
    // Records which process settings the agent itself put in force, and to
    // what, so the card can count your hand edits separately from its own.
    bool remember_agent_authored(const ToolActivity& activity);
    // Rebuilds that record from the persisted activities of the document in
    // hand. Without it a restart would relabel every change the agent made as
    // yours -- a loud, wrong claim rather than a quiet missing one.
    void rebuild_agent_authored();
    // The subset of a snapshot's deltas the agent can prove it wrote.
    std::set<std::string> agent_authored_keys(const Workspace::WorkspaceSnapshot& snapshot);
    void begin_tool_followup(const PendingToolContinuation& continuation);
    void start_next_queued_reply();
    void refresh_workspace_identity() const;
    bool agent_busy() const;
    void start_conversation_title(const std::string& conversation_id);
    void cancel_conversation_title();
    void pump_conversation_title();

    Workspace::IWorkspace&           m_workspace;
    // The intent and plan tools' owner: the project document, written through
    // the same persistence that carries conversations, so they travel in the
    // archive and are healed and copied forward with everything else.
    class DocumentProductState final : public IProductState
    {
    public:
        explicit DocumentProductState(ProjectPersistence& persistence) : m_persistence(persistence) {}

        std::vector<IntentField> print_intent() const override { return m_persistence.document().print_intent(); }
        std::vector<IntentField> set_print_intent(const std::vector<IntentField>& fields) override
        {
            auto stored = m_persistence.document().set_print_intent(fields, m_persistence.timestamp());
            m_persistence.flush();
            return stored;
        }
        PlanRecord plan() const override { return m_persistence.document().plan(); }
        PlanRecord set_plan(PlanRecord record) override
        {
            auto stored = m_persistence.document().set_plan(std::move(record), m_persistence.timestamp());
            m_persistence.flush();
            return stored;
        }
        std::vector<Workspace::RegionRecord> regions() const override { return m_persistence.document().regions(); }
        std::vector<Workspace::RegionRecord> set_regions(std::vector<Workspace::RegionRecord> records) override
        {
            auto stored = m_persistence.document().set_regions(std::move(records), m_persistence.timestamp());
            m_persistence.flush();
            return stored;
        }
        void flush_to_project() override { m_persistence.flush(); }
        bool has_printer_facts() const override { return !m_facts_path.empty(); }
        std::vector<Workspace::PrinterFact> printer_facts(const std::string& printer) const override
        {
            return facts()->current(printer);
        }
        std::vector<Workspace::PrinterFact> confirm_printer_facts(
            const std::string& printer, const std::vector<Workspace::FactConfirmation>& confirmations) override
        {
            return facts()->confirm(printer, confirmations);
        }
        void set_facts_path(std::string path) { m_facts_path = std::move(path); }

    private:
        // Opened on first use, so a host that never touches printer facts
        // never reads the file.
        Workspace::PrinterFactsStore* facts() const;

        ProjectPersistence&                                   m_persistence;
        std::string                                           m_facts_path;
        mutable std::unique_ptr<Workspace::PrinterFactsStore> m_facts;
    };

    ProjectPersistence&              m_persistence;
    DocumentProductState             m_product_state;
    ToolExecutionCoordinator         m_tools;
    ToolActivitySubscription         m_tool_activity_subscription;
    std::unique_ptr<Mcp::McpRuntime>   m_mcp;
    AgentServicePtr                  m_agent;
    AgentSetupServicePtr             m_setup;
    Workspace::WorkspaceSubscription m_workspace_subscription;

    SendFn                m_send;
    std::function<void()> m_handshake_listener;
    std::function<void()> m_setup_completed_listener;

    AgentSessionProfile             m_session_profile;
    PageMessageHandler              m_page_message_handler;
    ToolExecutionCoordinator::ExtensionExecutor m_session_tool_executor;
    ToolOutputFormatter             m_session_tool_output;
    std::function<nlohmann::json()> m_session_state_provider;
    ImageThumbnailFn                m_image_thumbnail_maker;
    AgentAvailability m_availability{AgentAvailability::Ready};
    bool              m_dark{false};
    bool              m_handshake{false};
    bool              m_setup_requested{false};

    std::optional<ActiveStream> m_stream;
    struct PendingTitle { std::string conversation_id; std::string text; };
    std::optional<PendingTitle> m_title;
    // The credentials of the check currently in flight, kept so a verified
    // key can be persisted without the page re-sending the secret.
    std::optional<SetupCredentials> m_setup_pending;
    McpConnectSettings              m_mcp_connect;
    McpCliRunner                    m_mcp_cli;
    RevealPathFn                    m_reveal_path;
    std::string                     m_mcp_discovery_path;
    std::string                     m_mcp_edit_tool;
    std::optional<Mcp::ConfigEdit>  m_mcp_edit;
    bool                            m_mcp_busy{false};
    std::deque<std::string>     m_queued_user_message_ids;
    std::map<std::string, PendingToolContinuation> m_tool_continuations;
    // See take_decision_input.
    std::map<std::string, nlohmann::json>          m_decision_inputs;
    // Every process setting the agent has applied, mapped to the value it
    // applied. A key stays the agent's only while that value is still in
    // force: hand-edit the setting and it becomes yours again, because the
    // card describes the project rather than the agent's turns.
    std::map<std::string, std::string> m_agent_authored;
    // The process preset the map above was recorded against. Switching presets
    // changes the baseline every delta is measured from, so the attribution
    // does not carry over. Empty until the first context binds it.
    std::optional<std::string> m_agent_authored_preset;

    std::uint64_t m_next_envelope_id{1};
    std::uint64_t m_messages_sent{0};
    std::uint64_t m_messages_received{0};

    mutable std::uint64_t m_last_session{0};
    mutable std::uint64_t m_last_revision{0};
};

} // namespace Slic3r::GUI::JusPrin::Agent
