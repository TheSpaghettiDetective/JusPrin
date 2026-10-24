#include "ProviderAgent.hpp"
#include "slic3r/GUI/JusPrin/Support/Base64.hpp"
#include "ToolRegistry.hpp"
#include "ToolResults.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

// What the intent and plan leave out rather than send empty: an unset field,
// an empty list, a false truncated flag.
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
    // The model's own notes, in the shapes workspace_inspect's intent and plan
    // sections return, less their timestamps: on the next turn it reads what
    // it recorded without spending a call on it.
    json intent = intent_section_result(request.intent);
    intent["purpose"] = request.setup_title;
    for (json& field : intent["fields"])
        field.erase("updatedAt");
    drop_empty(intent);
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
    return result;
}

bool available_in_app(const ToolDefinition& definition, bool allow_import, bool skills_listed,
                      const std::vector<std::string>& session_tools)
{
    // A session with a subject of its own offers exactly the tools it names,
    // and those may be its own rather than the app's.
    if (!session_tools.empty())
        return std::find(session_tools.begin(), session_tools.end(), definition.name) != session_tools.end() &&
               (has_exposure(definition.exposure, ToolExposure::InApp) || has_exposure(definition.exposure, ToolExposure::Printer));
    return has_exposure(definition.exposure, ToolExposure::InApp) &&
           (definition.availability == ToolAvailability::Always ||
            (definition.availability == ToolAvailability::ImportableAttachment && allow_import) ||
            (definition.availability == ToolAvailability::SkillIndex && skills_listed));
}

