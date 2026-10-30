#include "ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Regions.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

const char* role_name(MessageRole role)
{
    return role == MessageRole::User ? "user" : role == MessageRole::Note ? "note" : "assistant";
}

MessageRole role_from(const std::string& name)
{
    if (name == "user") return MessageRole::User;
    if (name == "note") return MessageRole::Note;
    return MessageRole::Assistant;
}

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

ToolState tool_state_from(const std::string& name)
{
    if (name == "succeeded")
        return ToolState::Succeeded;
    if (name == "failed")
        return ToolState::Failed;
    if (name == "cancelled")
        return ToolState::Cancelled;
    if (name == "running")
        return ToolState::Running;
    throw std::invalid_argument("Unknown tool activity state");
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
    if (!message.swatch.empty())
        entry["swatch"] = message.swatch;
    else
        entry.erase("swatch");
    if (!message.file_report.empty()) {
        entry["fileReport"]     = message.file_report;
        entry["fileReportCard"] = message.file_report_card;
    } else {
        entry.erase("fileReport");
        entry.erase("fileReportCard");
    }
    if (!message.issue_context.empty())
        entry["issueContext"] = message.issue_context;
    else
        entry.erase("issueContext");
    if (!message.receipt.empty())
        entry["receipt"] = message.receipt;
    else
        entry.erase("receipt");
    if (!message.for_model.empty())
        entry["forModel"] = message.for_model;
    else
        entry.erase("forModel");
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
    message.created_at        = entry.value("createdAt", "");
    if (entry.contains("error") && entry["error"].is_object())
        message.error = AgentError{entry["error"].value("code", ""), entry["error"].value("message", ""),
                                   entry["error"].value("retryable", false)};
    if (entry.contains("attachments") && entry["attachments"].is_array())
        for (const json& id : entry["attachments"])
            if (id.is_string())
                message.attachment_ids.push_back(id.get<std::string>());
    message.swatch           = entry.value("swatch", "");
    message.file_report      = entry.value("fileReport", "");
    message.file_report_card = entry.value("fileReportCard", false);
    message.issue_context    = entry.value("issueContext", "");
    message.receipt          = entry.value("receipt", "");
    message.for_model        = entry.value("forModel", "");
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

const char* destination_kind_name(GcodeDestination::Kind kind)
{
    return kind == GcodeDestination::Kind::Send ? "send" : "export";
}

json destination_json(const GcodeDestination& destination)
{
    json entry{{"id", destination.id},
               {"seq", destination.seq},
               {"kind", destination_kind_name(destination.kind)},
               {"at", destination.at},
               {"conversationId", destination.conversation_id},
               {"afterId", destination.after_id}};
    if (destination.kind == GcodeDestination::Kind::Export) {
        entry["path"]     = destination.path;
        entry["fileHash"] = destination.file_hash;
    } else {
        entry["printerName"]    = destination.printer_name;
        entry["printerId"]      = destination.printer_id;
        entry["transport"]      = destination.transport;
        entry["fileName"]       = destination.file_name;
        entry["startRequested"] = destination.start_requested;
        entry["deliveredAt"]    = destination.delivered_at;
        if (!destination.package_hash.empty())
            entry["packageHash"] = destination.package_hash;
    }
    return entry;
}

GcodeDestination read_destination(const json& entry)
{
    GcodeDestination destination;
    destination.id              = entry.value("id", "");
    destination.seq             = entry.value("seq", std::uint64_t(0));
    destination.kind            = entry.value("kind", "") == "send" ? GcodeDestination::Kind::Send : GcodeDestination::Kind::Export;
    destination.at              = entry.value("at", "");
    destination.conversation_id = entry.value("conversationId", "");
    destination.after_id        = entry.value("afterId", "");
    destination.path            = entry.value("path", "");
    destination.file_hash       = entry.value("fileHash", "");
    destination.printer_name    = entry.value("printerName", "");
    destination.printer_id      = entry.value("printerId", "");
    destination.transport       = entry.value("transport", "");
    destination.file_name       = entry.value("fileName", "");
    destination.start_requested = entry.value("startRequested", false);
    destination.delivered_at    = entry.value("deliveredAt", "");
    destination.package_hash    = entry.value("packageHash", "");
    return destination;
}

json print_json(const GcodePrint& print)
{
    return json{{"id", print.id},
                {"destinationId", print.destination_id},
                {"printerName", print.printer_name},
                {"printerId", print.printer_id},
                {"jobId", print.job_id},
                {"startedAt", print.started_at},
                {"finishedAt", print.finished_at}};
}

GcodePrint read_print(const json& entry)
{
    GcodePrint print;
    print.id             = entry.value("id", "");
    print.destination_id = entry.value("destinationId", "");
    print.printer_name   = entry.value("printerName", "");
    print.printer_id     = entry.value("printerId", "");
    print.job_id         = entry.value("jobId", "");
    print.started_at     = entry.value("startedAt", "");
    print.finished_at    = entry.value("finishedAt", "");
    return print;
}

GcodeRecord read_gcode(const json& entry)
{
    GcodeRecord record;
    record.hash                     = entry.value("hash", "");
    record.seq                      = entry.value("seq", std::uint64_t(0));
    record.created_at               = entry.value("createdAt", "");
    record.plate_index              = entry.value("plateIndex", std::size_t(0));
    record.plate_name               = entry.value("plateName", "");
    record.printer                  = entry.value("printer", "");
    record.material                 = entry.value("material", "");
    record.process                  = entry.value("process", "");
    record.manufacturing_input_hash = entry.value("manufacturingInputHash", "");
    record.conversation_id          = entry.value("conversationId", "");
    record.after_id                 = entry.value("afterId", "");
    if (entry.contains("statistics"))
        record.statistics = read_statistics(entry["statistics"]);
    if (const auto destinations = entry.find("destinations"); destinations != entry.end() && destinations->is_array())
        for (const json& destination : *destinations)
            record.destinations.push_back(read_destination(destination));
    if (const auto prints = entry.find("prints"); prints != entry.end() && prints->is_array())
        for (const json& print : *prints)
            record.prints.push_back(read_print(print));
    return record;
}

void write_activity_fields(json& entry, const ToolActivity& activity)
{
    entry["actionId"]         = activity.action_id;
    entry["source"]           = activity.source == ToolSource::Mcp ? "mcp" : "agent";
    entry["correlationId"]    = activity.correlation_id;
    if (!activity.call_id.empty())
        entry["callId"] = activity.call_id;
    entry["server"]           = activity.server;
    entry["tool"]             = activity.tool;
    entry["title"]            = activity.title;
    entry["arguments"]        = activity.arguments_json;
    entry["actionClass"]      = action_class_name(activity.action_class);
    entry["sessionId"]        = std::to_string(activity.session);
    entry["expectedRevision"] = activity.expected_revision;
    entry["state"]            = tool_state_name(activity.state);
    entry["progress"]         = json{{"current", activity.progress_current}, {"total", activity.progress_total}};
    if (!activity.result_json.empty())
        entry["result"] = activity.result_json;
    if (activity.error)
        entry["error"] = json{{"code", activity.error->code}, {"message", activity.error->message},
                                {"details", json::parse(activity.error->details_json)}};
    else
        entry.erase("error");
    if (!activity.proposal_json.empty())
        entry["proposal"] = activity.proposal_json;
    if (!activity.approves.empty())
        entry["approves"] = activity.approves;
}

ToolActivity read_activity(const json& entry)
{
    ToolActivity activity;
    activity.action_id         = entry.value("actionId", "");
    activity.correlation_id    = entry.value("correlationId", "");
    const auto source = entry.at("source").get<std::string>();
    if (source != "mcp" && source != "agent") throw std::invalid_argument("Unknown tool activity source");
    activity.source = source == "mcp" ? ToolSource::Mcp : ToolSource::Agent;
    activity.call_id           = entry.value("callId", "");
    activity.server            = entry.value("server", "");
    activity.tool              = entry.value("tool", "");
    activity.title             = entry.value("title", "");
    activity.arguments_json    = entry.value("arguments", "");
    activity.action_class      = action_class_from(entry.value("actionClass", "read_only"));
    try {
        activity.session = std::stoull(entry.value("sessionId", "0"));
    } catch (const std::exception&) {
        activity.session = 0;
    }
    activity.expected_revision = entry.value("expectedRevision", std::uint64_t(0));
    activity.state             = tool_state_from(entry.value("state", "running"));
    if (entry.contains("progress") && entry["progress"].is_object()) {
        activity.progress_current = entry["progress"].value("current", 0);
        activity.progress_total   = entry["progress"].value("total", 1);
    }
    activity.result_json = entry.value("result", "");
    activity.proposal_json = entry.value("proposal", "");
    activity.approves      = entry.value("approves", "");
    if (entry.contains("error") && entry["error"].is_object())
        activity.error = ToolError{entry["error"].value("code", ""), entry["error"].value("message", ""),
                                   entry["error"].value("details", json::object()).dump()};
    return activity;
}

json fresh_document()
{
    return json{{"schemaVersion", ProjectStateDocument::kSchemaVersion},
                {"docRevision", std::uint64_t(0)},
                {"project", json{{"projectId", ""}, {"lineageId", ""}, {"createdAt", ""}}},
                {"counters", json{{"nextSeq", std::uint64_t(1)}, {"nextMessage", std::uint64_t(1)},
                                  {"nextAction", std::uint64_t(1)}, {"nextConversation", std::uint64_t(1)},
                                  {"nextAttachment", std::uint64_t(1)}, {"nextDestination", std::uint64_t(1)},
                                  {"nextPrint", std::uint64_t(1)}}},
                {"activeConversationId", ""},
                {"viewedConversationId", ""},
                {"draft", ""},
                {"conversations", json::array()},
                {"toolActivities", json::array()},
                {"attachments", json::array()},
                {"gcodes", json::array()},
                {"regions", json::array()},
                {"changes", json::array()},
                {"work", json::array()}};
}

WorkEntry read_work(const json& entry)
{
    WorkEntry work;
    work.seq             = entry.value("seq", std::uint64_t(0));
    work.id              = entry.value("id", "");
    work.started_at      = entry.value("startedAt", "");
    work.ended_at        = entry.value("endedAt", "");
    work.kind            = entry.value("kind", "");
    work.title           = entry.value("title", "");
    work.state           = entry.value("state", "");
    work.detail          = entry.value("detail", "");
    work.conversation_id = entry.value("conversationId", "");
    work.after_id        = entry.value("afterId", "");
    return work;
}

ChangeEntry read_change(const json& entry)
{
    ChangeEntry change;
    change.seq             = entry.value("seq", std::uint64_t(0));
    change.created_at      = entry.value("createdAt", "");
    change.kind            = entry.value("kind", "");
    change.actor           = entry.value("actor", "");
    change.external        = entry.value("external", false);
    change.client          = entry.value("client", "");
    change.label           = entry.value("label", "");
    change.location        = entry.value("location", "");
    change.from            = entry.value("from", "");
    change.to              = entry.value("to", "");
    change.preset          = entry.value("preset", "");
    change.key             = entry.value("key", "");
    change.conversation_id = entry.value("conversationId", "");
    change.after_id        = entry.value("afterId", "");
    return change;
}

// What a new chat copies from the chat it starts from: its intent, and none
// of its plan.
json chat_intent(const json& chat)
{
    json intent{{"setupIntent", chat.value("setupIntent", "")}};
    const json setup = chat.value("purposeSetup", json::object());
    if (setup.is_object() && !setup.empty())
        intent["purposeSetup"] = setup;
    return intent;
}

PlanRecord read_plan(const json& stored)
{
    PlanRecord record;
    if (!stored.is_object())
        return record;
    record.seq            = stored.value("seq", std::uint64_t(0));
    record.updated_at     = stored.value("updatedAt", "");
    record.headline       = stored.value("headline", "");
    record.validity       = stored.value("validity", "current");
    record.invalidated_by = stored.value("invalidatedBy", "");
    if (stored.contains("decisions") && stored["decisions"].is_array())
        for (const json& entry : stored["decisions"])
            record.decisions.push_back({entry.value("topic", ""), entry.value("statement", ""),
                                        entry.value("confidence", ""), entry.value("alternative", "")});
    const auto read_lines = [&stored](const char* key, std::vector<std::string>& into) {
        if (!stored.contains(key) || !stored[key].is_array())
            return;
        for (const json& entry : stored[key])
            if (entry.is_string())
                into.push_back(entry.get<std::string>());
    };
    read_lines("assumptions", record.assumptions);
    read_lines("risks", record.risks);
    return record;
}

} // namespace

