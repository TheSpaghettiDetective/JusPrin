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
#include "ProjectPersistence.hpp"
#include "ToolExecutionCoordinator.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class AgentHost
{
public:
    using SendFn = std::function<void(const std::string& envelope_json)>;

    AgentHost(Workspace::IWorkspace& workspace,
              ProjectPersistence&    persistence,
              AgentAvailability      availability,
              bool                   dark_appearance);
    ~AgentHost();

    AgentHost(const AgentHost&) = delete;
    AgentHost& operator=(const AgentHost&) = delete;

    void set_send(SendFn send);

    // Invoked after every successful hello handshake (initial load and every
    // reload); the owner uses it to cancel its connection deadline.
    void set_handshake_listener(std::function<void()> listener) { m_handshake_listener = std::move(listener); }

    // Call when the page starts (re)loading; the host requires a new
    // handshake before any other message and pauses stream delivery until the
    // page reconnects. Conversation state is unaffected.
    void reset_page();

    // One JSON envelope from the page.
    void on_page_message(const std::string& envelope_json);

    void set_appearance(bool dark);
    void set_availability(AgentAvailability availability);

    bool              handshake_complete() const { return m_handshake; }
    bool              stream_active() const { return m_stream.has_value(); }
    AgentAvailability availability() const { return m_availability; }

    // Emits the next assistant delta (or the terminal event) when a stream is
    // active and the page is connected. The owner paces this from a timer;
    // tests call it directly.
    void pump_stream();

    // Advances tool execution one deterministic tick while the page is
    // connected, and gives dirty document state its throttled flush.
    void pump_tools();

    ToolExecutionCoordinator&       tools() { return m_tools; }
    const ToolExecutionCoordinator& tools() const { return m_tools; }
    ProjectPersistence&             persistence() { return m_persistence; }

    // The active conversation's messages, straight from the document.
    std::vector<ConversationMessage> conversation() const;

    // Diagnostics for the internal-connection error surface.
    std::uint64_t messages_sent() const { return m_messages_sent; }
    std::uint64_t messages_received() const { return m_messages_received; }

private:
    struct ActiveStream
    {
        std::string                conversation_id;
        ConversationMessage        message; // authoritative in-progress copy
        std::deque<std::string>    chunks;
        std::optional<AgentError>  error;
        std::optional<ToolRequest> tool; // proposed when the stream completes
        int                        next_seq{0};
    };

    void send_envelope(const char* type, const std::string& payload_json, const std::string& correlation_id = {});
    void send_bridge_error(const std::string& code, const std::string& message, const std::string& correlation_id = {});
    void send_state(const std::string& correlation_id = {});
    void send_context();

    void handle_hello(const std::string& envelope_id, const std::string& payload_json);
    void handle_user_message(const std::string& envelope_id, const std::string& payload_json);
    void handle_stop(const std::string& payload_json);
    void handle_retry(const std::string& envelope_id, const std::string& payload_json);
    void handle_tool_decision(const std::string& envelope_id, const std::string& payload_json);
    void handle_tool_cancel(const std::string& envelope_id, const std::string& payload_json);
    void handle_create_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_switch_conversation(const std::string& envelope_id, const std::string& payload_json);
    void handle_revert_to_revision(const std::string& envelope_id, const std::string& payload_json);
    void handle_draft_update(const std::string& payload_json);
    void handle_attach_file(const std::string& envelope_id, const std::string& payload_json);
    void handle_remove_attachment(const std::string& envelope_id, const std::string& payload_json);
    void send_attachment_updated(const AttachmentRecord& record, const std::string& correlation_id = {});
    std::string attachment_preview_data_url(const AttachmentRecord& record) const;
    void send_tool_activity(const ToolActivity& activity, const std::string& correlation_id = {});
    void on_document_replaced();

    std::optional<ConversationMessage> find_stored_message(const std::string& id, std::string* conversation_id = nullptr) const;
    void begin_reply(const std::string& user_message_id);
    void begin_stream(ConversationMessage assistant, const std::string& conversation_id);
    void finish_stream();
    void start_next_queued_reply();
    void refresh_workspace_identity() const;

    Workspace::IWorkspace&           m_workspace;
    ProjectPersistence&              m_persistence;
    ToolExecutionCoordinator         m_tools;
    Workspace::WorkspaceSubscription m_workspace_subscription;

    SendFn                m_send;
    std::function<void()> m_handshake_listener;
    AgentAvailability m_availability{AgentAvailability::Ready};
    bool              m_dark{false};
    bool              m_handshake{false};

    std::optional<ActiveStream> m_stream;
    std::deque<std::string>     m_queued_user_message_ids;

    std::uint64_t m_next_envelope_id{1};
    std::uint64_t m_messages_sent{0};
    std::uint64_t m_messages_received{0};

    mutable std::uint64_t m_last_session{0};
    mutable std::uint64_t m_last_revision{0};
};

} // namespace Slic3r::GUI::JusPrin::Agent