// The tools this turn offers, in the registry's order.
std::vector<OfferedTool> offered_tools(const AgentRequest& request)
{
    const bool allow_import = std::any_of(request.attachments.begin(), request.attachments.end(),
                                          [](const AgentAttachmentContext& attachment) { return attachment.importable; });
    std::vector<OfferedTool> tools;
    for (const ToolDefinition& definition : ToolRegistry::instance().definitions()) {
        if (!available_in_app(definition, allow_import, !request.skill_index.empty(), request.session.tool_names))
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

AgentHttpHeader auth_header(const ProviderProfile& profile, const std::string& api_key)
{
    switch (profile.auth) {
    case AuthStyle::Bearer: return {"Authorization", "Bearer " + api_key};
    case AuthStyle::ApiKeyHeader: return {"x-api-key", api_key};
    }
    return {"Authorization", "Bearer " + api_key};
}

} // namespace

ProviderAgent::ProviderAgent(ProviderAgentConfig config, std::unique_ptr<IProviderCodec> codec,
                             std::unique_ptr<IAgentHttpTransport> transport)
    : m_config(std::move(config)), m_codec(std::move(codec)), m_transport(std::move(transport))
{}

ProviderAgent::~ProviderAgent() { cancel(); }

bool ProviderAgent::ready() const
{
    return m_transport != nullptr && m_codec != nullptr && !m_config.api_key.empty() && !m_config.profile.model.empty() &&
           !m_config.profile.endpoint.empty();
}

bool ProviderAgent::busy() const { return m_busy; }

void ProviderAgent::begin_transcript(const AgentRequest& request)
{
    m_transcript = TurnTranscript{};
    m_transcript.request_id   = request.request_id;
    m_transcript.instructions = request.session.instructions;
    m_transcript.opening      = request.conversation;
    if (request.purpose == AgentRequest::Purpose::ConversationTitle)
        return;
    m_transcript.tools = offered_tools(request);
    m_transcript.opening_once = skills_to_send(request.skills);
    // A turn the app started, after one of its own notes: nothing was said,
    // and an empty user message would read as the person saying nothing.
    if (request.user_text.empty() && request.issue_context.empty() && request.attachments.empty() &&
        !request.session.include_workspace)
        return;

    // One part per kind of content: the person's words exactly as typed, then
    // the app's data, each in its own tag, then the attachments.
    std::vector<TurnContentPart>& content = m_transcript.user_content;
    const auto text = [&content](std::string value) {
        TurnContentPart part;
        part.text = std::move(value);
        content.push_back(std::move(part));
    };
    if (!request.user_text.empty())
        text(request.user_text);
    if (!request.issue_context.empty())
        text(tagged_block("print_issue", request.issue_context));
    if (request.session.include_workspace)
        text(tagged_block("workspace", workspace_json(request).dump()));
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
        text(attachments.str());
    for (const auto& attachment : request.attachments) {
        if (attachment.bytes.empty())
            continue;
        TurnContentPart part;
        part.mime   = attachment.mime.empty() ? "application/octet-stream" : attachment.mime;
        part.base64 = base64_encode(attachment.bytes);
        if (attachment.kind == "image") {
            part.kind = TurnContentPart::Kind::Image;
        } else if (attachment.kind == "pdf") {
            part.kind     = TurnContentPart::Kind::Pdf;
            part.filename = attachment.name;
        } else {
            continue;
        }
        content.push_back(std::move(part));
    }
}

bool ProviderAgent::start(const AgentRequest& request)
{
    if (!ready() || m_busy)
        return false;
    m_pending_call_id.clear();
    m_pending_tool.clear();
    m_queued_calls.clear();
    m_step_skills.clear();
    m_failed_call.clear();
    m_waiting_for_tool = false;
    m_rejected_calls   = 0;
    m_paused_turns     = 0;
    m_kind = request.purpose == AgentRequest::Purpose::ConversationTitle ? RequestKind::ConversationTitle : RequestKind::Reply;
    begin_transcript(request);
    return post();
}

bool ProviderAgent::continue_after_tool(const AgentToolResult& result)
{
    if (!m_busy || !m_waiting_for_tool || result.call_id != m_pending_call_id)
        return false;
    m_transcript.steps.back().results.push_back(
        TurnStepResult{result.call_id, result.output_json, result.image, result.state != "succeeded"});
    for (const AgentSkillUse& skill : result.skills) {
        const auto same = std::find_if(m_step_skills.begin(), m_step_skills.end(),
                                       [&skill](const AgentSkillUse& pending) { return pending.name == skill.name; });
        if (same != m_step_skills.end())
            *same = skill;
        else
            m_step_skills.push_back(skill);
    }
    if (result.state != "succeeded" && m_failed_call.empty())
        m_failed_call = m_pending_tool + " (" + m_pending_call_id + ")";
    m_waiting_for_tool = false;
    m_pending_call_id.clear();
    m_pending_tool.clear();
    return next_call();
}

void ProviderAgent::answer_call(const std::string& call_id, const json& output)
{
    m_transcript.steps.back().results.push_back(TurnStepResult{call_id, output.dump(), nullptr, true});
}

bool ProviderAgent::post()
{
    m_sse_buffer.clear();
    m_terminal_seen = false;
    m_busy = true;
    ++m_transcript.request_sequence;
    m_codec->reset();
    ProviderRequest built = m_codec->build(m_transcript, m_config.profile, m_kind);
    AgentHttpRequest request;
    request.url = m_config.profile.endpoint;
    request.headers.push_back(auth_header(m_config.profile, m_config.api_key));
    request.headers.insert(request.headers.end(), m_config.profile.headers.begin(), m_config.profile.headers.end());
    request.headers.insert(request.headers.end(), built.headers.begin(), built.headers.end());
    request.body = built.body.dump();
    const std::uint64_t generation = ++m_http_generation;
    if (!m_transport->post(std::move(request), [this, generation](AgentHttpEvent event) {
            accept_http(generation, std::move(event));
        })) {
        m_busy = false;
        return false;
    }
    return true;
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
    switch (event.kind) {
    case StreamEvent::Kind::TextDelta: m_events.push_back(AgentEvent::delta(std::move(event.text))); break;
    case StreamEvent::Kind::TurnComplete: finish_response(std::move(event)); break;
    case StreamEvent::Kind::Incomplete:
        fail(AgentError{"incomplete_response", "The Agent service stopped before finishing its response.", true});
        break;
    case StreamEvent::Kind::Failed: fail(std::move(event.error)); break;
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
    m_failed_call.clear();
    m_transcript.steps.push_back(TurnStep{std::move(event.assistant_turn), {}, {}});
    if (event.resend) {
        // The provider paused its own turn and resumes it from this one.
        constexpr unsigned kPausedTurnLimit = 4;
        if (++m_paused_turns > kPausedTurnLimit) {
            fail(AgentError{"agent_paused_too_often", "The Agent service kept pausing its response.", true});
            return;
        }
        if (!post())
            fail(AgentError{"agent_continuation_failed", "The Agent could not be asked to continue its response.", true});
        return;
    }
    for (ProviderToolCall& call : event.calls)
        m_queued_calls.push_back(std::move(call));
    if (!next_call())
        fail(AgentError{"agent_continuation_failed", "The Agent could not be told the results of its tool calls.", true});
}

// Hands out the next call of the response, or, once every call has its
// result, sends them all back. Whatever the model sent, the next request
// never carries a call without its result. False only when that request
// could not be sent.
bool ProviderAgent::next_call()
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
        if (call_id.empty() || !available || !validation.valid()) {
            fail(AgentError{"malformed_tool_call", "The Agent returned an invalid tool call.", false});
            return true;
        }
        request.arguments_json = std::move(validation.arguments_json);
        m_pending_call_id = call_id;
        m_pending_tool    = request.tool;
        m_waiting_for_tool = true;
        AgentToolCall call{call_id, std::move(request), true};
        m_events.push_back(AgentEvent::tool_call(std::move(call)));
        return true;
    }
    if (m_transcript.steps.back().results.empty()) {
        m_busy = false;
        m_events.push_back(AgentEvent::completed());
        return true;
    }
    m_transcript.steps.back().once = skills_to_send(m_step_skills);
    m_step_skills.clear();
    return post();
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
            fail(http_error(event.status, event.error));
        } else if (!m_terminal_seen) {
            // A valid SSE stream may close immediately after its final data
            // line without a trailing blank-line delimiter. Consume that
            // complete residual frame before classifying the response.
            if (!m_sse_buffer.empty()) {
                parse_sse_frame(m_sse_buffer);
                m_sse_buffer.clear();
            }
            if (!m_terminal_seen)
                fail(AgentError{"malformed_response", "The Agent service ended before a complete response arrived.", true});
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

AgentError ProviderAgent::http_error(unsigned status, const std::string& detail) const
{
    const std::string& name = m_config.profile.display_name;
    if (status == 401 || status == 403)
        return {"invalid_credentials", "The " + name + " API key was rejected. Update the Agent configuration and try again.", false};
    if (status == 413)
        return {"request_too_large", "The request was too large for the " + name + " service.", false};
    if (status == 429)
        return {"rate_limited", "The " + name + " service is rate limited. Try again shortly.", true};
    if (status == 529)
        return {"service_overloaded", "The " + name + " service is overloaded. Try again shortly.", true};
    if (status >= 500)
        return {"service_unavailable", "The " + name + " service is temporarily unavailable.", true};
    if (status == 408 || detail.find("timed out") != std::string::npos || detail.find("Timeout") != std::string::npos)
        return {"timeout", "The Agent request timed out.", true};
    return {"network_error", "The Agent request could not reach the " + name + " service.", true};
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

} // namespace Slic3r::GUI::JusPrin::Agent
