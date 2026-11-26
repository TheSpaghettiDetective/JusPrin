#include "ProviderAgent.hpp"
#include "CloudAccount.hpp"
#include "ManufacturingHistory.hpp"
#include "ProviderProfile.hpp"
#include "RecordConversion.hpp"
#include "slic3r/GUI/JusPrin/Support/Base64.hpp"
#include "ToolRegistry.hpp"
#include "ToolResults.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "libslic3r/format.hpp"
#include "slic3r/GUI/JusPrin/Support/SaidBy.hpp"
// Last: it defines the L marker.
#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

// What the plan leaves out rather than send empty: an unset field, an empty
// list, a false truncated flag.
void drop_empty(json& value)
{
    if (!value.is_object() && !value.is_array())
        return;
    for (json& child : value)
        drop_empty(child);
    if (!value.is_object())
        return;
    for (auto it = value.begin(); it != value.end();) {
        const bool empty = it->is_null() || (it->is_string() && it->get_ref<const std::string&>().empty()) ||
                           ((it->is_array() || it->is_object()) && it->empty()) ||
                           (it.key() == "truncated" && *it == false);
        it = empty ? value.erase(it) : std::next(it);
    }
}

json workspace_json(const AgentRequest& request)
{
    const Workspace::WorkspaceSnapshot& workspace = request.workspace;
    json plates = json::array();
    for (const auto& plate : workspace.plates) {
        json objects = json::array();
        for (const auto& object : plate.objects)
            objects.push_back(json{{"id", std::to_string(object.id.value())}, {"name", object.name},
                                   {"instances", object.instances.size()}});
        json entry{{"id", std::to_string(plate.id.value())}, {"name", plate.name},
                   {"active", plate.active}, {"sliced", plate.sliced}, {"objects", std::move(objects)}};
        // In printer_setup's name and plate_layout's values, so a filament
        // the plate cannot take is visible before anything is sliced.
        if (!plate.bed_type.empty())
            entry["plateType"] = plate.bed_type;
        // Why its last slice did not succeed, whoever asked for that slice.
        if (plate.slice_failure)
            entry["sliceFailure"] = slice_failure_result(*plate.slice_failure);
        // What its current slice warned about, whoever sliced it.
        if (plate.slice_warnings)
            entry["sliceWarnings"] = slice_warnings_result(*plate.slice_warnings);
        // A calibration test's steps, while the plate's print holds them.
        if (plate.calibration)
            entry["calibration"] = calibration_steps_result(*plate.calibration);
        plates.push_back(std::move(entry));
    }
    json selected = json::array();
    for (const auto id : workspace.selected_objects)
        selected.push_back(std::to_string(id.value()));
    json result{{"sessionId", std::to_string(workspace.session.value())},
                {"revision", workspace.revision},
                {"projectName", workspace.setup.project_name},
                {"printerPreset", workspace.setup.printer_preset},
                {"filamentPreset", workspace.setup.filament_preset},
                {"selectedObjectIds", std::move(selected)},
                {"plates", std::move(plates)}};
    if (!workspace.setup.process_preset.empty())
        result["processPreset"] = workspace.setup.process_preset;
    // The model's own notes: the purpose it recorded, and the plan in the
    // shape workspace_inspect's plan section returns, less its timestamp. On
    // the next turn it reads what it recorded without spending a call on it.
    json intent = json::object();
    if (!request.setup_title.empty())
        intent["purpose"] = request.setup_title;
    if (request.setup_unchecked_change)
        intent["setupUncheckedChange"] = true;
    if (!intent.empty())
        result["intent"] = std::move(intent);
    const json full_plan = plan_section_result(request.plan);
    json plan{{"headline", full_plan["headline"]}, {"decisions", full_plan["decisions"]},
              {"unverified", full_plan["unverified"]}, {"truncated", full_plan["truncated"]}};
    drop_empty(plan);
    // Whether the plan still holds, and the edit that put it in doubt.
    if (!plan.empty()) {
        plan["validity"] = full_plan["validity"];
        if (!request.plan.invalidated_by.empty())
            plan["invalidatedBy"] = full_plan["invalidatedBy"];
        result["plan"] = std::move(plan);
    }
    // What left the project and what the printers reported, newest first,
    // for when the person says how a print went. A print with no finish may
    // still be running or may have stopped: nothing records which.
    json history = json::array();
    for (const AgentRequest::GcodeHistoryEntry& entry : request.gcode_history) {
        const GcodeRecord& record = entry.record;
        json exports = json::array(), sends = json::array(), prints = json::array();
        for (const GcodeDestination& destination : record.destinations) {
            if (destination.kind == GcodeDestination::Kind::Export)
                exports.push_back(json{{"at", destination.at},
                                       {"file", std::filesystem::u8path(destination.path).filename().u8string()}});
            else
                sends.push_back(json{{"at", destination.at}, {"printer", destination.printer_name},
                                     {"startRequested", destination.start_requested}});
        }
        for (const GcodePrint& print : record.prints) {
            json item{{"printer", print.printer_name}, {"startedAt", print.started_at}};
            if (!print.finished_at.empty())
                item["finishedAt"] = print.finished_at;
            prints.push_back(std::move(item));
        }
        json item{{"gcode", record.hash.substr(0, 8)},
                  {"firstLeftAt", record.created_at},
                  {"plate", record.plate_name},
                  {"printerPreset", record.printer},
                  {"filamentPreset", record.material},
                  {"processPreset", record.process},
                  {"sameInputsAsNow", entry.same_inputs_as_now},
                  {"exports", std::move(exports)},
                  {"sends", std::move(sends)},
                  {"prints", std::move(prints)}};
        if (record.statistics.print_time_seconds > 0.0)
            item["estimatedSeconds"] = record.statistics.print_time_seconds;
        if (record.statistics.material_grams > 0.0)
            item["estimatedGrams"] = record.statistics.material_grams;
        drop_empty(item);
        history.push_back(std::move(item));
    }
    if (!history.empty())
        result["printHistory"] = std::move(history);
    return result;
}

