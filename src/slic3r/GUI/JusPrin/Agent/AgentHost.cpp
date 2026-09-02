#include "AgentHost.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <set>

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
    if (!message.attachment_ids.empty())
        result["attachments"] = message.attachment_ids;
    return result;
}

const char* tool_state_name(ToolState state)
{
    switch (state) {
    case ToolState::Pending: return "pending";
    case ToolState::Approved: return "approved";
    case ToolState::Running: return "running";
    case ToolState::Succeeded: return "succeeded";
    case ToolState::Failed: return "failed";
    case ToolState::Cancelled: return "cancelled";
    case ToolState::Rejected: return "rejected";
    }
    return "pending";
}

const char* action_class_name(ActionClass action_class)
{
    switch (action_class) {
    case ActionClass::ReadOnly: return "read_only";
    case ActionClass::Mutation: return "mutation";
    case ActionClass::Destructive: return "destructive";
    }
    return "read_only";
}

json parsed_or_object(const std::string& text)
{
    json value = json::parse(text, nullptr, false);
    return value.is_discarded() ? json::object() : value;
}

json activity_json(const ToolActivity& activity)
{
    json result{{"actionId", activity.action_id},
                {"correlationId", activity.correlation_id},
                {"server", activity.server},
                {"tool", activity.tool},
                {"title", activity.title},
                {"arguments", parsed_or_object(activity.arguments_json)},
                {"actionClass", action_class_name(activity.action_class)},
                {"requiresApproval", activity.requires_approval},
                {"sessionId", std::to_string(activity.session)},
                {"expectedRevision", activity.expected_revision},
                {"state", tool_state_name(activity.state)},
                {"progress", json{{"current", activity.progress_current}, {"total", activity.progress_total}}}};
    if (!activity.result_json.empty())
        result["result"] = parsed_or_object(activity.result_json);
    if (activity.error)
        result["error"] = json{{"code", activity.error->code}, {"message", activity.error->message}};
    return result;
}

json revision_json(const RevisionInfo& revision, const std::string& current_revision_id)
{
    return json{{"id", revision.id},
                {"createdAt", revision.created_at},
                {"cause", revision.cause},
                {"conversationId", revision.conversation_id},
                {"afterMessageId", revision.after_message_id},
                {"current", revision.id == current_revision_id},
                {"revertible", !revision.snapshot_file.empty()}};
}

json statistics_json(const SliceStatistics& statistics)
{
    return json{{"printTimeSeconds", statistics.print_time_seconds},
                {"filamentMm", statistics.filament_mm},
                {"materialGrams", statistics.material_grams},
                {"materialCost", statistics.material_cost},
                {"layerCount", statistics.layer_count}};
}

json build_json(const BuildRecord& record, const WorkspaceSnapshot& snapshot)
{
    const std::optional<std::string> current_hash = manufacturing_input_hash(snapshot, record.plate_index);
    const bool stale = !current_hash || *current_hash != record.manufacturing_input_hash;
    return json{{"id", record.id},
                {"seq", record.seq},
                {"createdAt", record.created_at},
                {"projectId", record.project_id},
                {"revisionId", record.revision_id},
                {"conversationId", record.conversation_id},
                {"afterMessageId", record.after_message_id},
                {"plateIndex", record.plate_index},
                {"plateName", record.plate_name},
                {"printer", record.printer},
                {"material", record.material},
                {"manufacturingInputHash", record.manufacturing_input_hash},
                {"outputHash", record.output_hash},
                {"slicerVersion", record.slicer_version},
                {"configurationProvenance", record.configuration_provenance},
                {"statistics", statistics_json(record.statistics)},
                {"warnings", record.warnings},
                {"stale", stale}};
}

json exported_copy_json(const ExportedCopyRecord& record)
{
    const bool verified = !record.observed_output_hash.empty() &&
                          record.observed_output_hash == record.expected_output_hash;
    const bool modified = !record.observed_output_hash.empty() && !verified;
    return json{{"id", record.id},
                {"seq", record.seq},
                {"createdAt", record.created_at},
                {"buildId", record.build_id},
                {"conversationId", record.conversation_id},
                {"afterMessageId", record.after_message_id},
                {"destination", record.destination},
                {"expectedOutputHash", record.expected_output_hash},
                {"observedOutputHash", record.observed_output_hash},
                {"verified", verified},
                {"modified", modified}};
}

json physical_print_json(const PhysicalPrintRecord& record, const ProjectStateDocument& document)
{
    const bool timeline_removed = !record.revision_id.empty() && !document.find_revision(record.revision_id).has_value();
    return json{{"id", record.id},
                {"seq", record.seq},
                {"startedAt", record.started_at},
                {"endedAt", record.ended_at},
                {"outcome", record.outcome},
                {"failure", record.failure},
                {"buildId", record.build_id},
                {"projectId", record.project_id},
                {"revisionId", record.revision_id},
                {"conversationId", record.conversation_id},
                {"afterMessageId", record.after_message_id},
                {"plateIndex", record.plate_index},
                {"plateName", record.plate_name},
                {"printer", record.printer},
                {"material", record.material},
                {"manufacturingInputHash", record.manufacturing_input_hash},
                {"outputHash", record.output_hash},
                {"gcodeHash", record.gcode_hash},
                {"statistics", statistics_json(record.statistics)},
                {"timelineRemoved", timeline_removed}};
}

// --- Attachments ---------------------------------------------------------

// Byte caps: reject anything larger than the total cap; only inline an image
// preview data URL when the blob is within the preview cap; truncate text
// previews to keep state.json small.
constexpr std::size_t kMaxAttachmentBytes    = 32u * 1024u * 1024u;
constexpr std::size_t kInlineImagePreviewCap = 256u * 1024u;
constexpr std::size_t kTextPreviewChars      = 4000u;
constexpr std::size_t kAgentTextContextCap   = 256u * 1024u;
constexpr std::size_t kAgentBinaryContextCap = 10u * 1024u * 1024u;

std::string to_lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extension_of(const std::string& name)
{
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return {};
    return to_lower(name.substr(dot + 1));
}

// Reduces a user-supplied name to one safe path component: basename only, no
// traversal, printable ASCII, length-capped. Never returns an empty string.
std::string sanitize_filename(const std::string& name)
{
    std::string base = name;
    const auto   slash = base.find_last_of("/\\");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    std::string out;
    for (char c : base) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '.' || c == '-' || c == '_' || c == ' ')
            out += c;
        else
            out += '_';
    }
    std::size_t i = 0;
    while (i < out.size() && out[i] == '.') // no leading dots => no "." or ".."
        ++i;
    out = out.substr(i);
    if (out.empty())
        out = "file";
    if (out.size() > 128) // keep the extension tail, which is the informative end
        out = out.substr(out.size() - 128);
    return out;
}

