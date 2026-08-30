#include "AgentHost.hpp"

#include "DeterministicMockAgent.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;
using Workspace::SelectionStatus;
using Workspace::WorkspaceSnapshot;

const char* role_name(MessageRole role)
{
    return role == MessageRole::User ? "user" : "assistant";
}

const char* state_name(MessageState state)
{
    switch (state) {
    case MessageState::Complete: return "complete";
    case MessageState::Streaming: return "streaming";
    case MessageState::Failed: return "failed";
    case MessageState::Stopped: return "stopped";
    }
    return "complete";
}

const char* availability_name(AgentAvailability availability)
{
    return availability == AgentAvailability::Ready ? "ready" : "unavailable";
}

json error_json(const AgentError& error)
{
    return json{{"code", error.code}, {"message", error.message}, {"retryable", error.retryable}};
}

json message_json(const ConversationMessage& message)
{
    json result{{"id", message.id},
                {"role", role_name(message.role)},
                {"state", state_name(message.state)},
                {"text", message.text},
                {"attempt", message.attempt}};
    if (!message.client_message_id.empty())
        result["clientMessageId"] = message.client_message_id;
    if (!message.in_reply_to.empty())
        result["inReplyTo"] = message.in_reply_to;
    if (message.error)
        result["error"] = error_json(*message.error);
    return result;
}

json context_json(const WorkspaceSnapshot& snapshot)
{
    json plates = json::array();
    for (const Workspace::WorkspacePlate& plate : snapshot.plates) {
        json objects = json::array();
        for (const Workspace::WorkspaceObject& object : plate.objects) {
            const bool selected = std::find(snapshot.selected_objects.begin(), snapshot.selected_objects.end(), object.id) !=
                                  snapshot.selected_objects.end();
            objects.push_back(json{{"id", std::to_string(object.id.value())},
                                   {"name", object.name},
                                   {"instances", object.instances.size()},
                                   {"selected", selected}});
        }
        plates.push_back(json{{"id", std::to_string(plate.id.value())},
                              {"name", plate.name},
                              {"active", plate.active},
                              {"sliced", plate.sliced},
                              {"objects", std::move(objects)}});
    }

    const char* selection_status = "none";
    if (snapshot.selection_status == SelectionStatus::Objects)
        selection_status = "objects";
    else if (snapshot.selection_status == SelectionStatus::Unsupported)
        selection_status = "unsupported";
    json selected_ids = json::array();
    for (Workspace::ObjectId id : snapshot.selected_objects)
        selected_ids.push_back(std::to_string(id.value()));

    return json{{"sessionId", std::to_string(snapshot.session.value())},
                {"revision", snapshot.revision},
                {"projectName", snapshot.setup.project_name},
                {"projectDirty", snapshot.setup.project_dirty},
                {"printer", json{{"preset", snapshot.setup.printer_preset}, {"filament", snapshot.setup.filament_preset}}},
                {"plates", std::move(plates)},
                {"selection", json{{"status", selection_status}, {"objectIds", std::move(selected_ids)}}},
                {"history", json{{"canUndo", snapshot.can_undo}, {"canRedo", snapshot.can_redo}}}};
}

} // namespace

AgentHost::AgentHost(Workspace::IWorkspace& workspace, AgentAvailability availability, bool dark_appearance)
    : m_workspace(workspace), m_availability(availability), m_dark(dark_appearance)
{
    refresh_workspace_identity();
    m_workspace_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        m_last_session  = change.session.value();
        m_last_revision = change.revision;
        if (m_handshake)
            send_context();
    });
}

void AgentHost::set_send(SendFn send)
{
    m_send = std::move(send);
}

void AgentHost::reset_page()
{
    m_handshake = false;
}

void AgentHost::refresh_workspace_identity() const
{
    const WorkspaceSnapshot snapshot = m_workspace.snapshot();
    m_last_session  = snapshot.session.value();
    m_last_revision = snapshot.revision;
}