bool available_in_app(const ToolDefinition& definition, bool skills_listed,
                      const std::vector<std::string>& session_tools)
{
    // A session with a subject of its own offers exactly the tools it names,
    // and those may be its own rather than the app's.
    if (!session_tools.empty())
        return std::find(session_tools.begin(), session_tools.end(), definition.name) != session_tools.end() &&
               (has_exposure(definition.exposure, ToolExposure::InApp) || has_exposure(definition.exposure, ToolExposure::Printer));
    return has_exposure(definition.exposure, ToolExposure::InApp) &&
           (definition.availability == ToolAvailability::Always ||
            (definition.availability == ToolAvailability::SkillIndex && skills_listed));
}

// The tools this turn offers, in the registry's order.
std::vector<OfferedTool> offered_tools(const AgentRequest& request)
{
    std::vector<OfferedTool> tools;
    for (const ToolDefinition& definition : ToolRegistry::instance().definitions()) {
        if (!available_in_app(definition, !request.skill_index.empty(), request.session.tool_names))
            continue;
        if (!request.model_reads_pictures && (definition.name == "view_render" || definition.name == "project_attachment_read"))
            continue;
        // skill_read carries the list it reads from, as one line per skill.
        std::string description = definition.description;
        if (definition.handler == ToolHandler::SkillRead) {
            description += "\n\nSkills:";
            for (const AgentSkill& skill : request.skill_index)
                description += "\n- " + skill.name + ": " + skill.description;
        }
        tools.push_back(OfferedTool{&definition, std::move(description)});
    }
    return tools;
}

// Adds the tools that come into force with `skills` to those offered, in the
// registry's order, so the list a chat sends stays the same from then on.
void bring_tools(std::vector<OfferedTool>& tools, const std::vector<AgentSkillUse>& skills)
{
    bool added = false;
    for (const AgentSkillUse& skill : skills)
        for (const std::string& name : skill.tools) {
            if (std::any_of(tools.begin(), tools.end(), [&name](const OfferedTool& tool) { return tool.definition->name == name; }))
                continue;
            const ToolDefinition* definition = ToolRegistry::instance().find(name);
            if (definition == nullptr)
                throw std::logic_error("The skill " + skill.name + " brings a tool this build does not have: " + name);
            tools.push_back(OfferedTool{definition, definition->description});
            added = true;
        }
    if (added)
        std::sort(tools.begin(), tools.end(),
                  [](const OfferedTool& lhs, const OfferedTool& rhs) { return lhs.definition->name < rhs.definition->name; });
}

// The skills to send with one request. A skill that yields to another fired
// for the same request is left out.
std::vector<AgentSkillUse> skills_to_send(const std::vector<AgentSkillUse>& skills)
{
    std::vector<AgentSkillUse> sent;
    for (const AgentSkillUse& skill : skills) {
        const bool yields = std::any_of(skills.begin(), skills.end(), [&skill](const AgentSkillUse& other) {
            return std::find(skill.yields_to.begin(), skill.yields_to.end(), other.trigger) != skill.yields_to.end();
        });
        if (!yields)
            sent.push_back(skill);
    }
    return sent;
}

RecordPart image_part(const ToolImage& image)
{
    RecordPart part;
    part.kind   = RecordPart::Kind::Image;
    part.mime   = image.mime_type;
    part.base64 = image.base64;
    return part;
}

// In place of a picture a model cannot read.
constexpr const char* kNoPictureNote = "No picture of the plate is attached: the model in this chat cannot read pictures.";

RecordPart text_part(std::string value)
{
    RecordPart part;
    part.text = std::move(value);
    return part;
}

AgentHttpHeader auth_header(const ProviderProfile& profile, const std::string& api_key)
{
    switch (profile.auth) {
    case AuthStyle::Bearer: return {"Authorization", "Bearer " + api_key};
    case AuthStyle::ApiKeyHeader: return {"x-api-key", api_key};
    case AuthStyle::None: break;
    }
    throw std::logic_error("auth_header for a provider that sends no credential: " + profile.id);
}

} // namespace

ProviderAgent::ProviderAgent(ProviderAgentConfig config, std::unique_ptr<IProviderCodec> codec,
                             std::unique_ptr<IAgentHttpTransport> transport)
    : m_config(std::move(config)), m_codec(std::move(codec)), m_transport(std::move(transport))
{}

ProviderAgent::~ProviderAgent() { cancel(); }