const std::set<std::string>& text_extensions()
{
    static const std::set<std::string> v{
        "txt", "md",  "markdown", "csv", "tsv", "log",  "json", "yaml", "yml", "xml", "ini", "cfg",  "conf",
        "toml", "c",  "cc",       "cpp", "cxx", "h",    "hpp",  "hxx",  "py",  "js",  "mjs", "ts",   "tsx",
        "jsx",  "sh", "bash",     "zsh", "rs",  "go",   "java", "kt",   "cs",  "rb",  "php", "css",  "scss",
        "html", "htm", "sql",     "rst", "tex", "properties"};
    return v;
}

const std::set<std::string>& model_extensions()
{
    static const std::set<std::string> v{"stl", "obj",  "3mf",  "step", "stp", "amf",
                                         "oltp", "drc", "ply", "usd",  "usda", "usdc", "usdz", "abc"};
    return v;
}

std::string strip_data_url_prefix(const std::string& data)
{
    // Accept both a bare base64 payload and a full "data:...;base64,<payload>".
    const auto marker = data.find(";base64,");
    if (marker != std::string::npos)
        return data.substr(marker + 8);
    return data;
}

bool base64_decode(const std::string& in, std::string& out)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const auto table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        for (int i = 0; i < 64; ++i)
            t[static_cast<unsigned char>(chars[i])] = i;
        return t;
    }();
    int val = 0, valb = -8;
    out.clear();
    for (unsigned char c : in) {
        if (c == '=')
            break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;
        const int decoded = table[c];
        if (decoded == -1)
            return false;
        val = (val << 6) + decoded;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return true;
}

std::string base64_encode(const std::string& in)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int         val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

bool is_valid_utf8(const std::string& s)
{
    std::size_t i = 0;
    const auto  n = s.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t         extra = 0;
        if (c < 0x80)
            extra = 0;
        else if ((c >> 5) == 0x6)
            extra = 1;
        else if ((c >> 4) == 0xE)
            extra = 2;
        else if ((c >> 3) == 0x1E)
            extra = 3;
        else
            return false;
        if (i + extra >= n && extra > 0)
            return false;
        for (std::size_t k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2)
                return false;
        i += extra + 1;
    }
    return true;
}

const char* image_mime_for(const std::string& ext)
{
    if (ext == "png")
        return "image/png";
    if (ext == "jpg" || ext == "jpeg")
        return "image/jpeg";
    if (ext == "gif")
        return "image/gif";
    if (ext == "webp")
        return "image/webp";
    return "application/octet-stream";
}

struct AttachmentClassification
{
    std::string kind;
    std::string mime;
};

// Maps a file name (and optional page-provided MIME) to the host's kind. Model
// and project files reach the Agent as native summaries, not decoded bytes.
AttachmentClassification classify_attachment(const std::string& name, const std::string& provided_mime)
{
    const std::string ext = extension_of(name);
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp")
        return {"image", image_mime_for(ext)};
    if (ext == "svg")
        return {"svg", "image/svg+xml"};
    if (ext == "pdf")
        return {"pdf", "application/pdf"};
    if (ext == "gcode" || ext == "g" || ext == "gco")
        return {"gcode", "text/x.gcode"};
    if (text_extensions().count(ext))
        return {"text", provided_mime.empty() ? "text/plain" : provided_mime};
    if (model_extensions().count(ext))
        return {"model", "model/3mf"};
    return {"unsupported", provided_mime};
}

// The saved, persisted fields of an attachment (no volatile image data URL).
json attachment_json(const AttachmentRecord& record)
{
    json result{{"id", record.id},
                {"name", record.original_name},
                {"kind", record.kind},
                {"mime", record.mime},
                {"sizeBytes", record.size_bytes},
                {"source", record.source},
                {"state", record.state}};
    if (!record.client_id.empty())
        result["clientId"] = record.client_id;
    if (!record.preview_text.empty())
        result["previewText"] = record.preview_text;
    if (!record.summary.empty())
        result["summary"] = record.summary;
    if (record.error)
        result["error"] = json{{"code", record.error->code}, {"message", record.error->message}};
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

AgentHost::AgentHost(Workspace::IWorkspace& workspace,
                     ProjectPersistence&    persistence,
                     AgentAvailability      availability,
                     bool                   dark_appearance,
                     AgentServicePtr        agent,
                     AgentSetupServicePtr   setup)
    : m_workspace(workspace), m_persistence(persistence), m_tools(workspace), m_agent(std::move(agent)),
      m_setup(std::move(setup)), m_availability(availability), m_dark(dark_appearance)
{
    refresh_workspace_identity();
    m_workspace_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        m_last_session  = change.session.value();
        m_last_revision = change.revision;
        if (m_handshake)
            send_context();
    });
    m_tools.set_action_id_allocator([this]() { return m_persistence.document().allocate_action_id(); });
    m_tools.set_attachment_path_resolver([this](const std::string& attachment_id) -> std::string {
        const std::optional<AttachmentRecord> record = m_persistence.document().find_attachment(attachment_id);
        if (!record || record->kind != "model" || record->stored_name.empty())
            return {};
        return (std::filesystem::path(m_persistence.jusprin_data_dir()) / std::filesystem::path(record->relative_path()))
            .string();
    });
    m_tools.set_extension_executor([this](const ToolActivity& activity) {
        return execute_manufacturing_tool(activity);
    });
    m_tools.set_listener([this](const ToolActivity& activity) {
        m_persistence.document().upsert_activity(activity, m_persistence.timestamp());
        if (tool_state_terminal(activity.state))
            m_persistence.flush();
        else
            m_persistence.commit();
        if (m_handshake)
            send_tool_activity(activity);
        if (m_handshake && activity.state == ToolState::Succeeded &&
            (activity.tool == "record_build" || activity.tool == "record_export_copy" ||
             activity.tool == "record_physical_print"))
            send_state();
        if (tool_state_terminal(activity.state))
            continue_after_tool(activity);
    });
    m_persistence.set_document_replaced_listener([this]() { on_document_replaced(); });
    m_persistence.set_revision_listener([this](const RevisionInfo& revision) {
        if (m_handshake)
            send_envelope(Protocol::kRevisionAdded,
                          json{{"revision", revision_json(revision, m_persistence.document().current_revision_id())}}.dump());
    });
}