void AgentHost::send_envelope(const char* type, const std::string& payload_json, const std::string& correlation_id)
{
    if (!m_send)
        return;
    json envelope{{"protocol", Protocol::kName},
                  {"version", Protocol::kVersion},
                  {"id", "h-" + std::to_string(m_next_envelope_id++)},
                  {"type", type},
                  {"sessionId", std::to_string(m_last_session)},
                  {"revision", m_last_revision},
                  {"payload", json::parse(payload_json)}};
    if (!correlation_id.empty())
        envelope["correlationId"] = correlation_id;
    ++m_messages_sent;
    // ensure_ascii so the transport may embed the envelope directly in a
    // JavaScript source string without UTF-8 or line-separator hazards.
    m_send(envelope.dump(-1, ' ', true));
}

void AgentHost::send_bridge_error(const std::string& code, const std::string& message, const std::string& correlation_id)
{
    send_envelope(Protocol::kBridgeError, json{{"code", code}, {"message", message}}.dump(), correlation_id);
}

void AgentHost::send_state(const std::string& correlation_id)
{
    const WorkspaceSnapshot snapshot = m_workspace.snapshot();
    m_last_session  = snapshot.session.value();
    m_last_revision = snapshot.revision;

    json conversation = json::array();
    for (const ConversationMessage& message : m_conversation)
        conversation.push_back(message_json(message));

    json payload{{"agent", json{{"status", availability_name(m_availability)}}},
                 {"appearance", m_dark ? "dark" : "light"},
                 {"conversation", std::move(conversation)},
                 {"streamingMessageId", m_stream ? json(m_stream->message_id) : json(nullptr)},
                 {"context", context_json(snapshot)}};
    send_envelope(Protocol::kState, payload.dump(), correlation_id);
}

void AgentHost::send_context()
{
    const WorkspaceSnapshot snapshot = m_workspace.snapshot();
    m_last_session  = snapshot.session.value();
    m_last_revision = snapshot.revision;
    send_envelope(Protocol::kContext, json{{"context", context_json(snapshot)}}.dump());
}

void AgentHost::on_page_message(const std::string& envelope_json)
{
    ++m_messages_received;

    json envelope = json::parse(envelope_json, nullptr, false);
    if (envelope.is_discarded() || !envelope.is_object()) {
        send_bridge_error("malformed_json", "The page sent an envelope that is not valid JSON.");
        return;
    }
    if (envelope.value("protocol", "") != Protocol::kName) {
        send_bridge_error("wrong_protocol", "The envelope does not carry the jusprin-agent-bridge protocol name.");
        return;
    }
    if (!envelope.contains("type") || !envelope["type"].is_string() || !envelope.contains("id") || !envelope["id"].is_string()) {
        send_bridge_error("malformed_envelope", "The envelope is missing its type or id.");
        return;
    }
    const std::string type        = envelope["type"].get<std::string>();
    const std::string envelope_id = envelope["id"].get<std::string>();
    const std::string payload     = envelope.contains("payload") ? envelope["payload"].dump() : std::string("{}");

    if (type == Protocol::kHello) {
        handle_hello(envelope_id, payload);
        return;
    }

    if (!envelope.contains("version") || !envelope["version"].is_number_integer() ||
        envelope["version"].get<int>() != Protocol::kVersion) {
        send_bridge_error("unsupported_version", "The envelope version does not match the negotiated protocol version.",
                          envelope_id);
        return;
    }
    if (!m_handshake) {
        send_bridge_error("handshake_required", "A hello handshake is required before other messages.", envelope_id);
        return;
    }

    if (type == Protocol::kStateRequest)
        send_state(envelope_id);
    else if (type == Protocol::kUserMessage)
        handle_user_message(envelope_id, payload);
    else if (type == Protocol::kStopGeneration)
        handle_stop(payload);
    else if (type == Protocol::kRetryMessage)
        handle_retry(envelope_id, payload);
    else
        send_bridge_error("unknown_type", "The message type \"" + type + "\" is not part of this protocol version.", envelope_id);
}