bool ProviderAgent::ready() const
{
    return m_transport != nullptr && m_codec != nullptr &&
           (m_config.profile.auth == AuthStyle::None || !m_config.api_key.empty()) && !m_config.profile.model.empty() &&
           !m_config.profile.endpoint.empty();
}

bool ProviderAgent::busy() const { return m_busy; }

void ProviderAgent::begin_transcript(const AgentRequest& request)
{
    m_transcript = TurnTranscript{};
    m_transcript.request_id   = request.request_id;
    m_transcript.chat_key     = request.chat_key;
    m_transcript.chat_title   = request.chat_title;
    // A provider with no web search of its own is not told it has one.
    m_transcript.instructions = m_config.profile.web_search_type.empty() && !request.session.instructions_without_web_search.empty()
                                    ? request.session.instructions_without_web_search
                                    : request.session.instructions;
    m_app_started             = request.app_started;
    m_reads_pictures          = request.model_reads_pictures;
    m_on_recorded             = request.on_recorded;
    m_keep_blob               = request.keep_blob;
    std::vector<RecordItem>& items = m_transcript.items;
    const auto text = [](std::string value) {
        RecordPart part;
        part.text = std::move(value);
        return part;
    };
    if (request.purpose == AgentRequest::Purpose::ConversationTitle) {
        items.push_back(RecordItem{RecordItem::Kind::User, {}, {text(request.user_text)}});
        m_recorded = m_settled = items.size();
        return;
    }
    if (!m_on_recorded || !m_keep_blob)
        throw std::logic_error("a reply request with nowhere to keep what it sends");
    items      = with_ended_turns_closed(request.record.items, request.record.format);
    m_recorded = items.size();
    // Each turn says what it is in, for a model served in another format.
    for (RecordItem& item : items)
        if (item.kind == RecordItem::Kind::Assistant && item.output_format.empty())
            item.output_format = request.record.format;
    m_transcript.tools = offered_tools(request);
    bring_tools(m_transcript.tools, request.skills);

    // What the model has not been sent yet, the person's side first: a
    // message that never reached it, then this turn's own.
    for (const RecordItem& item : request.unsent)
        if (item.kind == RecordItem::Kind::User)
            items.push_back(item);
    // Kept from here once the provider accepts the request: the person's side.
    m_settled = items.size();
    // One part per kind of content: the person's words exactly as typed, then
    // the app's data, each in its own tag, then the attachments. A turn the
    // app started on its note has only the data, kept with the note once the
    // model has answered both; a retry whose turn the record already holds
    // adds nothing.
    const bool app_turn = !request.note.empty();
    RecordItem turn{RecordItem::Kind::User, app_turn ? std::string() : request.source, {}};
    if (!request.user_text.empty())
        turn.parts.push_back(text(request.user_text));
    if (!request.issue_context.empty())
        turn.parts.push_back(text(tagged_block("print_issue", request.issue_context)));
    if (!request.turn_recorded && request.session.include_workspace)
        turn.parts.push_back(text(tagged_block("workspace", workspace_json(request).dump())));
    std::ostringstream attachments;
    for (const auto& attachment : request.attachments) {
        if (attachments.tellp() > 0)
            attachments << "\n";
        attachments << "Attachment " << attachment.id << ": " << attachment.name << " [" << attachment.kind << "]";
        if (!attachment.summary.empty())
            attachments << " — " << attachment.summary;
        if (!attachment.text.empty())
            attachments << "\n<attachment id=\"" << attachment.id << "\">\n" << attachment.text << "\n</attachment>";
    }
    if (attachments.tellp() > 0)
        turn.parts.push_back(text(attachments.str()));
    for (const auto& attachment : request.attachments) {
        if (attachment.bytes.empty() || (attachment.kind != "image" && attachment.kind != "pdf"))
            continue;
        // A model that cannot read pictures is refused any request that
        // carries one; the composer marked the picture as not sent.
        if (attachment.kind == "image" && !request.model_reads_pictures)
            continue;
        RecordPart part;
        part.kind   = attachment.kind == "image" ? RecordPart::Kind::Image : RecordPart::Kind::Pdf;
        part.mime   = attachment.mime.empty() ? "application/octet-stream" : attachment.mime;
        part.base64 = base64_encode(attachment.bytes);
        if (part.kind == RecordPart::Kind::Pdf)
            part.filename = attachment.name;
        turn.parts.push_back(kept(std::move(part)));
    }
    if (!turn.parts.empty())
        items.push_back(std::move(turn));
    // What the app adds from here is kept once the model has answered it:
    // its notes, the turn's own last, in the operator's voice.
    if (!app_turn)
        m_settled = items.size();
    std::vector<RecordItem> notes;
    for (const RecordItem& item : request.unsent)
        if (item.kind == RecordItem::Kind::Operator)
            notes.push_back(item);
    if (app_turn)
        notes.push_back(RecordItem{RecordItem::Kind::Operator, request.source, {}, request.note});
    push_skills(skills_to_send(request.skills), std::move(notes));
}