AgentHost::~AgentHost()
{
    if (m_agent)
        m_agent->cancel();
    // The persistence object outlives this host (the shell controller owns
    // both); drop the callbacks that capture `this`.
    m_persistence.set_document_replaced_listener({});
    m_persistence.set_revision_listener({});
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

std::vector<ConversationMessage> AgentHost::conversation() const
{
    return m_persistence.document().messages(m_persistence.document().active_conversation_id());
}

void AgentHost::on_document_replaced()
{
    if (m_agent)
        m_agent->cancel();
    m_stream.reset();
    m_queued_user_message_ids.clear();
    m_tool_continuations.clear();
    m_tools.clear();
    if (m_handshake)
        send_state();
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

    const ProjectStateDocument& document = m_persistence.document();
    const std::string active = document.active_conversation_id();

    json conversations = json::array();
    for (const ConversationInfo& info : document.conversations())
        conversations.push_back(json{{"id", info.id}, {"title", info.title}, {"createdAt", info.created_at}});

    json conversation = json::array();
    for (const ConversationMessage& message : document.messages(active))
        conversation.push_back(message_json(message));

    // Stored history first, overlaid by live coordinator records (the live
    // record is fresher while an action runs).
    std::vector<ToolActivity> merged = document.activities();
    for (const ToolActivity& live : m_tools.activities()) {
        const auto existing = std::find_if(merged.begin(), merged.end(),
                                           [&live](const ToolActivity& a) { return a.action_id == live.action_id; });
        if (existing == merged.end())
            merged.push_back(live);
        else
            *existing = live;
    }
    json tool_activities = json::array();
    for (const ToolActivity& activity : merged)
        tool_activities.push_back(activity_json(activity));

    json revisions = json::array();
    for (const RevisionInfo& revision : document.revisions())
        revisions.push_back(revision_json(revision, document.current_revision_id()));

    json attachments = json::array();
    for (const AttachmentRecord& record : document.attachments()) {
        json entry = attachment_json(record);
        const std::string preview = attachment_preview_data_url(record);
        if (!preview.empty())
            entry["previewDataUrl"] = preview;
        attachments.push_back(std::move(entry));
    }

    json builds = json::array();
    for (const BuildRecord& record : document.builds())
        builds.push_back(build_json(record, snapshot));
    json exported_copies = json::array();
    for (const ExportedCopyRecord& record : document.exported_copies())
        exported_copies.push_back(exported_copy_json(record));
    json physical_prints = json::array();
    for (const PhysicalPrintRecord& record : document.physical_prints())
        physical_prints.push_back(physical_print_json(record, document));

    json payload{{"agent", json{{"status", availability_name(m_availability)}}},
                 {"appearance", m_dark ? "dark" : "light"},
                 {"conversations", std::move(conversations)},
                 {"activeConversationId", active},
                 {"conversation", std::move(conversation)},
                 {"streamingMessageId", m_stream ? json(m_stream->message.id) : json(nullptr)},
                 {"toolActivities", std::move(tool_activities)},
                 {"revisions", std::move(revisions)},
                 {"draft", m_persistence.draft()},
                 {"attachments", std::move(attachments)},
                 {"builds", std::move(builds)},
                 {"exportedCopies", std::move(exported_copies)},
                 {"physicalPrints", std::move(physical_prints)},
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
    else if (type == Protocol::kToolDecision)
        handle_tool_decision(envelope_id, payload);
    else if (type == Protocol::kToolCancel)
        handle_tool_cancel(envelope_id, payload);
    else if (type == Protocol::kCreateConversation)
        handle_create_conversation(envelope_id, payload);
    else if (type == Protocol::kSwitchConversation)
        handle_switch_conversation(envelope_id, payload);
    else if (type == Protocol::kRevertToRevision)
        handle_revert_to_revision(envelope_id, payload);
    else if (type == Protocol::kDraftUpdate)
        handle_draft_update(payload);
    else if (type == Protocol::kAttachFile)
        handle_attach_file(envelope_id, payload);
    else if (type == Protocol::kRemoveAttachment)
        handle_remove_attachment(envelope_id, payload);
    else if (type == Protocol::kSetupCheckKey)
        handle_setup_check_key(envelope_id, payload);
    else if (type == Protocol::kSetupCancel)
        handle_setup_cancel();
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
                           {"message", "This build speaks jusprin-agent-bridge version " +
                                           std::to_string(Protocol::kVersion) + " only."}}
                          .dump(),
                      envelope_id);
        return;
    }

    // Capabilities are informational: the host reports what it can do and the
    // page hides what the host did not claim.
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
        !payload.contains("text") || !payload["text"].is_string()) {
        send_bridge_error("invalid_payload", "user_message requires a clientMessageId and text.", envelope_id);
        return;
    }
    const std::string client_id = payload["clientMessageId"].get<std::string>();
    const std::string text      = payload["text"].get<std::string>();

    std::vector<std::string> requested_attachments;
    if (payload.contains("attachmentIds") && payload["attachmentIds"].is_array())
        for (const json& id : payload["attachmentIds"])
            if (id.is_string())
                requested_attachments.push_back(id.get<std::string>());

    // A message must carry text or at least one attachment.
    if (text.empty() && requested_attachments.empty()) {
        send_bridge_error("invalid_payload", "A message needs text or an attachment.", envelope_id);
        return;
    }

    ProjectStateDocument& document = m_persistence.document();

    // A reconnect, reload, or crash-recovery resend may repeat a message the
    // document already owns; the stable client ID makes that a duplicate
    // acknowledgement, not a repeat.
    if (const std::optional<std::string> existing = document.client_message_lookup(client_id)) {
        if (const std::optional<ConversationMessage> message = find_stored_message(*existing))
            send_envelope(Protocol::kMessageAdded, json{{"message", message_json(*message)}}.dump(), envelope_id);
        return;
    }

    // Only attachments the host actually holds as staged can be sent; unknown
    // or already-sent IDs are dropped rather than trusted.
    std::vector<std::string> sent_attachments;
    for (const std::string& id : requested_attachments) {
        const std::optional<AttachmentRecord> record = document.find_attachment(id);
        if (record && record->state == "staged")
            sent_attachments.push_back(id);
    }

    const std::string conversation_id = document.active_conversation_id();
    ConversationMessage message;
    message.id                = document.allocate_message_id();
    message.role              = MessageRole::User;
    message.state             = MessageState::Complete;
    message.text              = text;
    message.client_message_id = client_id;
    message.attachment_ids    = sent_attachments;
    document.append_message(conversation_id, message, m_persistence.timestamp());
    document.mark_attachments_sent(sent_attachments);
    // The outgoing message is durable before any reply work starts.
    m_persistence.flush();
    // Sending cleared the composer; the draft it held is no longer working
    // state to recover.
    m_persistence.set_draft({});
    send_envelope(Protocol::kMessageAdded, json{{"message", message_json(message)}}.dump(), envelope_id);

    if (agent_busy()) {
        // The page disables sending while a reply streams; if a message
        // arrives anyway, answer it after the current stream finishes.
        m_queued_user_message_ids.push_back(message.id);
        return;
    }
    begin_reply(message.id);
}

