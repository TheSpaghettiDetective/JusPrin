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
#include "ModelCatalog.hpp"
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

// Where a chat's model choice becomes an agent. The wx side supplies it, from
// the keys setup stored and the providers' own model lists; without one (the
// printer panel) every chat uses the agent the host was given.
class IChatModelSource
{
public:
    virtual ~IChatModelSource() = default;
    // Providers with a stored key, in setup's order.
    virtual std::vector<std::string> configured_providers() const = 0;
    // The provider a chat without a choice of its own talks to.
    virtual std::string default_provider() const = 0;
    // A ready agent for this choice, or nullptr when its provider has no key.
    virtual AgentServicePtr make_agent(const ChatModelChoice& choice) = 0;
    // Asks the provider for its models; `done` is called later, once, on the
    // thread the host runs on, and not at all after the source is replaced.
    using ModelsDone = std::function<void(bool ok, std::vector<ModelOption> models)>;
    virtual void list_models(const std::string& provider, ModelsDone done) = 0;
    // For an account provider, the wire format the account's model list last
    // gave this model; empty when it gave none, and for other providers.
    virtual std::string model_format(const std::string& provider, const std::string& model) const
    {
        (void) provider;
        (void) model;
        return {};
    }
};

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
    // A session with tools of its own saves its own presets; in the project
    // chat the person saves one, from the card the Agent's preview proposes.
    // The surface the person opens the next project from, for the line a
    // reopened calibration test starts with.
    void set_project_opened_from(std::string surface) { m_opened_from = std::move(surface); }
    // The calibration project just opened (by the app after the popup, or by
    // calibration_generate from the person's own project): it is marked as
    // one, its chat is named for it and opens with the handover line, and,
    // when start is set, the Agent's first turn starts with the calibration
    // skill in force.
    void open_calibration_chat(const CalibrationHandover& handover, bool start);
    void set_session_profile(AgentSessionProfile profile)
    {
        m_session_profile = std::move(profile);
        m_tools.set_saves_need_approval(m_session_profile.tool_names.empty());
    }
    // The model answers next, from the conversation as it stands. A file
    // report supplies its note ID so that its data belongs to this turn;
    // other app-initiated turns have no message of their own.
    void start_turn(const std::string& source_message_id = {});
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
    // Shell navigation is transient view state. It travels with a full state
    // snapshot so a WebView reload can restore the correct Back action.
    void set_navigation_state_provider(std::function<nlohmann::json()> provider)
    {
        m_navigation_state_provider = std::move(provider);
    }
    void set_restore_points_provider(std::function<nlohmann::json()> provider)
    {
        m_restore_points_provider = std::move(provider);
    }
    void refresh_page_state() { send_state(); }
    // Decodes an image attachment's raw bytes into a small thumbnail data
    // URL, for the composer/thread preview only -- the model still gets the
    // untouched original, read separately for its own context. Empty (the
    // default, and what every GUI-free test leaves it) means no preview:
    // decoding an arbitrary image needs wx, which this host's own build
    // does not link, so the real app wires this after construction and the
    // tests wire a fake.
    using ImageThumbnailFn = std::function<std::string(const std::string& bytes)>;
    void set_image_thumbnail_maker(ImageThumbnailFn maker) { m_image_thumbnail_maker = std::move(maker); }
    // Shrinks an attached image before the model is sent it: the new mime
    // type and bytes, or nothing to send the original. Injected for the same
    // reason as the thumbnail maker: it needs wx to decode.
    using ImageForModelFn = std::function<std::optional<std::pair<std::string, std::string>>(const std::string& bytes)>;
    void set_image_for_model(ImageForModelFn shrink) { m_image_for_model = std::move(shrink); }
    // Sends one envelope of a type this protocol declares. The subject uses
    // it to push its own state between snapshots.
    void send_page_envelope(const std::string& type, const nlohmann::json& payload);
    // Runs the session's own tools inside the coordinator's state machine.
    // Anything it leaves unhandled stays with the host.
    void set_session_tool_executor(ToolExecutionCoordinator::ExtensionExecutor executor)
    {
        m_session_tool_executor = std::move(executor);
    }
    // Checks a call to the session's own tools before it runs.
    void set_session_tool_preflight(ToolExecutionCoordinator::ExtensionPreflight preflight);
    // What the model reads back for a call to the session's own tools, in
    // place of the host's envelope; nullopt keeps the host's.
    using ToolOutputFormatter = std::function<std::optional<nlohmann::json>(const ToolActivity& activity)>;
    void set_session_tool_output(ToolOutputFormatter formatter) { m_session_tool_output = std::move(formatter); }
    // Appends a message the agent is shown as having said, without asking the
    // model for it: a panel whose opening line is always the same. Returns
    // its id so the owner can anchor what it draws underneath.
    std::string post_assistant_message(const std::string& text);

    // Invoked after every successful hello handshake (initial load and every
    // reload); the owner uses it to cancel its connection deadline.
    void set_handshake_listener(std::function<void()> listener) { m_handshake_listener = std::move(listener); }
    // A completed, failed, or stopped reply is a document durability boundary.
    void set_turn_boundary_callback(std::function<void()> callback) { m_turn_boundary_callback = std::move(callback); }
    // Managed projects supply native model-version operations. Temporary
    // printer conversations intentionally leave these unset.
    void set_chat_checkpoint_callbacks(std::function<std::string()> pin_current,
                                       std::function<std::string()> current_version,
                                       std::function<bool(const std::string&)> version_exists,
                                       std::function<bool()> save_document,
                                       std::function<bool(const std::string&, const std::string&)> restore)
    {
        m_pin_chat_version = std::move(pin_current);
        m_current_chat_version = std::move(current_version);
        m_chat_version_exists = std::move(version_exists);
        m_save_chat_document = std::move(save_document);
        m_restore_chat_version = std::move(restore);
    }
    bool checkpoint_active_chat();

    // Call when the page starts (re)loading; the host requires a new
    // handshake before any other message and pauses stream delivery until the
    // page reconnects. Conversation state is unaffected.
    void reset_page();

    // One JSON envelope from the page.
    void on_page_message(const std::string& envelope_json);

    void set_appearance(bool dark);
    void set_availability(AgentAvailability availability);
    void set_agent(AgentServicePtr agent, AgentAvailability availability);
    // Lets each chat choose its provider, model and thinking level: the
    // composer's pickers, switchable between any two turns. The choice is
    // kept with the chat; each turn is started on an agent built for it.
    void set_chat_model_source(std::shared_ptr<IChatModelSource> source);
    // Asks the page a Confirmation question (Protocol::kConfirm); held until
    // the page is connected. A reload drops a question on screen; the person
    // asks again.
    struct Confirmation
    {
        std::string request_id, title, body, confirm, cancel;
    };
    void ask_confirmation(const Confirmation& question);

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
    // Offers the connections `other` offers: the same helper and the same
    // discovery file, for a host that runs no MCP server of its own (the
    // Preferences window's).
    void share_mcp_connect(const AgentHost& other)
    {
        m_mcp_connect        = other.m_mcp_connect;
        m_mcp_discovery_path = other.m_mcp_discovery_path;
    }
    void set_mcp_cli_runner(McpCliRunner runner);
    // Reveals one file in the desktop's file manager. The host stays GUI-free,
    // so the wx side supplies the implementation the way it does the CLI
    // runner above.
    using RevealPathFn = std::function<void(const std::string& path)>;
    void set_reveal_path_handler(RevealPathFn reveal);
    // Shows the desktop's file dialog for one kind of attachment and calls
    // `done` with the chosen paths (UTF-8), empty when the person cancels.
    // GUI-free like the reveal handler: the wx side supplies the dialog.
    struct FilePickerRequest
    {
        std::string              description; // e.g. "Photo or image"
        std::vector<std::string> extensions;  // lower case, without the dot
    };
    using FilePickerFn = std::function<void(const FilePickerRequest& request,
                                            std::function<void(std::vector<std::string> paths)> done)>;
    void set_file_picker(FilePickerFn picker);

    // The active conversation's messages, straight from the document.
    std::vector<ConversationMessage> conversation() const;

    // Appends one host-authored note to the active conversation: a short
    // factual line about something the shell just changed. It is durable like
    // any message, renders without a bubble, starts no reply, and is never
    // sent to the model. `swatch`, when given, is a colour ("#RRGGBB") the
    // line leads with -- the filament a change landed on. Returns the note's ID.
    std::string post_note(const std::string& text, const std::string& swatch = {});

    // A file the person opened or imported has finished loading. The report
    // joins the conversation as a note the model reads and the page does not
    // draw, and the Agent speaks first about it. When the Agent can't, the
    // page draws the report's messages as a card instead. A reopened project
    // that brought its conversation back is spoken about only when OrcaSlicer
    // said something while loading it.
    void on_file_loaded(const Workspace::LoadReport& report);
    // A calibration test the Agent generated: its project's chat opens with
    // the app's note of what OrcaSlicer made, and the Agent speaks first.
    // calibration_generate's test, made or not: the project's record, and
    // the handover turn of a project the tool opened.
    void on_calibration_finished(bool made, bool by_agent);
    // The receipt for a preset the person saved from a proposal, and the
    // Agent's turn that follows it.
    void on_proposal_saved(const std::string& apply_action_id);
    // A calibration test reopened without its steps (board 7): the app's
    // line, and the Agent's turn that offers to make it again.
    void tell_reopened_test();

    // A message the person sends by pressing a button outside the page (the
    // print-issue bubble on the canvas): the same durable user message a typed
    // one becomes, answered by the same reply path, with `context_json`
    // attached for the model. Unlike a typed message it leaves the composer's
    // draft alone. `client_message_id` makes a repeated press one message.
    // On Refused, `refusal` is the same code the page's own send would get.
    enum class SubmitResult : std::uint8_t { Started, Queued, Duplicate, Refused };
    SubmitResult submit_issue_message(const std::string& client_message_id, const std::string& text,
                                      const std::string& context_json, std::string* refusal = nullptr);
    // A reply is being written or a tool is running.
    bool busy() const { return agent_busy(); }

    // Work the application runs outside the chat: a slice, an arrange, an
    // export. An entry is saved with the project at the place in the timeline
    // where the work started, shown with its progress while it runs, and
    // completed in place. `begin_work` returns the id the other calls take.
    std::string begin_work(const std::string& kind, const std::string& title, bool cancellable);
    // `total` zero means the amount is not known.
    void        update_work(const std::string& id, int current, int total, const std::string& detail = {});
    // `state` is done, cancelled or failed. Unknown ids are ignored: the
    // project that owned the entry may have been replaced meanwhile.
    void        finish_work(const std::string& id, const std::string& state, const std::string& detail = {});
    // The page's cancel button on a running entry.
    void        set_work_cancel_handler(std::function<void(const std::string& id)> handler) { m_work_cancel = std::move(handler); }

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
    // Why no user message can be taken now, whoever sends it.
    std::optional<AgentError> user_message_refusal() const;
    // Makes `message` the next user message of the active chat and starts its
    // reply, or queues it behind the one being written. Returns whether the
    // reply started.
    bool accept_user_message(ConversationMessage message, const std::string& correlation_id, bool from_composer);
    void handle_stop(const std::string& payload_json);
    void handle_retry(const std::string& envelope_id, const std::string& payload_json);
    void handle_tool_cancel(const std::string& envelope_id, const std::string& payload_json);
    void handle_save_proposal(const std::string& envelope_id, const std::string& payload_json);
    void handle_undo_receipt(const std::string& envelope_id, const std::string& payload_json);
    void handle_work_cancel(const std::string& envelope_id, const std::string& payload_json);
    void send_work(const WorkEntry& work);

    // What is known about a running entry and is not saved.
    struct LiveWork
    {
        int         current{0}, total{0};
        std::string detail;
        bool        cancellable{false};
    };
    std::map<std::string, LiveWork>              m_live_work;
    std::function<void(const std::string& id)>   m_work_cancel;
    void handle_create_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_switch_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_restore_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_rename_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_delete_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_draft_update(const std::string& payload_json);
    void handle_attach_file(const std::string& envelope_id, const std::string& payload_json);
    void handle_pick_attachment(const std::string& envelope_id, const std::string& payload_json);
    void handle_set_chat_model(const std::string& envelope_id, const std::string& payload_json);
    // The active chat's choice, or the default provider's default model.
    ChatModelChoice chat_model_choice(const std::string& conversation_id) const;
    ChatModelChoice default_chat_model() const;
    // What the agent the host was handed (by its owner or by setup) runs:
    // the default choice. Nothing without a source or an agent.
    std::optional<ChatModelChoice> installed_agent_choice() const;
    nlohmann::json  chat_model_json(const std::string& conversation_id) const;
    // Fetches the lists not yet held, then sends what is known.
    void request_model_lists();
    void send_model_catalog();
    // Puts m_agent on the chat's choice before a turn; false when that
    // provider has no key (the turn then fails with a reason).
    bool use_chat_model(const std::string& conversation_id);
    // Checks and stores one file's contents on a staged record, then records
    // it and tells the page. Shared by files the page read and files the
    // native picker chose, so both are judged by the same rules.
    void store_file_attachment(AttachmentRecord record, const std::string& bytes, const std::string& correlation_id);
    void handle_remove_attachment(const std::string& envelope_id, const std::string& payload_json);
    void handle_setup_check_key(const std::string& envelope_id, const std::string& payload_json);
    void handle_setup_cancel();
    void handle_setup_remove_key(const std::string& envelope_id, const std::string& payload_json);
    void handle_account_sign_in(const std::string& envelope_id, bool stored);
    void handle_account_cancel();
    void pump_account();
    void send_account_status(const std::string& phase, const std::string& correlation_id = {},
                             const std::optional<AgentError>& error = std::nullopt, bool expired = false,
                             const std::string& warning = {});
    // The stored keys as setup_keys describes them; nothing before the
    // handshake or without a setup service.
    void send_setup_keys(const std::string& correlation_id = {});
    // The provider refused the key the current agent was built from.
    void note_key_rejected();
    void handle_mcp_catalog(const std::string& envelope_id);
    void handle_settings_catalog(const std::string& envelope_id);
    void handle_settings_action(const std::string& envelope_id, const std::string& payload_json);
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
    void on_document_replaced();

    std::optional<ConversationMessage> find_stored_message(const std::string& id, std::string* conversation_id = nullptr) const;
    void begin_reply(const std::string& user_message_id);
    void begin_stream(ConversationMessage assistant, const std::string& conversation_id);
    AgentRequest make_agent_request(const ConversationMessage& assistant, const std::string& conversation_id) const;
    // The page's skill for `trigger`, as one request gets it now, or nothing
    // when the page sent none for it or the conversation has no workspace.
    std::optional<AgentSkillUse> fire_skill(const std::string& trigger, const std::string& conversation_id,
                                            const Workspace::WorkspaceSnapshot& workspace) const;
    // The slice-failed skill, when `workspace` shows a plate whose slice
    // failed and no request has carried that failure yet. Fired once a failure.
    std::optional<AgentSkillUse> fire_on_new_slice_failure(const std::string& conversation_id,
                                                           const Workspace::WorkspaceSnapshot& workspace) const;
    // The print-issues skill, when `workspace` shows a plate whose slice
    // warned about something no request has carried yet, or when `chip` (a
    // message from a print-issue chip). Fired once per new set of warnings:
    // either way, the warnings in `workspace` count as told.
    std::optional<AgentSkillUse> fire_print_issues(const std::string& conversation_id,
                                                   const Workspace::WorkspaceSnapshot& workspace, bool chip) const;
    // A purpose is recorded once the chat has a setup title or an intent
    // field has an answer; a question asked and not answered is none.
    bool purpose_recorded(const std::string& conversation_id) const;
    // Whether the setup in force differs from the one the model was last
    // asked to check against the recorded purpose (ProjectStateDocument::
    // purpose_setup); false with no purpose or nothing checked yet.
    bool setup_unchecked(const std::string& conversation_id, const Workspace::WorkspaceSnapshot& workspace) const;
    // The model is asked to check the setup in force now: later changes are
    // measured from it.
    void mark_setup_checked(const Workspace::WorkspaceSnapshot& workspace);
    void handle_skills(const std::string& envelope_id, const std::string& payload_json);
    ToolExecutionCoordinator::ExtensionResult read_skill(const ToolActivity& activity) const;
    nlohmann::json tool_output_json(const ToolActivity& activity) const;
    void complete_stream();
    void fail_stream(AgentError error);
    void handle_agent_tool_call(AgentToolCall call);
    void continue_after_tool(const ToolActivity& activity);
    void begin_tool_followup(const PendingToolContinuation& continuation);
    void start_next_queued_reply();
    void refresh_workspace_identity() const;
    bool agent_busy() const;
    bool transition_busy() const;
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

        std::optional<std::string> setup_title(const std::string& message_id) const override
        {
            const std::string conversation_id = m_persistence.document().conversation_of_message(message_id);
            if (conversation_id.empty())
                return std::nullopt;
            return m_persistence.document().setup_intent(conversation_id);
        }
        bool set_setup_title(const std::string& message_id, const std::string& title) override
        {
            const std::string conversation_id = m_persistence.document().conversation_of_message(message_id);
            if (conversation_id.empty() || !m_persistence.document().set_setup_intent(conversation_id, title))
                return false;
            m_persistence.flush();
            return true;
        }
        nlohmann::json purpose_setup() const override { return m_persistence.document().purpose_setup(); }
        void           set_purpose_setup(nlohmann::json setup) override
        {
            m_persistence.document().set_purpose_setup(std::move(setup));
            m_persistence.flush();
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
    std::function<void()> m_turn_boundary_callback;
    std::function<std::string()> m_pin_chat_version;
    std::function<std::string()> m_current_chat_version;
    std::function<bool(const std::string&)> m_chat_version_exists;
    std::function<bool()> m_save_chat_document;
    std::function<bool(const std::string&, const std::string&)> m_restore_chat_version;
    bool m_chat_transition_blocked{false};

    // The project chat's instructions arrive from its page with
    // project_instructions; until then, an app-started turn waits in
    // m_queued_user_message_ids.
    AgentSessionProfile             m_session_profile;
    // The project skills, written by the page (AgentUI/src/skills) and sent
    // with the skills message, sorted by name, at most one for each trigger.
    // Until they arrive a project turn waits, as it does for the instructions.
    std::vector<AgentSkill>         m_skills;
    bool                            m_skills_received{false};
    // The slice failures (Workspace::SliceFailure::sequence) a request has
    // already carried with the slice-failed skill.
    mutable std::set<std::uint64_t> m_slice_failures_told;
    // The slice warnings (Workspace::SliceWarnings::sequence) a request has
    // already carried with the print-issues skill.
    mutable std::set<std::uint64_t> m_slice_warnings_told;
    // Load reports of projects the Agent opened, posted to the opened
    // project's chat on the next timer tick, once its document is in place.
    std::deque<Workspace::LoadReport> m_agent_opened_reports;
    // The same for calibration tests the Agent generated, by their summary.
    // The handover note of a calibration project calibration_generate
    // opened, whose turn starts once the test is made.
    std::string                     m_handover_turn;
    // Saves the person approved that succeeded, by the apply's action id,
    // receipted on the next timer tick, outside the coordinator's callback.
    std::deque<std::string>         m_saved_proposals;
    // A project's document came in; the next tick checks whether it is a
    // calibration test that lost its steps.
    bool                            m_check_reopened_test{false};
    // Where the person opened the next project from ("Home"), said by the
    // surface that opens it; used once.
    std::string                     m_opened_from;
    // Runs the chat's slice_start began that have ended, by that call's
    // action id, told on a timer tick once the turn that started them has
    // ended (slice_ended). A tool result the model reads before then carries
    // the outcome, and drops them.
    std::deque<std::string>         m_ended_slices;
    // False while the turn that started the run goes on: ask again later.
    bool slice_ended(const std::string& action_id);
    PageMessageHandler              m_page_message_handler;
    std::function<nlohmann::json()> m_restore_points_provider;
    ToolExecutionCoordinator::ExtensionExecutor m_session_tool_executor;
    ToolOutputFormatter             m_session_tool_output;
    std::function<nlohmann::json()> m_session_state_provider;
    std::function<nlohmann::json()> m_navigation_state_provider;
    ImageThumbnailFn                m_image_thumbnail_maker;
    ImageForModelFn                 m_image_for_model;
    AgentAvailability m_availability{AgentAvailability::Ready};
    bool              m_dark{false};
    bool              m_handshake{false};
    std::string       m_held_confirmation; // a kConfirm payload asked before the handshake

    std::optional<ActiveStream> m_stream;
    struct PendingTitle { std::string conversation_id; std::string text; };
    std::optional<PendingTitle> m_title;
    // The credentials of the check currently in flight, kept so a verified
    // key can be persisted without the page re-sending the secret.
    std::optional<SetupCredentials> m_setup_pending;
    // Providers that refused their stored key in this session, each with the
    // hint of the key refused: a key replaced since, here or in another
    // window, no longer matches and is not reported as refused.
    std::map<std::string, std::string> m_rejected_keys;
    McpConnectSettings              m_mcp_connect;
    McpCliRunner                    m_mcp_cli;
    RevealPathFn                    m_reveal_path;
    // Per-chat models: the source, the choice the current m_agent was built
    // for (none for the agent the host was handed), and each configured
    // provider's list as it arrives.
    struct ProviderModels
    {
        enum class State { Loading, Ready, Failed } state{State::Loading};
        std::vector<ModelOption> models;
    };
    std::shared_ptr<IChatModelSource>       m_model_source;
    std::optional<ChatModelChoice>          m_agent_choice;
    std::map<std::string, ProviderModels>   m_models;
    FilePickerFn                    m_file_picker;
    std::string                     m_mcp_discovery_path;
    std::string                     m_mcp_edit_tool;
    std::optional<Mcp::ConfigEdit>  m_mcp_edit;
    bool                            m_mcp_busy{false};
    std::deque<std::string>     m_queued_user_message_ids;
    std::map<std::string, PendingToolContinuation> m_tool_continuations;

    std::uint64_t m_next_envelope_id{1};
    std::uint64_t m_messages_sent{0};
    std::uint64_t m_messages_received{0};

    mutable std::uint64_t m_last_session{0};
    mutable std::uint64_t m_last_revision{0};
};

} // namespace Slic3r::GUI::JusPrin::Agent