// A chat's record as state.json keeps it: every field, a picture's blob path
// but never its bytes.
NLOHMANN_JSON_SERIALIZE_ENUM(RecordPart::Kind, {{RecordPart::Kind::Text, "text"}, {RecordPart::Kind::Image, "image"}, {RecordPart::Kind::Pdf, "pdf"}})
NLOHMANN_JSON_SERIALIZE_ENUM(RecordItem::Kind, {{RecordItem::Kind::User, "user"}, {RecordItem::Kind::Operator, "operator"},
                                                {RecordItem::Kind::Assistant, "assistant"}, {RecordItem::Kind::Results, "results"}})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RecordPart, kind, text, mime, filename, blob)
void to_json(json& entry, const RecordResult& result)
{
    entry = json{{"callId", result.call_id}, {"output", result.output_json}, {"failed", result.failed}, {"image", nullptr}};
    if (result.image)
        entry["image"] = *result.image;
}
void from_json(const json& entry, RecordResult& result)
{
    result = RecordResult{entry.at("callId").get<std::string>(), entry.at("output").get<std::string>(), entry.at("failed").get<bool>(), std::nullopt};
    if (!entry.at("image").is_null())
        result.image = entry.at("image").get<RecordPart>();
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RecordItem, kind, source, parts, text, output, results, clears)