std::string AgentHost::attachment_preview_data_url(const AttachmentRecord& record) const
{
    if (record.kind != "image" || record.stored_name.empty() || record.size_bytes > kInlineImagePreviewCap)
        return {};
    const std::string bytes = m_persistence.read_attachment_blob(record.relative_path());
    if (bytes.empty())
        return {};
    return "data:" + record.mime + ";base64," + base64_encode(bytes);
}

void AgentHost::send_attachment_updated(const AttachmentRecord& record, const std::string& correlation_id)
{
    json entry = attachment_json(record);
    const std::string preview = attachment_preview_data_url(record);
    if (!preview.empty())
        entry["previewDataUrl"] = preview;
    send_envelope(Protocol::kAttachmentUpdated, json{{"attachment", std::move(entry)}}.dump(), correlation_id);
}

void AgentHost::handle_attach_file(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    if (!payload.is_object()) {
        send_bridge_error("invalid_payload", "attach_file requires an object payload.", envelope_id);
        return;
    }
    const std::string client_id = payload.value("clientAttachmentId", "");
    const std::string name      = payload.value("name", "");
    std::string       source    = payload.value("source", "picker");
    if (source != "picker" && source != "drop" && source != "clipboard" && source != "project")
        source = "picker";
    if (name.empty() && source != "project") {
        send_bridge_error("invalid_payload", "attach_file requires a file name.", envelope_id);
        return;
    }

    ProjectStateDocument& document = m_persistence.document();

    // A lost acknowledgement can make the page resend the same file; the stable
    // client ID makes that idempotent rather than a duplicate attachment.
    if (const std::optional<AttachmentRecord> existing = document.find_attachment_by_client_id(client_id)) {
        send_attachment_updated(*existing, envelope_id);
        return;
    }

    AttachmentRecord record;
    record.id            = document.allocate_attachment_id();
    record.client_id     = client_id;
    record.original_name = name;
    record.source        = source;
    record.state         = "staged";

    if (source == "project") {
        // A reference to an item already in the open project: the Agent sees a
        // native summary, and there is no blob to store or decode.
        record.kind    = payload.value("refKind", "model");
        record.summary = payload.value("label", name.empty() ? std::string("Project item") : name);
        document.add_attachment(record, m_persistence.timestamp());
        m_persistence.commit();
        m_persistence.flush();
        send_attachment_updated(record, envelope_id);
        return;
    }

    const AttachmentClassification classification = classify_attachment(name, payload.value("mime", ""));
    record.kind        = classification.kind;
    record.mime        = classification.mime;
    record.stored_name = sanitize_filename(name);

    std::string bytes;
    if (!base64_decode(strip_data_url_prefix(payload.value("dataBase64", "")), bytes)) {
        record.state = "error";
        record.error = AgentError{"unreadable", "The file could not be read."};
        document.add_attachment(record, m_persistence.timestamp());
        m_persistence.commit();
        m_persistence.flush();
        send_attachment_updated(record, envelope_id);
        return;
    }

    if (bytes.size() > kMaxAttachmentBytes) {
        record.state = "error";
        record.error = AgentError{"too_large", "This file is too large to attach."};
    } else if (record.kind == "unsupported") {
        record.state = "error";
        record.error = AgentError{"unsupported_type", "This file type can't be read by the Agent."};
    } else if ((record.kind == "text" || record.kind == "gcode" || record.kind == "svg") && !is_valid_utf8(bytes)) {
        record.state = "error";
        record.error = AgentError{"not_text", "This file is not readable UTF-8 text."};
    } else if (!m_persistence.write_attachment_blob(record.relative_path(), bytes)) {
        record.state = "error";
        record.error = AgentError{"store_failed", "The attachment could not be stored."};
    } else {
        record.size_bytes = bytes.size();
        if (record.kind == "text" || record.kind == "gcode" || record.kind == "svg") {
            record.preview_text = bytes.size() > kTextPreviewChars ? bytes.substr(0, kTextPreviewChars) : bytes;
        } else if (record.kind == "model") {
            // Models reach the Agent as a native summary, never as bytes. The
            // authoritative import happens when the message is sent (Phase 5
            // import seam); this is the pre-import description.
            record.summary = "3D model \"" + name + "\" (" + std::to_string(bytes.size()) +
                             " bytes), ready to import into the project.";
        }
        // Images and PDFs are stored as-is; images get an inline preview built
        // on demand from the blob, so nothing large is kept in state.json.
    }

    document.add_attachment(record, m_persistence.timestamp());
    m_persistence.commit();
    m_persistence.flush();
    send_attachment_updated(record, envelope_id);
}

void AgentHost::handle_remove_attachment(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    const std::string id = payload.is_object() ? payload.value("attachmentId", "") : std::string();
    if (id.empty()) {
        send_bridge_error("invalid_payload", "remove_attachment requires an attachmentId.", envelope_id);
        return;
    }
    ProjectStateDocument& document = m_persistence.document();
    const std::optional<std::string> removed_dir = document.remove_staged_attachment(id);
    if (!removed_dir) {
        // Already gone or a sent (durable) attachment; report current state so
        // the page reconciles rather than assuming a removal that did not run.
        send_state(envelope_id);
        return;
    }
    m_persistence.remove_attachment_dir(*removed_dir);
    m_persistence.commit();
    m_persistence.flush();
    send_state(envelope_id);
}

void AgentHost::handle_stop(const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    const std::string message_id = payload.is_object() ? payload.value("messageId", "") : std::string();
    // Stopping an already-finished stream is a benign race; ignore silently.
    if (!m_stream || m_stream->message.id != message_id)
        return;
    ActiveStream stream = std::move(*m_stream);
    m_stream.reset();
    if (m_agent)
        m_agent->cancel();
    stream.message.state = MessageState::Stopped;
    m_persistence.document().update_message(stream.conversation_id, stream.message);
    m_persistence.flush();
    send_envelope(Protocol::kAssistantStopped, json{{"messageId", stream.message.id}}.dump());
    start_next_queued_reply();
}