void AgentHost::handle_hello(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    std::vector<int> versions;
    if (payload.is_object() && payload.contains("protocolVersions") && payload["protocolVersions"].is_array())
        for (const json& value : payload["protocolVersions"])
            if (value.is_number_integer())
                versions.push_back(value.get<int>());

    if (std::find(versions.begin(), versions.end(), Protocol::kVersion) == versions.end()) {
        m_handshake = false;
        send_envelope(Protocol::kHelloReject,
                      json{{"supportedVersions", json::array({Protocol::kVersion})},
                           {"message", "This build speaks jusprin-agent-bridge version 1 only."}}
                          .dump(),
                      envelope_id);
        return;
    }

    // Capabilities are informational in version 1: the host reports what it
    // can do and the page hides what the host did not claim.
    m_handshake = true;
    json capabilities = json::array();
    for (const std::string& capability : Protocol::capabilities())
        capabilities.push_back(capability);
    send_envelope(Protocol::kHelloAck,
                  json{{"version", Protocol::kVersion},
                       {"capabilities", std::move(capabilities)},
                       {"agent", json{{"status", availability_name(m_availability)}}},
                       {"appearance", m_dark ? "dark" : "light"}}
                      .dump(),
                  envelope_id);
    send_state();
    if (m_handshake_listener)
        m_handshake_listener();
}

void AgentHost::handle_user_message(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    if (!payload.is_object() || !payload.contains("clientMessageId") || !payload["clientMessageId"].is_string() ||
        !payload.contains("text") || !payload["text"].is_string() || payload["text"].get<std::string>().empty()) {
        send_bridge_error("invalid_payload", "user_message requires a clientMessageId and non-empty text.", envelope_id);
        return;
    }
    const std::string client_id = payload["clientMessageId"].get<std::string>();
    const std::string text      = payload["text"].get<std::string>();

    // A reconnect or reload may resend a message the host already owns; the
    // stable client ID makes that a duplicate acknowledgement, not a repeat.
    if (const auto existing = m_client_message_ids.find(client_id); existing != m_client_message_ids.end()) {
        if (const ConversationMessage* message = find_message(existing->second))
            send_envelope(Protocol::kMessageAdded, json{{"message", message_json(*message)}}.dump(), envelope_id);
        return;
    }

    ConversationMessage message;
    message.id                = "m-" + std::to_string(m_next_message_id++);
    message.role              = MessageRole::User;
    message.state             = MessageState::Complete;
    message.text              = text;
    message.client_message_id = client_id;
    m_client_message_ids.emplace(client_id, message.id);
    m_conversation.emplace_back(std::move(message));
    const std::string user_message_id = m_conversation.back().id;
    send_envelope(Protocol::kMessageAdded, json{{"message", message_json(m_conversation.back())}}.dump(), envelope_id);

    if (m_stream) {
        // The page disables sending while a reply streams; if a message
        // arrives anyway, answer it after the current stream finishes.
        m_queued_user_message_ids.push_back(user_message_id);
        return;
    }
    begin_reply(user_message_id);
}

void AgentHost::handle_stop(const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    const std::string message_id = payload.is_object() ? payload.value("messageId", "") : std::string();
    // Stopping an already-finished stream is a benign race; ignore silently.
    if (!m_stream || m_stream->message_id != message_id)
        return;
    ConversationMessage* message = find_message(message_id);
    m_stream.reset();
    if (message != nullptr) {
        message->state = MessageState::Stopped;
        send_envelope(Protocol::kAssistantStopped, json{{"messageId", message_id}}.dump());
    }
    start_next_queued_reply();
}

void AgentHost::handle_retry(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    const std::string    message_id = payload.is_object() ? payload.value("messageId", "") : std::string();
    ConversationMessage* message    = find_message(message_id);
    if (message == nullptr || message->role != MessageRole::Assistant ||
        (message->state != MessageState::Failed && message->state != MessageState::Stopped)) {
        send_bridge_error("invalid_retry", "Only a failed or stopped assistant message can be retried.", envelope_id);
        return;
    }
    if (m_stream) {
        send_bridge_error("busy", "Another reply is currently streaming.", envelope_id);
        return;
    }
    ++message->attempt;
    begin_stream(*message);
}

ConversationMessage* AgentHost::find_message(const std::string& id)
{
    for (ConversationMessage& message : m_conversation)
        if (message.id == id)
            return &message;
    return nullptr;
}