ProjectStateDocument::ProjectStateDocument() : m_doc(fresh_document()) {}

ProjectStateDocument::LoadResult ProjectStateDocument::load(const std::string& json_text)
{
    json parsed = json::parse(json_text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("schemaVersion") ||
        !parsed["schemaVersion"].is_number_integer()) {
        m_doc = fresh_document();
        return LoadResult::Corrupt;
    }
    if (parsed["schemaVersion"].get<int>() != kSchemaVersion) {
        // There is one schema and no migration: the project is not live yet.
        // Another version's document is not read, so nothing written by a
        // different build can be edited with its meaning silently dropped.
        m_doc = fresh_document();
        return LoadResult::Corrupt;
    }
    m_doc = std::move(parsed);
    // Sent events and physical prints were replaced by the G-code record
    // before any build wrote them outside tests; they are dropped, not kept
    // beside the records they would contradict.
    m_doc.erase("sent");
    m_doc.erase("physicalPrints");
    if (m_doc.contains("counters") && m_doc["counters"].is_object())
        m_doc["counters"].erase("nextSent");
    return LoadResult::Loaded;
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
    create_conversation({}, timestamp);
}

void ProjectStateDocument::fork_identity(const std::string& project_id, const std::string& timestamp)
{
    if (!has_identity() || project_id.empty())
        throw std::logic_error("cannot fork a project without an identity");
    m_doc["project"]["projectId"] = project_id;
    m_doc["project"]["createdAt"] = timestamp;
    touch();
}