void AgentHost::handle_retry(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    const std::string message_id = payload.is_object() ? payload.value("messageId", "") : std::string();

    std::string conversation_id;
    const std::optional<ConversationMessage> message = find_stored_message(message_id, &conversation_id);
    if (!message || message->role != MessageRole::Assistant ||
        (message->state != MessageState::Failed && message->state != MessageState::Stopped) ||
        conversation_id != m_persistence.document().active_conversation_id()) {
        send_bridge_error("invalid_retry", "Only a failed or stopped assistant message in the active conversation can be retried.",
                          envelope_id);
        return;
    }
    if (agent_busy()) {
        send_bridge_error("busy", "Another reply is currently streaming.", envelope_id);
        return;
    }
    ConversationMessage retried = *message;
    ++retried.attempt;
    begin_stream(std::move(retried), conversation_id);
}

void AgentHost::handle_tool_decision(const std::string& envelope_id, const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    if (!payload.is_object() || !payload.contains("actionId") || !payload["actionId"].is_string() ||
        !payload.contains("decision") || !payload["decision"].is_string()) {
        send_bridge_error("invalid_payload", "tool_decision requires an actionId and a decision.", envelope_id);
        return;
    }
    const std::string action_id = payload["actionId"].get<std::string>();
    const std::string decision  = payload["decision"].get<std::string>();
    if (decision != "approve" && decision != "reject") {
        send_bridge_error("invalid_payload", "tool_decision decision must be \"approve\" or \"reject\".", envelope_id);
        return;
    }
    if (m_tools.find(action_id) == nullptr) {
        send_bridge_error("unknown_action", "No tool action \"" + action_id + "\" exists.", envelope_id);
        return;
    }

    const bool changed = decision == "approve" ? m_tools.approve(action_id) : m_tools.reject(action_id);
    if (!changed) {
        // A resent decision after a reload or reconnect: acknowledge with the
        // authoritative record instead of running anything twice.
        if (const ToolActivity* activity = m_tools.find(action_id))
            send_tool_activity(*activity, envelope_id);
    }
}

void AgentHost::handle_tool_cancel(const std::string& envelope_id, const std::string& payload_json)
{
    const json        payload   = json::parse(payload_json, nullptr, false);
    const std::string action_id = payload.is_object() ? payload.value("actionId", "") : std::string();
    if (m_tools.find(action_id) == nullptr) {
        send_bridge_error("unknown_action", "No tool action \"" + action_id + "\" exists.", envelope_id);
        return;
    }
    if (!m_tools.cancel(action_id)) {
        // Cancelling an already-finished action is a benign race; answer with
        // the authoritative record.
        if (const ToolActivity* activity = m_tools.find(action_id))
            send_tool_activity(*activity, envelope_id);
    }
}

void AgentHost::handle_create_conversation(const std::string& envelope_id, const std::string& payload_json)
{
    if (agent_busy()) {
        send_bridge_error("busy", "Finish or stop the streaming reply before changing conversations.", envelope_id);
        return;
    }
    const json        payload = json::parse(payload_json, nullptr, false);
    const std::string title   = payload.is_object() ? payload.value("title", "") : std::string();
    m_persistence.document().create_conversation(title, m_persistence.timestamp());
    m_persistence.flush();
    send_state(envelope_id);
}

void AgentHost::handle_switch_conversation(const std::string& envelope_id, const std::string& payload_json)
{
    if (agent_busy()) {
        send_bridge_error("busy", "Finish or stop the streaming reply before changing conversations.", envelope_id);
        return;
    }
    const json        payload         = json::parse(payload_json, nullptr, false);
    const std::string conversation_id = payload.is_object() ? payload.value("conversationId", "") : std::string();
    if (!m_persistence.document().set_active_conversation(conversation_id)) {
        send_bridge_error("unknown_conversation", "No conversation \"" + conversation_id + "\" exists.", envelope_id);
        return;
    }
    m_persistence.flush();
    send_state(envelope_id);
}

void AgentHost::handle_revert_to_revision(const std::string& envelope_id, const std::string& payload_json)
{
    if (agent_busy() || m_tools.any_running()) {
        send_bridge_error("busy", "Finish or stop the current activity before reverting.", envelope_id);
        return;
    }
    const json        payload     = json::parse(payload_json, nullptr, false);
    const std::string revision_id = payload.is_object() ? payload.value("revisionId", "") : std::string();

    const ProjectPersistence::RevertResult result = m_persistence.revert_to_revision(revision_id);
    if (!result.ok)
        send_bridge_error("revert_failed", result.error, envelope_id);
    // On success the document-replaced listener has already pushed the full
    // reconstructed state.
}

void AgentHost::handle_draft_update(const std::string& payload_json)
{
    const json payload = json::parse(payload_json, nullptr, false);
    if (payload.is_object() && payload.contains("text") && payload["text"].is_string())
        m_persistence.set_draft(payload["text"].get<std::string>());
}

void AgentHost::send_tool_activity(const ToolActivity& activity, const std::string& correlation_id)
{
    send_envelope(Protocol::kToolActivity, json{{"activity", activity_json(activity)}}.dump(), correlation_id);
}