void ProviderAgent::push_skills(const std::vector<AgentSkillUse>& skills, std::vector<RecordItem> notes)
{
    // Each fired skill's picture on the person's side, then the app's voice:
    // its notes, then every skill's text, for this request only where the
    // provider can clear it.
    for (const AgentSkillUse& skill : skills)
        if (skill.picture && m_reads_pictures)
            m_transcript.items.push_back(RecordItem{RecordItem::Kind::User, {}, {kept(image_part(*skill.picture))}});
        else if (skill.picture)
            // What the skill's text calls "the picture" is not there; the
            // model is told, so it does not describe a plate it never saw.
            m_transcript.items.push_back(RecordItem{RecordItem::Kind::User, {}, {text_part(kNoPictureNote)}});
    for (RecordItem& note : notes)
        m_transcript.items.push_back(std::move(note));
    for (const AgentSkillUse& skill : skills) {
        // One that stays is offered on every request and sent once: the chat's
        // own record says whether it has been, a turn that failed included.
        if (skill.stays && std::any_of(m_transcript.items.begin(), m_transcript.items.end(), [&skill](const RecordItem& item) {
                return item.kind == RecordItem::Kind::Operator && item.text == skill.text;
            }))
            continue;
        m_transcript.items.push_back(RecordItem{RecordItem::Kind::Operator, {}, {}, skill.text, {}, {}, !skill.stays});
    }
}

RecordPart ProviderAgent::kept(RecordPart part)
{
    part.blob = m_keep_blob(part.base64);
    return part;
}

void ProviderAgent::record()
{
    // A title request belongs to no chat: nothing it sends or gets is kept.
    if (m_kind == RequestKind::ConversationTitle || m_settled <= m_recorded)
        return;
    m_on_recorded(wire_format(m_config.profile.family),
                  std::vector<RecordItem>(m_transcript.items.begin() + m_recorded, m_transcript.items.begin() + m_settled));
    m_recorded = m_settled;
}

bool ProviderAgent::start(const AgentRequest& request)
{
    if (!ready() || m_busy)
        return false;
    m_pending_call_id.clear();
    m_pending_tool.clear();
    m_queued_calls.clear();
    m_step_skills.clear();
    m_results = RecordItem{RecordItem::Kind::Results};
    m_failed_call.clear();
    m_waiting_for_tool = false;
    m_rejected_calls   = 0;
    m_paused_turns     = 0;
    m_kind = request.purpose == AgentRequest::Purpose::ConversationTitle ? RequestKind::ConversationTitle : RequestKind::Reply;
    begin_transcript(request);
    post();
    return true;
}

bool ProviderAgent::continue_after_tool(const AgentToolResult& result)
{
    if (!m_busy || !m_waiting_for_tool || result.call_id != m_pending_call_id)
        return false;
    m_results.results.push_back(RecordResult{result.call_id, result.output_json, result.state != "succeeded",
                                             result.image && m_reads_pictures ? std::optional<RecordPart>(kept(image_part(*result.image))) : std::nullopt});
    for (const AgentSkillUse& skill : result.skills) {
        const auto same = std::find_if(m_step_skills.begin(), m_step_skills.end(),
                                       [&skill](const AgentSkillUse& pending) { return pending.name == skill.name; });
        if (same != m_step_skills.end())
            *same = skill;
        else
            m_step_skills.push_back(skill);
    }
    // The request after this result is where those skills are first read, so
    // their tools are offered from it on.
    bring_tools(m_transcript.tools, result.skills);
    if (result.state != "succeeded" && m_failed_call.empty())
        m_failed_call = m_pending_tool + " (" + m_pending_call_id + ")";
    m_waiting_for_tool = false;
    m_pending_call_id.clear();
    m_pending_tool.clear();
    next_call();
    return true;
}

void ProviderAgent::answer_call(const std::string& call_id, const json& output)
{
    m_results.results.push_back(RecordResult{call_id, output.dump(), true, std::nullopt});
}

void ProviderAgent::post()
{
    m_sse_buffer.clear();
    m_service_request_id.clear();
    m_terminal_seen = false;
    m_busy = true;
    ++m_transcript.request_sequence;
    m_codec->reset();
    ProviderRequest built = m_codec->build(m_transcript, m_config.profile, m_kind);
    AgentHttpRequest request;
    request.url = m_config.profile.endpoint;
    request.request_timeout_seconds = m_config.profile.request_timeout_seconds;
    if (m_config.profile.auth != AuthStyle::None)
        request.headers.push_back(auth_header(m_config.profile, m_config.api_key));
    request.headers.insert(request.headers.end(), m_config.profile.headers.begin(), m_config.profile.headers.end());
    request.headers.insert(request.headers.end(), built.headers.begin(), built.headers.end());
    if (m_config.profile.account_sign_in) {
        request.headers.push_back({"X-JusPrin-Turn", m_transcript.request_id});
        request.headers.push_back({"X-JusPrin-Request", m_kind == RequestKind::ConversationTitle ? "title" : m_app_started ? "note" : "reply"});
        // The gateway keeps each chat whole, named by this. A copied project
        // gets a new project id, so its chats get new names too.
        if (!m_transcript.chat_key.empty())
            request.headers.push_back({"X-JusPrin-Chat", gateway_chat_id(m_transcript.chat_key)});
        // So Billing and usage, on the server, can list the chat by name.
        if (!m_transcript.chat_key.empty() && !m_transcript.chat_title.empty())
            request.headers.push_back({"X-JusPrin-Chat-Title", percent_encoded(m_transcript.chat_title)});
    }
    request.body = built.body.dump();
    if (m_config.request_listener)
        m_config.request_listener(SentRequest{m_config.profile.id, m_transcript.chat_key, m_transcript.request_id,
                                              m_transcript.request_sequence, built.body});
    const std::uint64_t generation = ++m_http_generation;
    if (!m_transport->post(std::move(request), [this, generation](AgentHttpEvent event) {
            accept_http(generation, std::move(event));
        })) {
        m_busy = false;
        throw std::runtime_error("the HTTP transport did not start the request");
    }
}