void ProjectStateDocument::set_draft(const std::string& draft)
{
    if (m_doc.value("draft", "") == draft)
        return;
    m_doc["draft"] = draft;
    touch();
}

std::string ProjectStateDocument::draft() const { return m_doc.value("draft", ""); }

std::vector<ConversationInfo> ProjectStateDocument::conversations() const
{
    std::vector<ConversationInfo> result;
    for (const json& entry : m_doc["conversations"]) {
        ConversationInfo info{entry.value("id", ""), entry.value("title", ""), entry.value("createdAt", "")};
        info.updated_at = info.created_at;
        info.activity_seq = entry.value("seq", std::uint64_t{0});
        for (const json& message : entry["messages"]) {
            info.updated_at = message.value("createdAt", info.updated_at);
            info.activity_seq = message.value("seq", info.activity_seq);
            const std::string text = message.value("text", "");
            if (!text.empty())
                info.preview = text;
            else if (message.contains("attachments") && !message["attachments"].empty())
                info.preview = "Attachment";
        }
        // Bound bridge metadata without splitting a UTF-8 code point.
        if (info.preview.size() > 512) {
            std::size_t end = 512;
            while ((static_cast<unsigned char>(info.preview[end]) & 0xc0) == 0x80) --end;
            info.preview.resize(end);
        }
        result.push_back(std::move(info));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.activity_seq > b.activity_seq;
    });
    return result;
}

std::string ProjectStateDocument::active_conversation_id() const { return m_doc.value("activeConversationId", ""); }

std::string ProjectStateDocument::viewed_conversation_id() const { return m_doc.value("viewedConversationId", ""); }

std::string ProjectStateDocument::create_conversation(const std::string& title, const std::string& timestamp)
{
    const json* active = conversation_json(active_conversation_id());
    return add_conversation(title, timestamp, active ? chat_intent(*active) : json::object());
}