ToolExecutionCoordinator::ExtensionResult AgentHost::execute_manufacturing_tool(const ToolActivity& activity)
{
    ToolExecutionCoordinator::ExtensionResult result;
    if (activity.tool != "record_build" && activity.tool != "record_export_copy" &&
        activity.tool != "record_physical_print")
        return result;
    result.handled = true;

    const json arguments = json::parse(activity.arguments_json, nullptr, false);
    if (!arguments.is_object()) {
        result.error = ToolError{"invalid_arguments", "The manufacturing record arguments are invalid."};
        return result;
    }

    ProjectStateDocument& document = m_persistence.document();
    const WorkspaceSnapshot snapshot = m_workspace.snapshot();
    const std::string conversation = document.conversation_of_message(activity.correlation_id);

    if (activity.tool == "record_build") {
        std::size_t plate_index = 0;
        if (snapshot.active_plate)
            for (std::size_t i = 0; i < snapshot.plates.size(); ++i)
                if (snapshot.plates[i].id == *snapshot.active_plate) {
                    plate_index = i;
                    break;
                }
        if (plate_index >= snapshot.plates.size() || !snapshot.plates[plate_index].sliced) {
            result.error = ToolError{"slice_required", "Slice the active plate before recording a build."};
            return result;
        }
        const std::optional<std::string> input_hash = manufacturing_input_hash(snapshot, plate_index);
        if (!input_hash) {
            result.error = ToolError{"missing_plate", "The active plate is no longer available."};
            return result;
        }
        BuildRecord record;
        record.project_id               = document.project_id();
        record.revision_id              = document.current_revision_id();
        record.conversation_id          = conversation;
        record.after_message_id         = activity.correlation_id;
        record.plate_index              = plate_index;
        record.plate_name               = snapshot.plates[plate_index].name;
        record.printer                  = snapshot.setup.printer_preset;
        record.material                 = snapshot.setup.filament_preset;
        record.manufacturing_input_hash = *input_hash;
        record.slicer_version           = arguments.value("slicerVersion", "JusPrin deterministic Phase 6");
        record.configuration_provenance = arguments.value(
            "configurationProvenance", "Active printer, process, filament, plate, objects, instances, and transforms");
        record.output_hash = deterministic_output_hash(*input_hash, record.slicer_version,
                                                       record.configuration_provenance);
        record.statistics.print_time_seconds = arguments.value("printTimeSeconds", 3720.0);
        record.statistics.filament_mm        = arguments.value("filamentMm", 1842.5);
        record.statistics.material_grams     = arguments.value("materialGrams", 14.7);
        record.statistics.material_cost      = arguments.value("materialCost", 0.44);
        record.statistics.layer_count        = arguments.value("layerCount", std::uint64_t(124));
        if (arguments.contains("warnings") && arguments["warnings"].is_array())
            for (const json& warning : arguments["warnings"])
                if (warning.is_string())
                    record.warnings.push_back(warning.get<std::string>());
        const std::string id = document.add_build(std::move(record), m_persistence.timestamp());
        m_persistence.flush();
        result.result_json = json{{"buildId", id}, {"recorded", true}}.dump();
        return result;
    }

    const std::string requested_build = arguments.value("buildId", "");
    const std::optional<BuildRecord> build = requested_build.empty() ? document.latest_build() :
                                                                       document.find_build(requested_build);
    if (!build) {
        result.error = ToolError{"missing_build", "Record a build before creating this history entry."};
        return result;
    }

    if (activity.tool == "record_export_copy") {
        ExportedCopyRecord record;
        record.build_id             = build->id;
        record.conversation_id      = conversation;
        record.after_message_id     = activity.correlation_id;
        record.destination          = arguments.value("destination", "Phase 6 demo.gcode");
        record.expected_output_hash = build->output_hash;
        record.observed_output_hash = arguments.value("observedOutputHash", build->output_hash);
        const std::string id = document.add_exported_copy(std::move(record), m_persistence.timestamp());
        m_persistence.flush();
        result.result_json = json{{"exportedCopyId", id}, {"buildId", build->id}}.dump();
        return result;
    }

    PhysicalPrintRecord record;
    record.build_id                 = build->id;
    record.project_id               = build->project_id;
    record.revision_id              = build->revision_id;
    record.conversation_id          = conversation;
    record.after_message_id         = activity.correlation_id;
    record.plate_index              = build->plate_index;
    record.plate_name               = build->plate_name;
    record.printer                  = arguments.value("printer", build->printer);
    record.material                 = arguments.value("material", build->material);
    record.started_at               = arguments.value("startedAt", m_persistence.timestamp());
    record.ended_at                 = arguments.value("endedAt", m_persistence.timestamp());
    record.outcome                  = arguments.value("outcome", "completed");
    record.failure                  = arguments.value("failure", "");
    record.manufacturing_input_hash = build->manufacturing_input_hash;
    record.output_hash              = build->output_hash;
    record.gcode_hash               = arguments.value("gcodeHash", build->output_hash);
    record.statistics               = build->statistics;
    const std::string id = document.add_physical_print(std::move(record), m_persistence.timestamp());
    m_persistence.flush();
    m_persistence.notify_ledger_changed();
    result.result_json = json{{"physicalPrintId", id}, {"buildId", build->id}, {"recorded", true}}.dump();
    return result;
}

void AgentHost::send_setup_status(const std::string&               phase,
                                  const std::string&               correlation_id,
                                  std::optional<int>               elapsed_ms,
                                  const std::optional<AgentError>& error,
                                  const std::string&               warning)
{
    json payload{{"phase", phase}};
    if (m_setup_pending)
        payload["provider"] = m_setup_pending->provider;
    if (elapsed_ms)
        payload["elapsedMs"] = *elapsed_ms;
    if (error)
        payload["error"] = json{{"code", error->code}, {"message", error->message}, {"retryable", error->retryable}};
    if (!warning.empty())
        payload["warning"] = warning;
    send_envelope(Protocol::kSetupStatus, payload.dump(), correlation_id);
}

void AgentHost::handle_setup_check_key(const std::string& envelope_id, const std::string& payload_json)
{
    if (!m_setup) {
        send_bridge_error("setup_unavailable", "This build cannot configure an Agent provider.", envelope_id);
        return;
    }
    const json payload = json::parse(payload_json, nullptr, false);
    if (!payload.is_object()) {
        send_bridge_error("invalid_payload", "setup_check_key requires an object payload.", envelope_id);
        return;
    }

    SetupCredentials credentials;
    credentials.provider = payload.value("provider", "");
    credentials.api_key  = payload.value("apiKey", "");
    credentials.model    = payload.value("model", "");
    credentials.endpoint = payload.value("endpoint", "");

    if (credentials.api_key.empty()) {
        send_bridge_error("invalid_payload", "setup_check_key requires an apiKey.", envelope_id);
        return;
    }
    if (m_setup->busy()) {
        send_bridge_error("setup_busy", "A credential check is already running.", envelope_id);
        return;
    }

    // The pending credentials outlive the request so a verified key can be
    // persisted without the page holding or re-sending the secret.
    m_setup_pending = credentials;
    if (!m_setup->start_check(credentials)) {
        const AgentError error{"unsupported_provider",
                               "This build cannot verify a key for that provider yet.", false};
        send_setup_status("error", envelope_id, std::nullopt, error);
        m_setup_pending.reset();
        return;
    }
    send_setup_status("checking", envelope_id);
}

void AgentHost::handle_setup_cancel()
{
    if (!m_setup || !m_setup->busy())
        return; // Cancelling a finished check is a benign race.
    m_setup->cancel();
    m_setup_pending.reset();
    send_setup_status("idle");
}

