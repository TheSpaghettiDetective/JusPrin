#include "ProjectStateDocument.hpp"

#include <algorithm>
#include <set>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

const char* role_name(MessageRole role) { return role == MessageRole::User ? "user" : "assistant"; }

MessageRole role_from(const std::string& name) { return name == "user" ? MessageRole::User : MessageRole::Assistant; }

const char* message_state_name(MessageState state)
{
    switch (state) {
    case MessageState::Complete: return "complete";
    case MessageState::Streaming: return "streaming";
    case MessageState::Failed: return "failed";
    case MessageState::Stopped: return "stopped";
    }
    return "complete";
}

MessageState message_state_from(const std::string& name)
{
    if (name == "streaming")
        return MessageState::Streaming;
    if (name == "failed")
        return MessageState::Failed;
    if (name == "stopped")
        return MessageState::Stopped;
    return MessageState::Complete;
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

ToolState tool_state_from(const std::string& name)
{
    if (name == "approved")
        return ToolState::Approved;
    if (name == "running")
        return ToolState::Running;
    if (name == "succeeded")
        return ToolState::Succeeded;
    if (name == "failed")
        return ToolState::Failed;
    if (name == "cancelled")
        return ToolState::Cancelled;
    if (name == "rejected")
        return ToolState::Rejected;
    return ToolState::Pending;
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

ActionClass action_class_from(const std::string& name)
{
    if (name == "mutation")
        return ActionClass::Mutation;
    if (name == "destructive")
        return ActionClass::Destructive;
    return ActionClass::ReadOnly;
}

// Updates the known fields of an existing JSON object in place, so fields
// written by other builds are preserved.
void write_message_fields(json& entry, const ConversationMessage& message)
{
    entry["id"]      = message.id;
    entry["role"]    = role_name(message.role);
    entry["state"]   = message_state_name(message.state);
    entry["text"]    = message.text;
    entry["attempt"] = message.attempt;
    if (!message.client_message_id.empty())
        entry["clientMessageId"] = message.client_message_id;
    if (!message.in_reply_to.empty())
        entry["inReplyTo"] = message.in_reply_to;
    if (message.error)
        entry["error"] = json{{"code", message.error->code}, {"message", message.error->message},
                              {"retryable", message.error->retryable}};
    else
        entry.erase("error");
    if (!message.attachment_ids.empty())
        entry["attachments"] = message.attachment_ids;
    else
        entry.erase("attachments");
}

ConversationMessage read_message(const json& entry)
{
    ConversationMessage message;
    message.id                = entry.value("id", "");
    message.role              = role_from(entry.value("role", "assistant"));
    message.state             = message_state_from(entry.value("state", "complete"));
    message.text              = entry.value("text", "");
    message.attempt           = entry.value("attempt", 1);
    message.client_message_id = entry.value("clientMessageId", "");
    message.in_reply_to       = entry.value("inReplyTo", "");
    if (entry.contains("error") && entry["error"].is_object())
        message.error = AgentError{entry["error"].value("code", ""), entry["error"].value("message", ""),
                                   entry["error"].value("retryable", false)};
    if (entry.contains("attachments") && entry["attachments"].is_array())
        for (const json& id : entry["attachments"])
            if (id.is_string())
                message.attachment_ids.push_back(id.get<std::string>());
    return message;
}

void write_attachment_fields(json& entry, const AttachmentRecord& record)
{
    entry["id"]           = record.id;
    if (!record.client_id.empty())
        entry["clientId"] = record.client_id;
    entry["originalName"] = record.original_name;
    entry["storedName"]   = record.stored_name;
    entry["kind"]         = record.kind;
    entry["mime"]         = record.mime;
    entry["sizeBytes"]    = record.size_bytes;
    entry["source"]       = record.source;
    entry["state"]        = record.state;
    if (!record.preview_text.empty())
        entry["previewText"] = record.preview_text;
    else
        entry.erase("previewText");
    if (!record.preview_data_url.empty())
        entry["previewDataUrl"] = record.preview_data_url;
    else
        entry.erase("previewDataUrl");
    if (!record.summary.empty())
        entry["summary"] = record.summary;
    else
        entry.erase("summary");
    if (!record.content_hash.empty())
        entry["contentHash"] = record.content_hash;
    else
        entry.erase("contentHash");
    if (record.error)
        entry["error"] = json{{"code", record.error->code}, {"message", record.error->message}};
    else
        entry.erase("error");
}

AttachmentRecord read_attachment(const json& entry)
{
    AttachmentRecord record;
    record.id               = entry.value("id", "");
    record.seq              = entry.value("seq", std::uint64_t(0));
    record.client_id        = entry.value("clientId", "");
    record.original_name    = entry.value("originalName", "");
    record.stored_name      = entry.value("storedName", "");
    record.kind             = entry.value("kind", "unsupported");
    record.mime             = entry.value("mime", "");
    record.size_bytes       = entry.value("sizeBytes", std::uint64_t(0));
    record.source           = entry.value("source", "picker");
    record.state            = entry.value("state", "staged");
    record.preview_text     = entry.value("previewText", "");
    record.preview_data_url = entry.value("previewDataUrl", "");
    record.summary          = entry.value("summary", "");
    record.content_hash     = entry.value("contentHash", "");
    if (entry.contains("error") && entry["error"].is_object())
        record.error = AgentError{entry["error"].value("code", ""), entry["error"].value("message", ""), false};
    return record;
}

json statistics_json(const SliceStatistics& statistics)
{
    return json{{"printTimeSeconds", statistics.print_time_seconds},
                {"filamentMm", statistics.filament_mm},
                {"materialGrams", statistics.material_grams},
                {"materialCost", statistics.material_cost},
                {"layerCount", statistics.layer_count}};
}

SliceStatistics read_statistics(const json& entry)
{
    SliceStatistics statistics;
    if (!entry.is_object())
        return statistics;
    statistics.print_time_seconds = entry.value("printTimeSeconds", 0.0);
    statistics.filament_mm        = entry.value("filamentMm", 0.0);
    statistics.material_grams     = entry.value("materialGrams", 0.0);
    statistics.material_cost      = entry.value("materialCost", 0.0);
    statistics.layer_count        = entry.value("layerCount", std::uint64_t(0));
    return statistics;
}

BuildRecord read_build(const json& entry)
{
    BuildRecord record;
    record.id                       = entry.value("id", "");
    record.seq                      = entry.value("seq", std::uint64_t(0));
    record.created_at               = entry.value("createdAt", "");
    record.project_id               = entry.value("projectId", "");
    record.revision_id              = entry.value("revisionId", "");
    record.conversation_id          = entry.value("conversationId", "");
    record.after_message_id         = entry.value("afterMessageId", "");
    record.plate_index              = entry.value("plateIndex", std::size_t(0));
    record.plate_name               = entry.value("plateName", "");
    record.printer                  = entry.value("printer", "");
    record.material                 = entry.value("material", "");
    record.manufacturing_input_hash = entry.value("manufacturingInputHash", "");
    record.output_hash              = entry.value("outputHash", "");
    record.slicer_version           = entry.value("slicerVersion", "");
    record.configuration_provenance = entry.value("configurationProvenance", "");
    if (entry.contains("statistics"))
        record.statistics = read_statistics(entry["statistics"]);
    if (entry.contains("warnings") && entry["warnings"].is_array())
        for (const json& warning : entry["warnings"])
            if (warning.is_string())
                record.warnings.push_back(warning.get<std::string>());
    return record;
}

ExportedCopyRecord read_exported_copy(const json& entry)
{
    ExportedCopyRecord record;
    record.id                   = entry.value("id", "");
    record.seq                  = entry.value("seq", std::uint64_t(0));
    record.created_at           = entry.value("createdAt", "");
    record.build_id             = entry.value("buildId", "");
    record.conversation_id      = entry.value("conversationId", "");
    record.after_message_id     = entry.value("afterMessageId", "");
    record.destination          = entry.value("destination", "");
    record.expected_output_hash = entry.value("expectedOutputHash", "");
    record.observed_output_hash = entry.value("observedOutputHash", "");
    return record;
}

PhysicalPrintRecord read_physical_print(const json& entry)
{
    PhysicalPrintRecord record;
    record.id                       = entry.value("id", "");
    record.seq                      = entry.value("seq", std::uint64_t(0));
    record.started_at               = entry.value("startedAt", "");
    record.ended_at                 = entry.value("endedAt", "");
    record.outcome                  = entry.value("outcome", "");
    record.failure                  = entry.value("failure", "");
    record.build_id                 = entry.value("buildId", "");
    record.project_id               = entry.value("projectId", "");
    record.revision_id              = entry.value("revisionId", "");
    record.conversation_id          = entry.value("conversationId", "");
    record.after_message_id         = entry.value("afterMessageId", "");
    record.plate_index              = entry.value("plateIndex", std::size_t(0));
    record.plate_name               = entry.value("plateName", "");
    record.printer                  = entry.value("printer", "");
    record.material                 = entry.value("material", "");
    record.manufacturing_input_hash = entry.value("manufacturingInputHash", "");
    record.output_hash              = entry.value("outputHash", "");
    record.gcode_hash               = entry.value("gcodeHash", "");
    if (entry.contains("statistics"))
        record.statistics = read_statistics(entry["statistics"]);
    return record;
}

void write_activity_fields(json& entry, const ToolActivity& activity)
{
    entry["actionId"]         = activity.action_id;
    entry["correlationId"]    = activity.correlation_id;
    entry["server"]           = activity.server;
    entry["tool"]             = activity.tool;
    entry["title"]            = activity.title;
    entry["arguments"]        = activity.arguments_json;
    entry["actionClass"]      = action_class_name(activity.action_class);
    entry["requiresApproval"] = activity.requires_approval;
    entry["sessionId"]        = std::to_string(activity.session);
    entry["expectedRevision"] = activity.expected_revision;
    entry["state"]            = tool_state_name(activity.state);
    entry["progress"]         = json{{"current", activity.progress_current}, {"total", activity.progress_total}};
    if (!activity.result_json.empty())
        entry["result"] = activity.result_json;
    if (activity.error)
        entry["error"] = json{{"code", activity.error->code}, {"message", activity.error->message}};
    else
        entry.erase("error");
}

ToolActivity read_activity(const json& entry)
{
    ToolActivity activity;
    activity.action_id         = entry.value("actionId", "");
    activity.correlation_id    = entry.value("correlationId", "");
    activity.server            = entry.value("server", "");
    activity.tool              = entry.value("tool", "");
    activity.title             = entry.value("title", "");
    activity.arguments_json    = entry.value("arguments", "");
    activity.action_class      = action_class_from(entry.value("actionClass", "read_only"));
    activity.requires_approval = entry.value("requiresApproval", false);
    try {
        activity.session = std::stoull(entry.value("sessionId", "0"));
    } catch (const std::exception&) {
        activity.session = 0;
    }
    activity.expected_revision = entry.value("expectedRevision", std::uint64_t(0));
    activity.state             = tool_state_from(entry.value("state", "pending"));
    if (entry.contains("progress") && entry["progress"].is_object()) {
        activity.progress_current = entry["progress"].value("current", 0);
        activity.progress_total   = entry["progress"].value("total", 1);
    }
    activity.result_json = entry.value("result", "");
    if (entry.contains("error") && entry["error"].is_object())
        activity.error = ToolError{entry["error"].value("code", ""), entry["error"].value("message", "")};
    return activity;
}

RevisionInfo read_revision(const json& entry)
{
    RevisionInfo revision;
    revision.id               = entry.value("id", "");
    revision.seq              = entry.value("seq", std::uint64_t(0));
    revision.created_at       = entry.value("createdAt", "");
    revision.cause            = entry.value("cause", "");
    revision.snapshot_file    = entry.value("snapshotFile", "");
    revision.conversation_id  = entry.value("conversationId", "");
    revision.after_message_id = entry.value("afterMessageId", "");
    return revision;
}

json fresh_document()
{
    return json{{"schemaVersion", ProjectStateDocument::kSchemaVersion},
                {"docRevision", std::uint64_t(0)},
                {"project", json{{"projectId", ""}, {"lineageId", ""}, {"createdAt", ""}}},
                {"counters", json{{"nextSeq", std::uint64_t(1)}, {"nextMessage", std::uint64_t(1)},
                                  {"nextAction", std::uint64_t(1)}, {"nextConversation", std::uint64_t(1)},
                                  {"nextRevision", std::uint64_t(1)}, {"nextAttachment", std::uint64_t(1)},
                                  {"nextBuild", std::uint64_t(1)}, {"nextExport", std::uint64_t(1)},
                                  {"nextPrint", std::uint64_t(1)}}},
                {"activeConversationId", ""},
                {"currentRevisionId", ""},
                {"conversations", json::array()},
                {"toolActivities", json::array()},
                {"attachments", json::array()},
                {"revisions", json::array()},
                {"builds", json::array()},
                {"exportedCopies", json::array()},
                {"physicalPrints", json::array()}};
}

} // namespace

ProjectStateDocument::ProjectStateDocument() : m_doc(fresh_document()) {}

ProjectStateDocument::LoadResult ProjectStateDocument::load(const std::string& json_text)
{
    json parsed = json::parse(json_text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("schemaVersion") ||
        !parsed["schemaVersion"].is_number_integer()) {
        m_doc = fresh_document();
        return LoadResult::Corrupt;
    }
    const int version = parsed["schemaVersion"].get<int>();
    if (version > kSchemaVersion) {
        // A newer document must not be edited by an older build that would
        // silently drop meaning; treat it like corrupt state.
        m_doc = fresh_document();
        return LoadResult::Corrupt;
    }

    // Merge onto the fresh skeleton so documents from older schema versions
    // gain any structure they predate while keeping everything they carry.
    json result = fresh_document();
    for (auto& [key, value] : parsed.items())
        result[key] = std::move(value);
    result["schemaVersion"] = kSchemaVersion;
    m_doc                   = std::move(result);
    return version < kSchemaVersion ? LoadResult::Migrated : LoadResult::Loaded;
}

std::string ProjectStateDocument::dump() const { return m_doc.dump(2); }

std::uint64_t ProjectStateDocument::doc_revision() const { return m_doc.value("docRevision", std::uint64_t(0)); }

void ProjectStateDocument::touch() { m_doc["docRevision"] = doc_revision() + 1; }

bool ProjectStateDocument::has_identity() const { return !project_id().empty(); }

std::string ProjectStateDocument::project_id() const
{
    return m_doc.contains("project") ? m_doc["project"].value("projectId", "") : std::string();
}

std::string ProjectStateDocument::lineage_id() const
{
    return m_doc.contains("project") ? m_doc["project"].value("lineageId", "") : std::string();
}

void ProjectStateDocument::initialize_identity(const std::string& project_id,
                                               const std::string& lineage_id,
                                               const std::string& timestamp)
{
    m_doc["project"]["projectId"] = project_id;
    m_doc["project"]["lineageId"] = lineage_id;
    m_doc["project"]["createdAt"] = timestamp;
    create_conversation("Conversation 1", timestamp);
}

std::vector<ConversationInfo> ProjectStateDocument::conversations() const
{
    std::vector<ConversationInfo> result;
    for (const json& entry : m_doc["conversations"])
        result.push_back(ConversationInfo{entry.value("id", ""), entry.value("title", ""), entry.value("createdAt", "")});
    return result;
}

std::string ProjectStateDocument::active_conversation_id() const { return m_doc.value("activeConversationId", ""); }

std::string ProjectStateDocument::create_conversation(const std::string& title, const std::string& timestamp)
{
    const std::uint64_t number = m_doc["counters"].value("nextConversation", std::uint64_t(1));
    m_doc["counters"]["nextConversation"] = number + 1;
    const std::string id = "c-" + std::to_string(number);
    m_doc["conversations"].push_back(json{{"id", id},
                                          {"seq", next_seq()},
                                          {"title", title.empty() ? "Conversation " + std::to_string(number) : title},
                                          {"createdAt", timestamp},
                                          {"messages", json::array()}});
    m_doc["activeConversationId"] = id;
    touch();
    return id;
}

bool ProjectStateDocument::set_active_conversation(const std::string& conversation_id)
{
    if (conversation_json(conversation_id) == nullptr)
        return false;
    if (active_conversation_id() == conversation_id)
        return true;
    m_doc["activeConversationId"] = conversation_id;
    touch();
    return true;
}

nlohmann::json* ProjectStateDocument::conversation_json(const std::string& conversation_id)
{
    for (json& entry : m_doc["conversations"])
        if (entry.value("id", "") == conversation_id)
            return &entry;
    return nullptr;
}

const nlohmann::json* ProjectStateDocument::conversation_json(const std::string& conversation_id) const
{
    for (const json& entry : m_doc["conversations"])
        if (entry.value("id", "") == conversation_id)
            return &entry;
    return nullptr;
}

std::uint64_t ProjectStateDocument::next_seq()
{
    const std::uint64_t seq = m_doc["counters"].value("nextSeq", std::uint64_t(1));
    m_doc["counters"]["nextSeq"] = seq + 1;
    return seq;
}

std::string ProjectStateDocument::allocate_message_id()
{
    const std::uint64_t number = m_doc["counters"].value("nextMessage", std::uint64_t(1));
    m_doc["counters"]["nextMessage"] = number + 1;
    touch();
    return "m-" + std::to_string(number);
}

std::string ProjectStateDocument::allocate_action_id()
{
    const std::uint64_t number = m_doc["counters"].value("nextAction", std::uint64_t(1));
    m_doc["counters"]["nextAction"] = number + 1;
    touch();
    return "t-" + std::to_string(number);
}

std::string ProjectStateDocument::allocate_attachment_id()
{
    const std::uint64_t number = m_doc["counters"].value("nextAttachment", std::uint64_t(1));
    m_doc["counters"]["nextAttachment"] = number + 1;
    touch();
    return "a-" + std::to_string(number);
}

void ProjectStateDocument::append_message(const std::string&         conversation_id,
                                          const ConversationMessage& message,
                                          const std::string&         timestamp)
{
    json* conversation = conversation_json(conversation_id);
    if (conversation == nullptr)
        return;
    json entry;
    write_message_fields(entry, message);
    entry["seq"]       = next_seq();
    entry["createdAt"] = timestamp;
    (*conversation)["messages"].push_back(std::move(entry));
    touch();
}

bool ProjectStateDocument::update_message(const std::string& conversation_id, const ConversationMessage& message)
{
    json* conversation = conversation_json(conversation_id);
    if (conversation == nullptr)
        return false;
    for (json& entry : (*conversation)["messages"])
        if (entry.value("id", "") == message.id) {
            write_message_fields(entry, message);
            touch();
            return true;
        }
    return false;
}

std::vector<ConversationMessage> ProjectStateDocument::messages(const std::string& conversation_id) const
{
    std::vector<ConversationMessage> result;
    const json* conversation = conversation_json(conversation_id);
    if (conversation == nullptr)
        return result;
    for (const json& entry : (*conversation)["messages"])
        result.push_back(read_message(entry));
    return result;
}

std::string ProjectStateDocument::conversation_of_message(const std::string& message_id) const
{
    for (const json& conversation : m_doc["conversations"])
        for (const json& entry : conversation["messages"])
            if (entry.value("id", "") == message_id)
                return conversation.value("id", "");
    return {};
}

std::optional<std::string> ProjectStateDocument::client_message_lookup(const std::string& client_message_id) const
{
    for (const json& conversation : m_doc["conversations"])
        for (const json& entry : conversation["messages"])
            if (entry.value("clientMessageId", "") == client_message_id)
                return entry.value("id", "");
    return std::nullopt;
}

void ProjectStateDocument::upsert_activity(const ToolActivity& activity, const std::string& timestamp)
{
    for (json& entry : m_doc["toolActivities"])
        if (entry.value("actionId", "") == activity.action_id) {
            write_activity_fields(entry, activity);
            entry["updatedAt"] = timestamp;
            touch();
            return;
        }
    json entry;
    write_activity_fields(entry, activity);
    entry["seq"]       = next_seq();
    entry["createdAt"] = timestamp;
    m_doc["toolActivities"].push_back(std::move(entry));
    touch();
}

std::vector<ToolActivity> ProjectStateDocument::activities() const
{
    std::vector<ToolActivity> result;
    for (const json& entry : m_doc["toolActivities"])
        result.push_back(read_activity(entry));
    return result;
}

void ProjectStateDocument::add_attachment(const AttachmentRecord& record, const std::string& timestamp)
{
    json entry;
    write_attachment_fields(entry, record);
    entry["seq"]       = next_seq();
    entry["createdAt"] = timestamp;
    m_doc["attachments"].push_back(std::move(entry));
    touch();
}

bool ProjectStateDocument::update_attachment(const AttachmentRecord& record)
{
    for (json& entry : m_doc["attachments"])
        if (entry.value("id", "") == record.id) {
            write_attachment_fields(entry, record);
            touch();
            return true;
        }
    return false;
}

std::vector<AttachmentRecord> ProjectStateDocument::attachments() const
{
    std::vector<AttachmentRecord> result;
    for (const json& entry : m_doc["attachments"])
        result.push_back(read_attachment(entry));
    return result;
}

std::optional<AttachmentRecord> ProjectStateDocument::find_attachment(const std::string& attachment_id) const
{
    for (const json& entry : m_doc["attachments"])
        if (entry.value("id", "") == attachment_id)
            return read_attachment(entry);
    return std::nullopt;
}

std::optional<AttachmentRecord> ProjectStateDocument::find_attachment_by_client_id(const std::string& client_id) const
{
    if (client_id.empty())
        return std::nullopt;
    for (const json& entry : m_doc["attachments"])
        if (entry.value("clientId", "") == client_id)
            return read_attachment(entry);
    return std::nullopt;
}

std::optional<std::string> ProjectStateDocument::remove_staged_attachment(const std::string& attachment_id)
{
    json& attachments = m_doc["attachments"];
    for (auto it = attachments.begin(); it != attachments.end(); ++it)
        if (it->value("id", "") == attachment_id) {
            if (it->value("state", "") != "staged" && it->value("state", "") != "error")
                return std::nullopt; // a sent attachment is durable history
            const std::string dir = read_attachment(*it).relative_dir();
            attachments.erase(it);
            touch();
            return dir;
        }
    return std::nullopt;
}

std::vector<std::string> ProjectStateDocument::mark_attachments_sent(const std::vector<std::string>& attachment_ids)
{
    std::vector<std::string> sent;
    for (const std::string& id : attachment_ids)
        for (json& entry : m_doc["attachments"])
            if (entry.value("id", "") == id && entry.value("state", "") == "staged") {
                entry["state"] = "sent";
                sent.push_back(id);
                break;
            }
    if (!sent.empty())
        touch();
    return sent;
}

std::string ProjectStateDocument::add_build(BuildRecord record, const std::string& timestamp)
{
    const std::uint64_t number = m_doc["counters"].value("nextBuild", std::uint64_t(1));
    m_doc["counters"]["nextBuild"] = number + 1;
    record.id = "b-" + std::to_string(number);
    json entry{{"id", record.id},
               {"seq", next_seq()},
               {"createdAt", timestamp},
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
               {"warnings", record.warnings}};
    m_doc["builds"].push_back(std::move(entry));
    touch();
    return record.id;
}

std::string ProjectStateDocument::add_exported_copy(ExportedCopyRecord record, const std::string& timestamp)
{
    const std::uint64_t number = m_doc["counters"].value("nextExport", std::uint64_t(1));
    m_doc["counters"]["nextExport"] = number + 1;
    record.id = "e-" + std::to_string(number);
    m_doc["exportedCopies"].push_back(json{{"id", record.id},
                                           {"seq", next_seq()},
                                           {"createdAt", timestamp},
                                           {"buildId", record.build_id},
                                           {"conversationId", record.conversation_id},
                                           {"afterMessageId", record.after_message_id},
                                           {"destination", record.destination},
                                           {"expectedOutputHash", record.expected_output_hash},
                                           {"observedOutputHash", record.observed_output_hash}});
    touch();
    return record.id;
}

std::string ProjectStateDocument::add_physical_print(PhysicalPrintRecord record, const std::string& timestamp)
{
    const std::uint64_t number = m_doc["counters"].value("nextPrint", std::uint64_t(1));
    m_doc["counters"]["nextPrint"] = number + 1;
    record.id = "p-" + std::to_string(number);
    if (record.started_at.empty())
        record.started_at = timestamp;
    if (record.ended_at.empty())
        record.ended_at = timestamp;
    m_doc["physicalPrints"].push_back(json{{"id", record.id},
                                           {"seq", next_seq()},
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
                                           {"statistics", statistics_json(record.statistics)}});
    touch();
    return record.id;
}

std::vector<BuildRecord> ProjectStateDocument::builds() const
{
    std::vector<BuildRecord> result;
    for (const json& entry : m_doc["builds"])
        result.push_back(read_build(entry));
    return result;
}

std::vector<ExportedCopyRecord> ProjectStateDocument::exported_copies() const
{
    std::vector<ExportedCopyRecord> result;
    for (const json& entry : m_doc["exportedCopies"])
        result.push_back(read_exported_copy(entry));
    return result;
}

std::vector<PhysicalPrintRecord> ProjectStateDocument::physical_prints() const
{
    std::vector<PhysicalPrintRecord> result;
    for (const json& entry : m_doc["physicalPrints"])
        result.push_back(read_physical_print(entry));
    return result;
}

std::size_t ProjectStateDocument::physical_print_count() const
{
    return m_doc["physicalPrints"].size();
}

std::optional<BuildRecord> ProjectStateDocument::find_build(const std::string& build_id) const
{
    for (const json& entry : m_doc["builds"])
        if (entry.value("id", "") == build_id)
            return read_build(entry);
    return std::nullopt;
}

std::optional<BuildRecord> ProjectStateDocument::latest_build() const
{
    if (m_doc["builds"].empty())
        return std::nullopt;
    return read_build(m_doc["builds"].back());
}

bool ProjectStateDocument::normalize_interrupted_state()
{
    bool changed = false;
    for (json& conversation : m_doc["conversations"])
        for (json& entry : conversation["messages"])
            if (entry.value("state", "") == "streaming") {
                entry["state"] = "stopped";
                changed        = true;
            }
    for (json& entry : m_doc["toolActivities"]) {
        const ToolState state = tool_state_from(entry.value("state", "pending"));
        if (!tool_state_terminal(state)) {
            entry["state"] = "cancelled";
            changed        = true;
        }
    }
    if (changed)
        touch();
    return changed;
}

std::string ProjectStateDocument::peek_next_revision_id() const
{
    return "r-" + std::to_string(m_doc["counters"].value("nextRevision", std::uint64_t(1)));
}

std::string ProjectStateDocument::add_revision(const std::string& cause,
                                               const std::string& snapshot_file,
                                               const std::string& conversation_id,
                                               const std::string& timestamp)
{
    const std::uint64_t number = m_doc["counters"].value("nextRevision", std::uint64_t(1));
    m_doc["counters"]["nextRevision"] = number + 1;
    const std::string id = "r-" + std::to_string(number);

    std::string after_message_id;
    if (const json* conversation = conversation_json(conversation_id);
        conversation != nullptr && !(*conversation)["messages"].empty())
        after_message_id = (*conversation)["messages"].back().value("id", "");

    m_doc["revisions"].push_back(json{{"id", id},
                                      {"seq", next_seq()},
                                      {"createdAt", timestamp},
                                      {"cause", cause},
                                      {"snapshotFile", snapshot_file},
                                      {"conversationId", conversation_id},
                                      {"afterMessageId", after_message_id}});
    m_doc["currentRevisionId"] = id;
    touch();
    return id;
}

std::vector<RevisionInfo> ProjectStateDocument::revisions() const
{
    std::vector<RevisionInfo> result;
    for (const json& entry : m_doc["revisions"])
        result.push_back(read_revision(entry));
    return result;
}

std::optional<RevisionInfo> ProjectStateDocument::find_revision(const std::string& revision_id) const
{
    for (const json& entry : m_doc["revisions"])
        if (entry.value("id", "") == revision_id)
            return read_revision(entry);
    return std::nullopt;
}

std::string ProjectStateDocument::current_revision_id() const { return m_doc.value("currentRevisionId", ""); }

bool ProjectStateDocument::set_revision_snapshot(const std::string& revision_id, const std::string& snapshot_file)
{
    for (json& entry : m_doc["revisions"])
        if (entry.value("id", "") == revision_id && entry.value("snapshotFile", "").empty()) {
            entry["snapshotFile"] = snapshot_file;
            touch();
            return true;
        }
    return false;
}

std::optional<ProjectStateDocument::TruncateResult> ProjectStateDocument::revert_to_revision(const std::string& revision_id)
{
    const std::optional<RevisionInfo> target = find_revision(revision_id);
    if (!target)
        return std::nullopt;
    const std::uint64_t cutoff = target->seq;

    TruncateResult result;
    // Conversations created after the cutoff are later editable entries too.
    json& conversations = m_doc["conversations"];
    conversations.erase(std::remove_if(conversations.begin(), conversations.end(),
                                       [cutoff](const json& entry) {
                                           return entry.value("seq", std::uint64_t(0)) > cutoff;
                                       }),
                        conversations.end());
    for (json& conversation : conversations) {
        json& messages = conversation["messages"];
        messages.erase(std::remove_if(messages.begin(), messages.end(),
                                      [cutoff](const json& entry) { return entry.value("seq", std::uint64_t(0)) > cutoff; }),
                       messages.end());
    }
    json& activities = m_doc["toolActivities"];
    activities.erase(std::remove_if(activities.begin(), activities.end(),
                                    [cutoff](const json& entry) { return entry.value("seq", std::uint64_t(0)) > cutoff; }),
                     activities.end());
    json& revisions = m_doc["revisions"];
    for (const json& entry : revisions) {
        const std::string file = entry.value("snapshotFile", "");
        if (file.empty())
            continue;
        if (entry.value("seq", std::uint64_t(0)) > cutoff)
            result.removed_snapshot_files.push_back(file);
        else
            result.kept_snapshot_files.push_back(file);
    }
    revisions.erase(std::remove_if(revisions.begin(), revisions.end(),
                                   [cutoff](const json& entry) { return entry.value("seq", std::uint64_t(0)) > cutoff; }),
                    revisions.end());

    // Builds and exported copies are editable timeline entries. A copy also
    // disappears if its source build no longer exists after truncation.
    json& builds = m_doc["builds"];
    builds.erase(std::remove_if(builds.begin(), builds.end(),
                                [cutoff](const json& entry) {
                                    return entry.value("seq", std::uint64_t(0)) > cutoff;
                                }),
                 builds.end());
    std::set<std::string> kept_build_ids;
    for (const json& entry : builds)
        kept_build_ids.insert(entry.value("id", ""));
    json& copies = m_doc["exportedCopies"];
    copies.erase(std::remove_if(copies.begin(), copies.end(),
                                [cutoff, &kept_build_ids](const json& entry) {
                                    return entry.value("seq", std::uint64_t(0)) > cutoff ||
                                           kept_build_ids.find(entry.value("buildId", "")) == kept_build_ids.end();
                                }),
                 copies.end());
    // physicalPrints is intentionally untouched: it is a factual ledger, not
    // recoverable editable project state.

    // Attachments: keep only sent blobs still owned by a surviving message.
    // Staged/error records are unsent composer working state and a destructive
    // Revert discards them regardless of when they were staged.
    std::set<std::string> referenced;
    for (const json& conversation : conversations)
        for (const json& message : conversation["messages"])
            if (message.contains("attachments") && message["attachments"].is_array())
                for (const json& id : message["attachments"])
                    if (id.is_string())
                        referenced.insert(id.get<std::string>());
    json& attachments = m_doc["attachments"];
    for (const json& entry : attachments) {
        const AttachmentRecord record = read_attachment(entry);
        const std::uint64_t    seq    = entry.value("seq", std::uint64_t(0));
        const bool unsent             = record.state != "sent";
        const bool orphaned_sent      = record.state == "sent" && referenced.find(record.id) == referenced.end();
        if (seq > cutoff || unsent || orphaned_sent)
            result.removed_attachment_dirs.push_back(record.relative_dir());
        else
            result.kept_attachment_dirs.push_back(record.relative_dir());
    }
    attachments.erase(std::remove_if(attachments.begin(), attachments.end(),
                                     [cutoff, &referenced](const json& entry) {
                                         const std::string state = entry.value("state", "");
                                         const std::string id    = entry.value("id", "");
                                         const bool unsent = state != "sent";
                                         const bool orphaned_sent =
                                             state == "sent" && referenced.find(id) == referenced.end();
                                         return entry.value("seq", std::uint64_t(0)) > cutoff || unsent || orphaned_sent;
                                     }),
                      attachments.end());

    m_doc["currentRevisionId"] = revision_id;

    // The active conversation may have been created after the cutoff.
    bool active_exists = conversation_json(active_conversation_id()) != nullptr;
    if (!active_exists && !m_doc["conversations"].empty())
        m_doc["activeConversationId"] = m_doc["conversations"].front().value("id", "");

    touch();
    return result;
}

} // namespace Slic3r::GUI::JusPrin::Agent