void ProviderAgent::accept_http(std::uint64_t generation, AgentHttpEvent event)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_http_events.push_back(QueuedHttpEvent{generation, std::move(event)});
}

void ProviderAgent::parse_sse_frame(const std::string& frame)
{
    std::string payload;
    std::istringstream lines(frame);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("data:", 0) == 0) {
            std::string part = line.substr(5);
            if (!part.empty() && part.front() == ' ')
                part.erase(part.begin());
            if (!payload.empty())
                payload += '\n';
            payload += part;
        }
    }
    if (payload.empty() || payload == "[DONE]")
        return;
    for (StreamEvent& event : m_codec->parse(payload))
        handle(std::move(event));
}

void ProviderAgent::handle(StreamEvent event)
{
    // The provider has accepted the request: what it carried is now part of
    // the chat. A request it refused leaves no trace.
    record();
    switch (event.kind) {
    case StreamEvent::Kind::TextDelta: m_events.push_back(AgentEvent::delta(std::move(event.text))); break;
    case StreamEvent::Kind::TurnComplete: finish_response(std::move(event)); break;
    case StreamEvent::Kind::Failed: fail(expected(std::move(event.error), "failed its reply", {})); break;
    case StreamEvent::Kind::ServiceError: {
        // An error inside a reply can name a cause a person can act on; a
        // reply cut short or unreadable names none.
        std::optional<AgentError> expected = in_reply_answer(event.answer);
        if (!expected)
            unexpected(event.text, {});
        AgentError error = this->expected(std::move(*expected), event.text, {});
        // Nothing more of this reply is read.
        cancel();
        m_events.push_back(AgentEvent::failed(std::move(error)));
    }
    }
}

void ProviderAgent::finish_response(StreamEvent event)
{
    if (m_terminal_seen)
        return;
    m_terminal_seen = true;
    if (m_config.usage_listener && event.usage)
        m_config.usage_listener(*event.usage);
    m_queued_calls.clear();
    m_step_skills.clear();
    m_results = RecordItem{RecordItem::Kind::Results};
    m_failed_call.clear();
    m_transcript.items.push_back(RecordItem{RecordItem::Kind::Assistant, {}, {}, {}, std::move(event.assistant_turn)});
    // Kept as it arrives, before its calls are answered: a turn can end
    // first, and the reply may hold what an earlier one asked for (the
    // result of a web search). The next turn answers what it left unanswered
    // (with_ended_turns_closed).
    m_settled = m_transcript.items.size();
    record();
    if (event.resend) {
        // The provider paused its own turn and resumes it from this one.
        constexpr unsigned kPausedTurnLimit = 4;
        if (++m_paused_turns > kPausedTurnLimit) {
            unexpected("paused its response more than " + std::to_string(kPausedTurnLimit) + " times", {});
        }
        post();
        return;
    }
    for (ProviderToolCall& call : event.calls)
        m_queued_calls.push_back(std::move(call));
    next_call();
}

// Hands out the next call of the response, or, once every call has its
// result, sends them all back. Whatever the model sent, the next request
// never carries a call without its result. False only when that request
// could not be sent.
void ProviderAgent::next_call()
{
    while (!m_queued_calls.empty()) {
        const ProviderToolCall item = std::move(m_queued_calls.front());
        m_queued_calls.pop_front();
        const std::string& call_id = item.call_id;
        ToolRequest request{item.name, item.arguments_json};
        // The model chose this call before it saw the earlier call fail, so
        // it is answered unrun and the model decides again with both results.
        if (!call_id.empty() && !m_failed_call.empty()) {
            answer_call(call_id, json{{"state", "failed"},
                                      {"error", json{{"code", "not_run"},
                                                     {"message", "Not run: an earlier call in the same response, " + m_failed_call +
                                                                     ", did not succeed. Nothing was queued. Call it again if it is "
                                                                     "still needed."}}}});
            continue;
        }
        const auto offered = std::find_if(m_transcript.tools.begin(), m_transcript.tools.end(),
                                          [&request](const OfferedTool& tool) { return tool.definition->name == request.tool; });
        const bool available = m_kind == RequestKind::Reply && offered != m_transcript.tools.end();
        ToolValidationResult validation;
        if (available)
            validation = ToolRegistry::instance().validate_call(*offered->definition, request.arguments_json);
        // A call to no tool of this turn, or with arguments that miss the
        // tool's contract, never reaches a card. The model gets the refusal as
        // that call's result and may correct it, a bounded number of times
        // per turn.
        constexpr unsigned kRejectedCallLimit = 2;
        if ((!available || !validation.valid()) && m_config.refusal_listener)
            m_config.refusal_listener(request.tool, request.arguments_json, available ? validation.error->code : "unknown_tool");
        if (!call_id.empty() && m_kind == RequestKind::Reply && (!available || !validation.valid()) &&
            m_rejected_calls < kRejectedCallLimit) {
            ++m_rejected_calls;
            json error = available ?
                json{{"code", validation.error->code},
                     {"message", validation.error->message +
                                     " Nothing was queued. Check the arguments against this tool's parameters and call it again."}} :
                json{{"code", "unknown_tool"},
                     {"message", "There is no tool named \"" + request.tool + "\" here. Nothing was queued. Use one of the listed tools."}};
            answer_call(call_id, json{{"state", "failed"}, {"error", std::move(error)}});
            m_failed_call = request.tool + " (" + call_id + ")";
            continue;
        }
        if (call_id.empty() || !available || !validation.valid())
            unexpected("sent a call to " + request.tool + " that cannot run (" +
                           (call_id.empty() ? "no call id" : available ? validation.error->code : "unknown_tool") + ")",
                       request.arguments_json);
        request.arguments_json = std::move(validation.arguments_json);
        m_pending_call_id = call_id;
        m_pending_tool    = request.tool;
        m_waiting_for_tool = true;
        AgentToolCall call{call_id, std::move(request), true};
        m_events.push_back(AgentEvent::tool_call(std::move(call)));
        return;
    }
    if (m_results.results.empty()) {
        m_settled = m_transcript.items.size();
        record();
        m_busy = false;
        m_events.push_back(AgentEvent::completed());
        return;
    }
    // The results go back together, and with them the skills they fired.
    m_transcript.items.push_back(std::exchange(m_results, RecordItem{RecordItem::Kind::Results}));
    m_settled = m_transcript.items.size();
    push_skills(skills_to_send(m_step_skills));
    m_step_skills.clear();
    post();
}