void AgentHost::pump_setup()
{
    if (!m_setup)
        return;
    std::optional<SetupOutcome> outcome = m_setup->poll();
    if (!outcome)
        return;

    if (!outcome->ok) {
        send_setup_status("error", {}, outcome->elapsed_ms, outcome->error);
        m_setup_pending.reset();
        return;
    }

    // The provider answered, so the credentials are good. Persisting them can
    // still fail on a machine with no usable credential store; the Agent works
    // for this session either way, and the user is told which one happened
    // rather than discovering it at the next launch.
    std::string warning;
    if (m_setup_pending && !m_setup->commit(*m_setup_pending))
        warning = "This key could not be saved to the system credential store, so it will have to be entered again "
                  "next time JusPrin starts.";

    send_setup_status("verified", {}, outcome->elapsed_ms, std::nullopt, warning);
    m_setup_pending.reset();
    // set_agent refuses an installation while a turn is in flight. Setup is
    // only reachable with no service configured, and an unconfigured host
    // rejects user messages before any stream or tool continuation starts, so
    // there is nothing here for it to refuse.
    set_agent(std::move(outcome->service), AgentAvailability::Ready);
}

void AgentHost::pump_tools()
{
    // A parked project boundary (Project event delivered before the new
    // directory was in place) resolves here, once per timer tick.
    m_persistence.resolve_pending_boundary();
    if (m_handshake)
        m_tools.pump();
    // Streaming deltas and progress mark the document dirty without forcing a
    // write each; this pump gives them their throttled flush.
    m_persistence.flush_if_dirty();
}

std::optional<ConversationMessage> AgentHost::find_stored_message(const std::string& id, std::string* conversation_id) const
{
    const ProjectStateDocument& document = m_persistence.document();
    const std::string owner = document.conversation_of_message(id);
    if (owner.empty())
        return std::nullopt;
    if (conversation_id != nullptr)
        *conversation_id = owner;
    for (const ConversationMessage& message : document.messages(owner))
        if (message.id == id)
            return message;
    return std::nullopt;
}

void AgentHost::begin_reply(const std::string& user_message_id)
{
    ProjectStateDocument& document = m_persistence.document();
    std::string conversation_id = document.conversation_of_message(user_message_id);
    if (conversation_id.empty())
        conversation_id = document.active_conversation_id();

    if (m_availability != AgentAvailability::Ready) {
        ConversationMessage failed;
        failed.id          = document.allocate_message_id();
        failed.role        = MessageRole::Assistant;
        failed.state       = MessageState::Failed;
        failed.in_reply_to = user_message_id;
        failed.error       = AgentError{"agent_unavailable", "The Agent service is not available.", true};
        document.append_message(conversation_id, failed, m_persistence.timestamp());
        m_persistence.flush();
        send_envelope(Protocol::kAssistantStarted, json{{"messageId", failed.id}, {"inReplyTo", user_message_id}, {"attempt", 1}}.dump());
        send_envelope(Protocol::kAssistantFailed, json{{"messageId", failed.id}, {"error", error_json(*failed.error)}}.dump());
        return;
    }

    ConversationMessage assistant;
    assistant.id          = document.allocate_message_id();
    assistant.role        = MessageRole::Assistant;
    assistant.state       = MessageState::Streaming;
    assistant.in_reply_to = user_message_id;
    document.append_message(conversation_id, assistant, m_persistence.timestamp());
    m_persistence.commit();
    begin_stream(std::move(assistant), conversation_id);
}

void AgentHost::begin_stream(ConversationMessage assistant, const std::string& conversation_id)
{
    assistant.state = MessageState::Streaming;
    assistant.text.clear();
    assistant.error.reset();
    m_persistence.document().update_message(conversation_id, assistant);
    m_persistence.commit();

    ActiveStream stream;
    stream.conversation_id = conversation_id;
    stream.message         = assistant;
    m_stream               = std::move(stream);

    send_envelope(Protocol::kAssistantStarted,
                  json{{"messageId", assistant.id}, {"inReplyTo", assistant.in_reply_to}, {"attempt", assistant.attempt}}.dump());

    if (!m_agent || !m_agent->ready() || !m_agent->start(make_agent_request(assistant, conversation_id)))
        fail_stream(AgentError{"agent_unavailable", "The Agent service could not start this request.", true});
}

AgentRequest AgentHost::make_agent_request(const ConversationMessage& assistant, const std::string& conversation_id) const
{
    const ProjectStateDocument& document = m_persistence.document();
    AgentRequest request;
    // Message IDs restart for each project. Scope the provider idempotency key
    // to the durable project and conversation so a later app run cannot reuse
    // a cached response for a different native operation.
    request.request_id = document.project_id() + "-" + conversation_id + "-" + assistant.id + "-attempt-" +
                         std::to_string(assistant.attempt);
    request.attempt    = assistant.attempt;
    request.workspace  = m_workspace.snapshot();

    const std::optional<ConversationMessage> user = find_stored_message(assistant.in_reply_to);
    if (user)
        request.user_text = user->text;

    // The provider gets bounded semantic history. The current user message is
    // supplied separately with its attachments, and the streaming placeholder
    // is native-only state, so neither is duplicated here.
    for (const ConversationMessage& message : document.messages(conversation_id)) {
        if (message.id == assistant.id || message.id == assistant.in_reply_to || message.text.empty())
            continue;
        AgentConversationContext entry;
        entry.role = message.role == MessageRole::User ? "user" : "assistant";
        entry.text = message.text;
        request.conversation.emplace_back(std::move(entry));
    }
    constexpr std::size_t kHistoryMessages = 20;
    if (request.conversation.size() > kHistoryMessages)
        request.conversation.erase(request.conversation.begin(), request.conversation.end() - kHistoryMessages);

    if (!user)
        return request;

    for (const std::string& id : user->attachment_ids) {
        const std::optional<AttachmentRecord> record = document.find_attachment(id);
        if (!record)
            continue;

        AgentAttachmentContext context;
        context.id         = record->id;
        context.name       = record->original_name.empty() ? record->summary : record->original_name;
        context.kind       = record->kind;
        context.mime       = record->mime;
        context.summary    = record->summary;
        context.importable = record->kind == "model" && !record->stored_name.empty();

        if ((record->kind == "text" || record->kind == "gcode" || record->kind == "svg") &&
            !record->stored_name.empty()) {
            context.text = m_persistence.read_attachment_blob(record->relative_path());
            if (context.text.size() > kAgentTextContextCap) {
                context.text.resize(kAgentTextContextCap);
                while (!context.text.empty() && !is_valid_utf8(context.text))
                    context.text.pop_back();
                context.summary += (context.summary.empty() ? "" : " ") +
                                   std::string("Content was truncated to 256 KiB for Agent context.");
            }
        } else if ((record->kind == "image" || record->kind == "pdf") &&
                   record->size_bytes <= kAgentBinaryContextCap && !record->stored_name.empty()) {
            context.bytes = m_persistence.read_attachment_blob(record->relative_path());
        } else if ((record->kind == "image" || record->kind == "pdf") && record->size_bytes > kAgentBinaryContextCap) {
            context.summary += (context.summary.empty() ? "" : " ") +
                               std::string("The file exceeds the 10 MiB live-Agent context limit.");
        }
        request.attachments.emplace_back(std::move(context));
    }
    return request;
}