std::string ProjectStateDocument::add_conversation(const std::string& title, const std::string& timestamp, json intent)
{
    const std::uint64_t number = m_doc["counters"].value("nextConversation", std::uint64_t(1));
    m_doc["counters"]["nextConversation"] = number + 1;
    const std::string id = "c-" + std::to_string(number);
    json chat{{"id", id},
              {"seq", next_seq()},
              {"title", title.empty() ? "New chat" : title},
              {"titleSource", title.empty() ? "default" : "manual"},
              {"createdAt", timestamp},
              {"setupIntent", intent.value("setupIntent", "")},
              {"messages", json::array()}};
    if (intent.contains("purposeSetup"))
        chat["purposeSetup"] = intent["purposeSetup"];
    m_doc["conversations"].push_back(std::move(chat));
    m_doc["activeConversationId"] = id;
    m_doc["viewedConversationId"] = id;
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

bool ProjectStateDocument::set_viewed_conversation(const std::string& conversation_id)
{
    if (conversation_json(conversation_id) == nullptr)
        return false;
    if (viewed_conversation_id() == conversation_id)
        return true;
    m_doc["viewedConversationId"] = conversation_id;
    touch();
    return true;
}

std::optional<json> ProjectStateDocument::chat_checkpoint(const std::string& conversation_id) const
{
    const json* conversation = conversation_json(conversation_id);
    if (!conversation || !conversation->contains("resumeCheckpoint") ||
        !(*conversation)["resumeCheckpoint"].is_object())
        return std::nullopt;
    const json& record = (*conversation)["resumeCheckpoint"];
    if (!record.contains("versionId") || !record["versionId"].is_string() ||
        record["versionId"].get<std::string>().empty() || !record.contains("summary") ||
        !record["summary"].is_object() || !record.contains("boundaryId") ||
        !record["boundaryId"].is_string())
        return std::nullopt;
    return std::optional<json>(std::in_place, record);
}

bool ProjectStateDocument::save_chat_checkpoint(const std::string& conversation_id,
                                                 const std::string& version_id,
                                                 const json& summary,
                                                 const std::string& timestamp)
{
    json* conversation = conversation_json(conversation_id);
    if (!conversation || conversation_id != active_conversation_id() || version_id.empty() || !summary.is_object())
        return false;
    (*conversation)["resumeCheckpoint"] = json{{"versionId", version_id},
                                                  {"summary", summary},
                                                  {"boundaryId", last_item_id(conversation_id)},
                                                  {"savedAt", timestamp}};
    touch();
    return true;
}

bool ProjectStateDocument::needs_conversation_title(const std::string& conversation_id) const
{
    const json* conversation = conversation_json(conversation_id);
    return conversation && conversation->value("titleSource", "") == "default";
}

bool ProjectStateDocument::rename_conversation(const std::string& conversation_id, const std::string& title, bool generated)
{
    json* conversation = conversation_json(conversation_id);
    if (!conversation || (generated && !needs_conversation_title(conversation_id))) return false;
    const auto first = title.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    std::string cleaned = title.substr(first, title.find_last_not_of(" \t\r\n") - first + 1);
    const auto characters = std::count_if(cleaned.begin(), cleaned.end(), [](unsigned char byte) {
        return (byte & 0xc0) != 0x80;
    });
    if (characters > 120 || cleaned.find_first_of("\r\n") != std::string::npos) return false;
    (*conversation)["title"] = std::move(cleaned);
    (*conversation)["titleSource"] = generated ? "generated" : "manual";
    touch();
    return true;
}

std::string ProjectStateDocument::setup_intent(const std::string& conversation_id) const
{
    const json* conversation = conversation_json(conversation_id);
    return conversation ? conversation->value("setupIntent", "") : std::string();
}

std::optional<json> ProjectStateDocument::calibration() const
{
    if (!m_doc.contains("calibration") || !m_doc["calibration"].is_object())
        return std::nullopt;
    return std::optional<json>(std::in_place, m_doc["calibration"]);
}

void ProjectStateDocument::set_calibration(json calibration)
{
    m_doc["calibration"] = std::move(calibration);
    touch();
}

bool ProjectStateDocument::set_setup_intent(const std::string& conversation_id, const std::string& intent)
{
    json* conversation = conversation_json(conversation_id);
    if (!conversation) return false;
    const auto first = intent.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    std::string cleaned = intent.substr(first, intent.find_last_not_of(" \t\r\n") - first + 1);
    // Keep this a title rather than a paragraph; the card wraps it at narrow
    // widths without changing the saved wording.
    const auto characters = std::count_if(cleaned.begin(), cleaned.end(), [](unsigned char byte) {
        return (byte & 0xc0) != 0x80;
    });
    if (characters > 120 || cleaned.find_first_of("\r\n") != std::string::npos) return false;
    (*conversation)["setupIntent"] = std::move(cleaned);
    touch();
    return true;
}

std::optional<ChatModelChoice> ProjectStateDocument::chat_model(const std::string& conversation_id) const
{
    const json* conversation = conversation_json(conversation_id);
    if (!conversation || !conversation->contains("chatModel") || !(*conversation)["chatModel"].is_object())
        return std::nullopt;
    const json& stored = (*conversation)["chatModel"];
    ChatModelChoice choice{stored.value("provider", ""), stored.value("model", ""), stored.value("effort", ""),
                           stored.value("format", "")};
    if (choice.provider.empty() || choice.model.empty())
        return std::nullopt;
    return choice;
}

bool ProjectStateDocument::set_chat_model(const std::string& conversation_id, const ChatModelChoice& choice)
{
    json* conversation = conversation_json(conversation_id);
    if (!conversation || choice.provider.empty() || choice.model.empty()) return false;
    (*conversation)["chatModel"] = json{{"provider", choice.provider}, {"model", choice.model}, {"effort", choice.effort}};
    if (!choice.format.empty())
        (*conversation)["chatModel"]["format"] = choice.format;
    touch();
    return true;
}

std::optional<ChatRecord> ProjectStateDocument::chat_record(const std::string& conversation_id) const
{
    const json* conversation = conversation_json(conversation_id);
    if (!conversation || !conversation->contains("record"))
        return std::nullopt;
    const json& stored = conversation->at("record");
    return ChatRecord{stored.at("format").get<std::string>(), stored.at("items").get<std::vector<RecordItem>>()};
}

void ProjectStateDocument::append_chat_record(const std::string& conversation_id, const std::string& format,
                                              const std::vector<RecordItem>& items)
{
    json* conversation = conversation_json(conversation_id);
    if (!conversation || format.empty())
        throw std::logic_error("a record in no format, or for a chat the document does not hold");
    json& stored = (*conversation)["record"];
    if (stored.is_null())
        stored = json{{"format", format}, {"items", json::array()}};
    if (stored.at("format") != format)
        throw std::logic_error("a chat recorded in " + stored.at("format").get<std::string>() + " cannot take items in " + format);
    for (const RecordItem& item : items)
        stored["items"].push_back(item);
    touch();
}

std::optional<std::vector<std::string>> ProjectStateDocument::delete_conversation(const std::string& conversation_id,
                                                                                const std::string& timestamp)
{
    const json* conversation = conversation_json(conversation_id);
    if (!conversation) return std::nullopt;
    const json inherited = conversation_id == active_conversation_id() ? chat_intent(*conversation) : json::object();
    std::set<std::string> message_ids;
    std::set<std::string> attachment_ids;
    for (const json& message : (*conversation)["messages"]) {
        message_ids.insert(message.value("id", ""));
        for (const auto& id : message.value("attachments", json::array()))
            attachment_ids.insert(id.get<std::string>());
    }
    auto& chats = m_doc["conversations"];
    chats.erase(std::remove_if(chats.begin(), chats.end(), [&](const json& entry) {
        return entry.value("id", "") == conversation_id;
    }), chats.end());
    auto& activities = m_doc["toolActivities"];
    activities.erase(std::remove_if(activities.begin(), activities.end(), [&](const json& entry) {
        return message_ids.count(entry.value("correlationId", "")) != 0;
    }), activities.end());
    for (const json& chat : chats)
        for (const json& message : chat["messages"])
            for (const auto& id : message.value("attachments", json::array()))
                attachment_ids.erase(id.get<std::string>());
    std::vector<std::string> removed_dirs{chat_record_dir(conversation_id)};
    auto& attachments = m_doc["attachments"];
    attachments.erase(std::remove_if(attachments.begin(), attachments.end(), [&](const json& entry) {
        const std::string id = entry.value("id", "");
        if (!attachment_ids.count(id) || entry.value("state", "") == "staged") return false;
        removed_dirs.push_back("attachments/" + id);
        return true;
    }), attachments.end());
    if (chats.empty() || active_conversation_id() == conversation_id)
        add_conversation({}, timestamp, inherited);
    if (viewed_conversation_id() == conversation_id)
        m_doc["viewedConversationId"] = active_conversation_id();
    touch();
    return removed_dirs;
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

nlohmann::json& ProjectStateDocument::active_chat()
{
    json* chat = conversation_json(active_conversation_id());
    if (chat == nullptr)
        throw std::logic_error("the project has no active chat to hold its intent and plan");
    return *chat;
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

json* ProjectStateDocument::gcode_json(const std::string& hash)
{
    const auto entries = m_doc.find("gcodes");
    if (entries == m_doc.end())
        return nullptr;
    for (json& entry : *entries)
        if (entry.value("hash", "") == hash)
            return &entry;
    return nullptr;
}

std::string ProjectStateDocument::add_gcode_destination(GcodeRecord facts, GcodeDestination destination,
                                                        const std::string& timestamp)
{
    if (facts.hash.empty())
        throw std::logic_error("a G-code record needs the hash of its bytes");
    const std::uint64_t number = m_doc["counters"].value("nextDestination", std::uint64_t(1));
    m_doc["counters"]["nextDestination"] = number + 1;
    destination.id = "d-" + std::to_string(number);
    if (destination.at.empty())
        destination.at = timestamp;
    destination.seq                = next_seq();
    const std::string conversation = active_conversation_id();
    destination.conversation_id    = conversation;
    destination.after_id           = conversation.empty() ? std::string() : last_item_id(conversation);

    json* entry = gcode_json(facts.hash);
    if (entry == nullptr) {
        if (!m_doc.contains("gcodes"))
            m_doc["gcodes"] = json::array();
        m_doc["gcodes"].push_back(json{{"hash", facts.hash},
                                       {"seq", destination.seq},
                                       {"createdAt", timestamp},
                                       {"plateIndex", facts.plate_index},
                                       {"plateName", facts.plate_name},
                                       {"printer", facts.printer},
                                       {"material", facts.material},
                                       {"process", facts.process},
                                       {"manufacturingInputHash", facts.manufacturing_input_hash},
                                       {"conversationId", conversation},
                                       {"afterId", destination.after_id},
                                       {"statistics", statistics_json(facts.statistics)},
                                       {"destinations", json::array()},
                                       {"prints", json::array()}});
        entry = &m_doc["gcodes"].back();
    }
    (*entry)["destinations"].push_back(destination_json(destination));
    touch();
    return destination.id;
}

std::string ProjectStateDocument::add_gcode_print(const std::string& hash, GcodePrint print, const std::string& timestamp)
{
    json* entry = gcode_json(hash);
    if (entry == nullptr)
        throw std::logic_error("a print must run a recorded G-code; " + hash + " is not one");
    const json& destinations = (*entry)["destinations"];
    if (std::none_of(destinations.begin(), destinations.end(), [&print](const json& destination) {
            return destination.value("id", "") == print.destination_id && destination.value("kind", "") == "send";
        }))
        throw std::logic_error("a print must follow a send of its G-code; " + print.destination_id + " is not one");
    const std::uint64_t number = m_doc["counters"].value("nextPrint", std::uint64_t(1));
    m_doc["counters"]["nextPrint"] = number + 1;
    print.id = "p-" + std::to_string(number);
    if (print.started_at.empty())
        print.started_at = timestamp;
    (*entry)["prints"].push_back(print_json(print));
    touch();
    return print.id;
}

void ProjectStateDocument::finish_gcode_print(const std::string& hash, const std::string& print_id,
                                              const std::string& timestamp)
{
    json* entry = gcode_json(hash);
    if (entry == nullptr)
        throw std::logic_error("finished print of unknown G-code " + hash);
    for (json& print : (*entry)["prints"]) {
        if (print.value("id", "") != print_id)
            continue;
        // A printer repeats its state; the first report is when it finished.
        if (!print.value("finishedAt", "").empty())
            return;
        print["finishedAt"] = timestamp;
        touch();
        return;
    }
    throw std::logic_error("finished print " + print_id + " is not a print of " + hash);
}

std::vector<GcodeRecord> ProjectStateDocument::gcode_records() const
{
    std::vector<GcodeRecord> result;
    if (const auto entries = m_doc.find("gcodes"); entries != m_doc.end())
        for (const json& entry : *entries)
            result.push_back(read_gcode(entry));
    return result;
}

std::optional<GcodeRecord> ProjectStateDocument::gcode_record(const std::string& hash) const
{
    if (const auto entries = m_doc.find("gcodes"); entries != m_doc.end())
        for (const json& entry : *entries)
            if (entry.value("hash", "") == hash)
                return read_gcode(entry);
    return std::nullopt;
}

std::size_t ProjectStateDocument::gcode_record_count() const
{
    const auto entries = m_doc.find("gcodes");
    return entries == m_doc.end() ? 0 : entries->size();
}

json ProjectStateDocument::purpose_setup() const
{
    const json* chat = conversation_json(active_conversation_id());
    const json setup = chat ? chat->value("purposeSetup", json::object()) : json::object();
    return setup.is_object() ? setup : json::object();
}

void ProjectStateDocument::set_purpose_setup(json setup)
{
    json& chat = active_chat();
    if (setup.is_object() && !setup.empty())
        chat["purposeSetup"] = std::move(setup);
    else
        chat.erase("purposeSetup");
    touch();
}

bool ProjectStateDocument::restored_since_reply(const std::string& except_message_id) const
{
    std::uint64_t restored = 0;
    for (const json& change : m_doc["changes"])
        if (change.value("kind", "") == "restore")
            restored = std::max(restored, change.value("seq", std::uint64_t(0)));
    if (restored == 0)
        return false;
    for (const json& conversation : m_doc["conversations"])
        for (const json& message : conversation.value("messages", json::array()))
            if (message.value("role", "") == "assistant" && message.value("id", "") != except_message_id &&
                message.value("seq", std::uint64_t(0)) > restored)
                return false;
    return true;
}

PlanRecord ProjectStateDocument::plan() const { return plan(active_conversation_id()); }

PlanRecord ProjectStateDocument::plan(const std::string& conversation_id) const
{
    const json* chat = conversation_json(conversation_id);
    return chat && chat->contains("plan") ? read_plan((*chat)["plan"]) : PlanRecord{};
}

PlanRecord ProjectStateDocument::set_plan(PlanRecord record, const std::string& timestamp)
{
    json& stored      = active_chat()["plan"];
    record.seq        = next_seq();
    record.updated_at = timestamp;
    if (!stored.is_object())
        stored = json::object();
    stored["seq"]         = record.seq;
    stored["updatedAt"]   = record.updated_at;
    stored["headline"]    = record.headline;
    stored["validity"]    = "current";
    stored["invalidatedBy"] = "";
    stored.erase("invalidatedWeight");
    stored["decisions"]   = json::array();
    for (const PlanDecision& decision : record.decisions)
        stored["decisions"].push_back({{"topic", decision.topic},
                                       {"statement", decision.statement},
                                       {"confidence", decision.confidence},
                                       {"alternative", decision.alternative}});
    stored["assumptions"] = record.assumptions;
    stored["risks"]       = record.risks;
    touch();
    return record;
}

bool ProjectStateDocument::mark_plan_needs_reassessment(const std::string& reason, int weight)
{
    // Only the active chat's plan can go stale: nothing edits the project for
    // an inactive chat.
    json* chat = conversation_json(active_conversation_id());
    if (chat == nullptr || !chat->contains("plan"))
        return false;
    json& stored = (*chat)["plan"];
    if (!stored.is_object() || (stored.value("headline", "").empty() &&
                                (!stored.contains("decisions") || stored["decisions"].empty())))
        return false;
    const bool in_doubt = stored.value("validity", "current") == "needs_reassessment";
    if (in_doubt && (stored.value("invalidatedBy", "") == reason || stored.value("invalidatedWeight", 0) > weight))
        return false;
    stored["validity"] = "needs_reassessment";
    stored["invalidatedBy"] = reason;
    stored["invalidatedWeight"] = weight;
    touch();
    return true;
}

std::vector<Workspace::RegionRecord> ProjectStateDocument::regions() const
{
    std::vector<Workspace::RegionRecord> records;
    const json& stored = m_doc["regions"];
    if (stored.is_array())
        for (const json& entry : stored)
            records.push_back(Workspace::region_record_from(entry));
    return records;
}

std::vector<Workspace::RegionRecord> ProjectStateDocument::set_regions(std::vector<Workspace::RegionRecord> records,
                                                                       const std::string& timestamp)
{
    json stored = json::array();
    for (Workspace::RegionRecord& record : records) {
        if (record.seq == 0) {
            record.seq        = next_seq();
            record.updated_at = timestamp;
        }
        stored.push_back(Workspace::region_record_json(record));
    }
    m_doc["regions"] = std::move(stored);
    touch();
    return records;
}

void ProjectStateDocument::restore_regions(const json& regions)
{
    if (!regions.is_array())
        throw std::invalid_argument("a model version's regions are not a list");
    m_doc["regions"] = regions;
    touch();
}

ChangeEntry ProjectStateDocument::add_change(ChangeEntry entry, const std::string& timestamp)
{
    entry.conversation_id = active_conversation_id();
    entry.after_id        = m_change_anchor.empty() ? last_item_id(entry.conversation_id) : m_change_anchor;
    entry.seq             = next_seq();
    entry.created_at      = timestamp;
    json record{{"seq", entry.seq},
                {"createdAt", entry.created_at},
                {"kind", entry.kind},
                {"actor", entry.actor},
                {"label", entry.label},
                {"location", entry.location},
                {"conversationId", entry.conversation_id},
                {"afterId", entry.after_id}};
    if (entry.external) record["external"] = true;
    if (!entry.client.empty()) record["client"] = entry.client;
    if (entry.kind == "setting") {
        record["from"]   = entry.from;
        record["to"]     = entry.to;
        record["preset"] = entry.preset;
        if (!entry.key.empty()) record["key"] = entry.key;
    } else if (entry.kind == "restore") {
        // Keep the exact source and target versions in the audit entry.
        record["from"] = entry.from;
        record["to"]   = entry.to;
    }
    m_doc["changes"].push_back(std::move(record));
    touch();
    return entry;
}

WorkEntry ProjectStateDocument::begin_work(WorkEntry entry, const std::string& timestamp)
{
    if (entry.conversation_id.empty()) {
        entry.conversation_id = active_conversation_id();
        entry.after_id        = last_item_id(entry.conversation_id);
    }
    entry.seq             = next_seq();
    entry.id              = "work-" + std::to_string(entry.seq);
    entry.started_at      = timestamp;
    entry.ended_at.clear();
    entry.state           = "running";
    m_doc["work"].push_back(json{{"seq", entry.seq},
                                 {"id", entry.id},
                                 {"startedAt", entry.started_at},
                                 {"endedAt", ""},
                                 {"kind", entry.kind},
                                 {"title", entry.title},
                                 {"state", entry.state},
                                 {"detail", entry.detail},
                                 {"conversationId", entry.conversation_id},
                                 {"afterId", entry.after_id}});
    touch();
    return entry;
}

std::optional<WorkEntry> ProjectStateDocument::finish_work(const std::string& id, const std::string& state, const std::string& detail,
                                                           const std::string& timestamp)
{
    for (json& entry : m_doc["work"]) {
        if (entry.value("id", "") != id || entry.value("state", "") != "running")
            continue;
        entry["state"]   = state;
        entry["detail"]  = detail;
        entry["endedAt"] = timestamp;
        touch();
        return read_work(entry);
    }
    return std::nullopt;
}

std::vector<WorkEntry> ProjectStateDocument::work() const
{
    std::vector<WorkEntry> result;
    for (const json& entry : m_doc["work"])
        result.push_back(read_work(entry));
    return result;
}

std::vector<ChangeEntry> ProjectStateDocument::changes() const
{
    std::vector<ChangeEntry> result;
    for (const json& entry : m_doc["changes"])
        result.push_back(read_change(entry));
    return result;
}

std::string ProjectStateDocument::last_item_id(const std::string& conversation_id) const
{
    const json* conversation = conversation_json(conversation_id);
    if (conversation == nullptr)
        return {};
    std::string   last_id;
    std::uint64_t last_seq = 0;
    std::set<std::string> message_ids;
    for (const json& message : (*conversation)["messages"]) {
        message_ids.insert(message.value("id", ""));
        if (message.value("seq", std::uint64_t(0)) >= last_seq) {
            last_seq = message.value("seq", std::uint64_t(0));
            last_id  = message.value("id", "");
        }
    }
    // A tool activity belongs to the conversation of the message that
    // started it; one from an external MCP client belongs to none.
    for (const json& activity : m_doc["toolActivities"])
        if (message_ids.count(activity.value("correlationId", "")) != 0 &&
            activity.value("seq", std::uint64_t(0)) >= last_seq) {
            last_seq = activity.value("seq", std::uint64_t(0));
            last_id  = activity.value("actionId", "");
        }
    return last_id;
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
        const ToolState state = tool_state_from(entry.value("state", "running"));
        if (!tool_state_terminal(state)) {
            entry["state"] = "cancelled";
            changed        = true;
        }
    }
    if (changed)
        touch();
    return changed;
}

} // namespace Slic3r::GUI::JusPrin::Agent