void ProviderAgent::parse_available()
{
    std::deque<QueuedHttpEvent> incoming;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        incoming.swap(m_http_events);
    }
    for (QueuedHttpEvent& queued : incoming) {
        if (queued.generation != m_http_generation)
            continue;
        AgentHttpEvent& event = queued.event;
        if (!event.request_id.empty())
            m_service_request_id = event.request_id;
        if (event.kind == AgentHttpEvent::Kind::Data) {
            m_sse_buffer += event.data;
            for (;;) {
                const std::size_t lf_pos = m_sse_buffer.find("\n\n");
                const std::size_t crlf_pos = m_sse_buffer.find("\r\n\r\n");
                const bool crlf_first = crlf_pos != std::string::npos &&
                                        (lf_pos == std::string::npos || crlf_pos < lf_pos);
                const std::size_t pos = crlf_first ? crlf_pos : lf_pos;
                if (pos == std::string::npos)
                    break;
                const std::string frame = m_sse_buffer.substr(0, pos);
                m_sse_buffer.erase(0, pos + (crlf_first ? 4 : 2));
                parse_sse_frame(frame);
            }
        } else if (event.kind == AgentHttpEvent::Kind::Error) {
            fail(http_error(event));
        } else if (!m_terminal_seen) {
            // A valid SSE stream may close immediately after its final data
            // line without a trailing blank-line delimiter. Consume that
            // complete residual frame before classifying the response.
            if (!m_sse_buffer.empty()) {
                parse_sse_frame(m_sse_buffer);
                m_sse_buffer.clear();
            }
            if (!m_terminal_seen)
                unexpected("ended its stream before the response was complete", {});
        }
    }
}

std::optional<AgentEvent> ProviderAgent::poll()
{
    parse_available();
    if (m_events.empty())
        return std::nullopt;
    AgentEvent event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}