const ConversationMessage* AgentHost::find_message(const std::string& id) const
{
    for (const ConversationMessage& message : m_conversation)
        if (message.id == id)
            return &message;
    return nullptr;
}

void AgentHost::begin_reply(const std::string& user_message_id)
{
    if (m_availability != AgentAvailability::Ready) {
        ConversationMessage failed;
        failed.id          = "m-" + std::to_string(m_next_message_id++);
        failed.role        = MessageRole::Assistant;
        failed.state       = MessageState::Failed;
        failed.in_reply_to = user_message_id;
        failed.error       = AgentError{"agent_unavailable", "The Agent service is not available.", true};
        m_conversation.emplace_back(failed);
        send_envelope(Protocol::kAssistantStarted, json{{"messageId", failed.id}, {"inReplyTo", user_message_id}, {"attempt", 1}}.dump());
        send_envelope(Protocol::kAssistantFailed, json{{"messageId", failed.id}, {"error", error_json(*failed.error)}}.dump());
        return;
    }

    ConversationMessage assistant;
    assistant.id          = "m-" + std::to_string(m_next_message_id++);
    assistant.role        = MessageRole::Assistant;
    assistant.state       = MessageState::Streaming;
    assistant.in_reply_to = user_message_id;
    m_conversation.emplace_back(std::move(assistant));
    begin_stream(m_conversation.back());
}

void AgentHost::begin_stream(ConversationMessage& assistant)
{
    const ConversationMessage* user = find_message(assistant.in_reply_to);
    const DeterministicMockAgent::Reply reply =
        DeterministicMockAgent::reply_for(user != nullptr ? user->text : std::string(), assistant.attempt, m_workspace.snapshot());

    assistant.state = MessageState::Streaming;
    assistant.text.clear();
    assistant.error.reset();

    ActiveStream stream;
    stream.message_id = assistant.id;
    stream.chunks.assign(reply.chunks.begin(), reply.chunks.end());
    stream.error = reply.error;
    m_stream     = std::move(stream);

    send_envelope(Protocol::kAssistantStarted,
                  json{{"messageId", assistant.id}, {"inReplyTo", assistant.in_reply_to}, {"attempt", assistant.attempt}}.dump());
}

void AgentHost::pump_stream()
{
    if (!m_stream || !m_handshake)
        return;

    ConversationMessage* message = find_message(m_stream->message_id);
    if (message == nullptr) {
        m_stream.reset();
        return;
    }

    if (!m_stream->chunks.empty()) {
        const std::string chunk = std::move(m_stream->chunks.front());
        m_stream->chunks.pop_front();
        message->text += chunk;
        send_envelope(Protocol::kAssistantDelta,
                      json{{"messageId", message->id}, {"seq", m_stream->next_seq++}, {"text", chunk}}.dump());
        return;
    }
    finish_stream();
}

void AgentHost::finish_stream()
{
    ConversationMessage* message = find_message(m_stream->message_id);
    const std::optional<AgentError> error = m_stream->error;
    m_stream.reset();
    if (message != nullptr) {
        if (error) {
            message->state = MessageState::Failed;
            message->error = error;
            send_envelope(Protocol::kAssistantFailed, json{{"messageId", message->id}, {"error", error_json(*error)}}.dump());
        } else {
            message->state = MessageState::Complete;
            send_envelope(Protocol::kAssistantCompleted, json{{"messageId", message->id}}.dump());
        }
    }
    start_next_queued_reply();
}

void AgentHost::start_next_queued_reply()
{
    if (m_stream || m_queued_user_message_ids.empty())
        return;
    const std::string next = m_queued_user_message_ids.front();
    m_queued_user_message_ids.pop_front();
    begin_reply(next);
}

void AgentHost::set_appearance(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    if (m_handshake)
        send_envelope(Protocol::kAppearance, json{{"appearance", m_dark ? "dark" : "light"}}.dump());
}

void AgentHost::set_availability(AgentAvailability availability)
{
    if (m_availability == availability)
        return;
    m_availability = availability;
    if (m_handshake)
        send_envelope(Protocol::kAgentStatus, json{{"status", availability_name(m_availability)}}.dump());
}

} // namespace Slic3r::GUI::JusPrin::Agent