void AgentHost::complete_stream()
{
    if (!m_stream)
        return;
    ActiveStream stream = std::move(*m_stream);
    m_stream.reset();
    stream.message.state = MessageState::Complete;
    m_persistence.document().update_message(stream.conversation_id, stream.message);
    m_persistence.flush();
    send_envelope(Protocol::kAssistantCompleted, json{{"messageId", stream.message.id}}.dump());
    start_next_queued_reply();
}

void AgentHost::fail_stream(AgentError error)
{
    if (!m_stream)
        return;
    ActiveStream stream = std::move(*m_stream);
    m_stream.reset();
    stream.message.state = MessageState::Failed;
    stream.message.error = std::move(error);
    m_persistence.document().update_message(stream.conversation_id, stream.message);
    m_persistence.flush();
    send_envelope(Protocol::kAssistantFailed,
                  json{{"messageId", stream.message.id}, {"error", error_json(*stream.message.error)}}.dump());
    start_next_queued_reply();
}

void AgentHost::handle_agent_tool_call(AgentToolCall call)
{
    if (!m_stream)
        return;

    ActiveStream stream = std::move(*m_stream);
    m_stream.reset();
    stream.message.state = MessageState::Complete;
    m_persistence.document().update_message(stream.conversation_id, stream.message);
    m_persistence.flush();
    send_envelope(Protocol::kAssistantCompleted, json{{"messageId", stream.message.id}}.dump());

    const ToolActivity& proposed = m_tools.propose(call.request, stream.message.id);
    if (call.await_result) {
        PendingToolContinuation continuation;
        continuation.call_id            = std::move(call.call_id);
        continuation.conversation_id     = stream.conversation_id;
        continuation.user_message_id     = stream.message.in_reply_to;
        m_tool_continuations[proposed.action_id] = std::move(continuation);
    } else {
        start_next_queued_reply();
    }
}

void AgentHost::continue_after_tool(const ToolActivity& activity)
{
    const auto found = m_tool_continuations.find(activity.action_id);
    if (found == m_tool_continuations.end())
        return;

    PendingToolContinuation continuation = found->second;
    m_tool_continuations.erase(found);

    const WorkspaceSnapshot current_workspace = m_workspace.snapshot();
    json output{{"state", tool_state_name(activity.state)},
                {"actionId", activity.action_id},
                {"workspaceRevision", current_workspace.revision},
                {"workspace", context_json(current_workspace)}};
    if (!activity.result_json.empty())
        output["result"] = parsed_or_object(activity.result_json);
    if (activity.error)
        output["error"] = json{{"code", activity.error->code}, {"message", activity.error->message}};

    AgentToolResult result;
    result.call_id     = continuation.call_id;
    result.state       = tool_state_name(activity.state);
    result.output_json = output.dump();
    if (!m_agent || !m_agent->continue_after_tool(result)) {
        begin_tool_followup(continuation);
        fail_stream(AgentError{"agent_continuation_failed", "The Agent could not receive the native tool result.", true});
        return;
    }
    begin_tool_followup(continuation);
}

void AgentHost::begin_tool_followup(const PendingToolContinuation& continuation)
{
    ProjectStateDocument& document = m_persistence.document();
    ConversationMessage assistant;
    assistant.id          = document.allocate_message_id();
    assistant.role        = MessageRole::Assistant;
    assistant.state       = MessageState::Streaming;
    assistant.in_reply_to = continuation.user_message_id;
    document.append_message(continuation.conversation_id, assistant, m_persistence.timestamp());
    m_persistence.commit();

    ActiveStream stream;
    stream.conversation_id = continuation.conversation_id;
    stream.message         = assistant;
    m_stream               = std::move(stream);
    send_envelope(Protocol::kAssistantStarted,
                  json{{"messageId", assistant.id}, {"inReplyTo", assistant.in_reply_to}, {"attempt", assistant.attempt}}.dump());
}

void AgentHost::pump_stream()
{
    if (!m_stream || !m_handshake)
        return;

    const std::optional<AgentEvent> next = m_agent ? m_agent->poll() : std::nullopt;
    if (!next)
        return;

    if (next->kind == AgentEventKind::TextDelta) {
        const std::string& chunk = next->text;
        m_stream->message.text += chunk;
        m_persistence.document().update_message(m_stream->conversation_id, m_stream->message);
        m_persistence.commit();
        send_envelope(Protocol::kAssistantDelta,
                      json{{"messageId", m_stream->message.id}, {"seq", m_stream->next_seq++}, {"text", chunk}}.dump());
        return;
    }

    if (next->kind == AgentEventKind::ToolCall && next->tool)
        handle_agent_tool_call(*next->tool);
    else if (next->kind == AgentEventKind::Failed)
        fail_stream(next->error.value_or(AgentError{"agent_error", "The Agent request failed.", true}));
    else if (next->kind == AgentEventKind::Completed)
        complete_stream();
}

void AgentHost::start_next_queued_reply()
{
    if (agent_busy() || m_queued_user_message_ids.empty())
        return;
    const std::string next = m_queued_user_message_ids.front();
    m_queued_user_message_ids.pop_front();
    begin_reply(next);
}

bool AgentHost::agent_busy() const
{
    return m_stream.has_value() || !m_tool_continuations.empty() || (m_agent && m_agent->busy());
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

void AgentHost::set_agent(AgentServicePtr agent, AgentAvailability availability)
{
    if (agent_busy())
        return;
    if (m_agent)
        m_agent->cancel();
    m_agent = std::move(agent);
    set_availability(availability);
}

} // namespace Slic3r::GUI::JusPrin::Agent