// What an AI request can end in. A person is told the cause only where they
// can do something about it; every other answer is unexpected: the request
// stops and the answer is thrown, to be reported with the service's words.
//
//   JusPrin account, by the gateway's code: update_required, unauthorized,
//   credits_exhausted, rate_limited; inside a reply, none.
//   An own key, by what the service names in its answer or in the event that
//   fails a reply: its account out of credits, the key refused, too many
//   requests, and a refusal of what the key, its account or its settings
//   allow, shown in the service's own words.
//   No answer at all: the network.
//   An own key's service failing on its side, by status or inside a reply:
//   that service, which nobody here can fix.
//   A model on this computer, by the words of the app that runs it, or, when
//   the answer is not that app talking at all, the address.
//   A chat more than the model takes is ours: the app sizes what it sends.
namespace {

// What a service's error says, wherever it says it: an HTTP body or an error
// event ({"error": {...}}, OpenAI's documented flat error event, Anthropic's
// {"type": "error", "error": {...}}) or a failed response
// ({"response": {"error": {...}}}). OpenRouter puts its kind of error in
// error_type, beside the error or in its metadata. Ollama's own API puts its
// words in the error itself ({"error": "model 'x' not found"}).
struct ServiceSays
{
    std::string type, code, message, error_type;
};

std::string string_at(const json& object, const char* key)
{
    if (!object.is_object() || !object.contains(key) || !object[key].is_string())
        return {};
    return object[key].get<std::string>();
}

ServiceSays service_says(const json& answer)
{
    if (!answer.is_object())
        return {};
    const json response = answer.value("response", json());
    const json error    = answer.value("error", json()).is_object() ? answer["error"] :
                          response.is_object() && response.value("error", json()).is_object() ? response["error"] :
                                                                                                 answer;
    ServiceSays says{string_at(error, "type"), string_at(error, "code"), string_at(error, "message"),
                     string_at(answer, "error_type")};
    if (says.message.empty())
        says.message = string_at(answer, "error");
    if (says.error_type.empty())
        says.error_type = string_at(response, "error_type");
    if (says.error_type.empty())
        says.error_type = string_at(error.value("metadata", json()), "error_type");
    return says;
}

bool starts_with(const std::string& text, const char* start) { return text.rfind(start, 0) == 0; }

// The person's own account at the service has no money left to pay for the
// request. OpenAI names it by type (its spend-limit codes share the type) and
// may send it inside the reply; Anthropic, by its words on a 400.
bool out_of_credits(unsigned status, const ServiceSays& says)
{
    return status == 402 || says.type == "insufficient_quota" || says.code == "credit_balance_exhausted" ||
           says.error_type == "payment_required" || starts_with(says.message, "Your credit balance is too low");
}

AgentError provider_out_of_credits(const std::string& name)
{
    return {"provider_credits_exhausted",
            // TRN %1% is an AI provider, such as OpenAI: the person's own account there has no credits left.
            Slic3r::format(translated(L("Your %1% account is out of credits. Add credits at %1%, then try again.")), name), false};
}

AgentError rate_limited(const std::string& name)
{
    // TRN %1% is an AI provider, such as OpenAI.
    return {"rate_limited", Slic3r::format(translated(L("The %1% service is rate limited. Try again shortly.")), name), true};
}

AgentError provider_unavailable(const std::string& name)
{
    return {"provider_unavailable",
            // TRN %1% is an AI provider, such as OpenAI. Its service is down or overloaded on its own side.
            Slic3r::format(translated(L("The %1% service is having trouble right now. Try again later.")), name), true};
}

// The code the gateway's error body names its cause by.
std::string error_code(const std::string& body)
{
    const json answer = json::parse(body, nullptr, false);
    if (!answer.is_object() || !answer.value("error", json()).is_object())
        return {};
    const json code = answer["error"].value("code", json());
    return code.is_string() ? code.get<std::string>() : std::string();
}

} // namespace

std::string service_message(const std::string& body) { return service_says(json::parse(body, nullptr, false)).message; }

AgentError local_refused(const std::string& name, const std::string& words)
{
    return {"local_refused", said_by(name, words), false};
}

AgentError local_unreachable(const std::string& address_or_endpoint)
{
    // TRN %1% is the address of the app that runs an AI model on this computer, such as 127.0.0.1:11434.
    // Ollama and LM Studio are the names of two such apps.
    return {"local_unreachable",
            Slic3r::format(translated(L("Nothing is answering at %1%. Start Ollama or LM Studio, then try again.")),
                           local_address_shown(address_or_endpoint)),
            true};
}

AgentError local_cant_run()
{
    return {"local_cant_run",
            translated(L("This model can't run the app's tools. Pick a model with tool support and a context of at least 32k.")),
            false};
}

AgentError ProviderAgent::http_error(const AgentHttpEvent& event)
{
    // No answer at all: the connection failed or stalled. On this computer
    // that is the app that runs the model not running, which the person can
    // start.
    if (event.status < 400 && m_config.profile.on_this_computer)
        return expected(local_unreachable(m_config.profile.endpoint), "could not be reached", event.error);
    if (event.status < 400)
        // TRN %1% is an AI provider, such as OpenAI.
        return expected({"network_error", Slic3r::format(translated(L("The AI request could not reach the %1% service.")), m_config.profile.display_name), true},
                        "could not be reached", event.error);
    std::optional<AgentError> expected;
    if (m_config.profile.account_sign_in) {
        expected = update_required(event.status, event.data);
        if (!expected)
            expected = gateway_answer(error_code(event.data), event.data);
    } else {
        expected = provider_answer(event.status, json::parse(event.data, nullptr, false));
    }
    const std::string answered = "answered HTTP " + std::to_string(event.status);
    const std::string& words   = event.data.empty() ? event.error : event.data;
    if (expected)
        return this->expected(std::move(*expected), answered, words);
    // The app that runs the model on this computer refusing in its own words,
    // such as a model deleted since setup or one too big for the memory. An
    // answer that is not that app talking at all, such as another program at
    // a wrong port or an Ollama too old for this endpoint, leaves the person
    // to start the app or fix the address.
    if (m_config.profile.on_this_computer) {
        const json answer = json::parse(event.data, nullptr, false);
        if (const std::string message = service_says(answer).message; !message.empty())
            return this->expected(local_refused(m_config.profile.display_name, message), answered, words);
        if (answer.is_discarded())
            return this->expected(local_unreachable(m_config.profile.endpoint), answered, words);
    }
    // The service of the person's own key failing on its side: whoever runs
    // it can fix it, and nobody here can.
    if (!m_config.profile.account_sign_in && !m_config.profile.on_this_computer && event.status >= 500)
        return this->expected(provider_unavailable(m_config.profile.display_name), answered, words);
    // The key check (setup_probe_request) sends the same request for every
    // person; only the key and its account differ. A refusal of it in the
    // service's words is about the key's account, such as an OpenRouter
    // workspace limited to the person's own provider keys.
    if (!m_config.profile.account_sign_in && m_transcript.request_id == "setup-key-check" && event.status < 500)
        if (const std::string message = service_says(json::parse(event.data, nullptr, false)).message; !message.empty())
            return this->expected({"provider_refused", said_by(m_config.profile.display_name, message), false}, answered, words);
    unexpected(answered, words);
}

std::optional<AgentError> ProviderAgent::gateway_answer(const std::string& code, const std::string& body) const
{
    const std::string& name = m_config.profile.display_name;
    if (code == "unauthorized")
        return AgentError{"invalid_credentials", sign_in_ended(), false};
    if (code == "credits_exhausted") {
        // What the message costs and what the person has, from the gateway's
        // numbers, in the app's language. The gateway always sends both.
        const json answer = json::parse(body, nullptr, false);
        if (!answer.is_object() || !answer.value("cost", json()).is_number_unsigned() || !answer.value("balance", json()).is_number_unsigned())
            return std::nullopt;
        const unsigned cost = answer["cost"].get<unsigned>();
        // TRN %1% is what the message costs in credits, %2% the credits the person has.
        return AgentError{"credits_exhausted",
                          Slic3r::format(translated_plural("This message needs %1% credit; you have %2%.",
                                                           "This message needs %1% credits; you have %2%.", cost),
                                         cost, answer["balance"].get<unsigned>()),
                          false};
    }
    // The person sent too many turns in a minute.
    if (code == "rate_limited")
        return rate_limited(name);
    return std::nullopt;
}

std::optional<AgentError> ProviderAgent::provider_answer(unsigned status, const json& answer) const
{
    const std::string& name = m_config.profile.display_name;
    const ServiceSays  says = service_says(answer);
    if (out_of_credits(status, says))
        return provider_out_of_credits(name);
    // Anthropic's 403 is a key without permission for what it asked; OpenAI's
    // and OpenRouter's 403s name other causes (a country, moderation, a key's
    // spending limit).
    if (status == 401 || (status == 403 && says.type == "permission_error"))
        // TRN %1% is an AI provider, such as OpenAI.
        return AgentError{"invalid_credentials", Slic3r::format(translated(L("The %1% API key was rejected. Update the AI configuration and try again.")), name), false};
    if (status == 429)
        return says.message.empty() ? rate_limited(name) : AgentError{"rate_limited", said_by(name, says.message), true};
    // The service refusing what the person's key, account or settings allow,
    // in words that say what to do: OpenAI's unsupported country,
    // OpenRouter's guardrails, spending limits and flagged input.
    if (status == 403 && !says.message.empty())
        return AgentError{"provider_refused", said_by(name, says.message), false};
    return std::nullopt;
}

std::optional<AgentError> ProviderAgent::in_reply_answer(const json& answer) const
{
    if (!m_config.profile.account_sign_in) {
        // The service of the person's own key overloaded or failing on its
        // side, as Anthropic, OpenAI and OpenRouter name it inside a reply.
        const ServiceSays says = service_says(answer);
        if (!m_config.profile.on_this_computer &&
            (says.type == "overloaded_error" || says.type == "api_error" || says.code == "server_error" ||
             says.error_type == "provider_overloaded" || says.error_type == "provider_unavailable" || says.error_type == "server"))
            return provider_unavailable(m_config.profile.display_name);
        if (std::optional<AgentError> expected = provider_answer(0, answer))
            return expected;
        // The app that runs the model on this computer stopping a reply in
        // its own words, such as its runner ending mid-reply.
        if (m_config.profile.on_this_computer && !says.message.empty())
            return local_refused(m_config.profile.display_name, says.message);
        return std::nullopt;
    }
    // Inside a reply the gateway names no cause a person can act on.
    return std::nullopt;
}

std::string ProviderAgent::answer_said(const std::string& answered, const std::string& words) const
{
    std::string what = m_config.profile.display_name + " " + answered + " (request " + m_transcript.request_id + " #" +
                       std::to_string(m_transcript.request_sequence);
    if (!m_service_request_id.empty())
        what += ", its request id " + m_service_request_id;
    what += ")";
    if (!words.empty())
        what += ": " + words;
    return what;
}

void ProviderAgent::unexpected(const std::string& answered, const std::string& words)
{
    std::string what = answer_said(answered, words);
    cancel();
    throw UnexpectedServiceAnswer(what);
}

AgentError ProviderAgent::expected(AgentError error, const std::string& answered, const std::string& words) const
{
    error.said = answer_said(answered, words) + " (model " + m_config.profile.model + ")";
    return error;
}

void ProviderAgent::fail(AgentError error)
{
    if (!m_busy && m_terminal_seen)
        return;
    m_busy = false;
    m_waiting_for_tool = false;
    m_queued_calls.clear();
    m_terminal_seen = true;
    m_events.push_back(AgentEvent::failed(std::move(error)));
}

void ProviderAgent::cancel()
{
    if (m_transport)
        m_transport->cancel();
    ++m_http_generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_http_events.clear();
    }
    m_events.clear();
    m_sse_buffer.clear();
    m_queued_calls.clear();
    m_busy = false;
    m_waiting_for_tool = false;
    m_terminal_seen = true;
}

std::string gateway_chat_id(const std::string& chat_key) { return sha256_hex(chat_key).substr(0, 32); }

} // namespace Slic3r::GUI::JusPrin::Agent
