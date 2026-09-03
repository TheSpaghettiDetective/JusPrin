#include "OpenAIResponsesAgent.hpp"
#include "ToolRegistry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

std::string base64_encode(const std::string& input)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = -6;
    for (unsigned char byte : input) {
        value = (value << 8) + byte;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(value >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6)
        out.push_back(table[((value << 8) >> (bits + 8)) & 0x3f]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

json workspace_json(const Workspace::WorkspaceSnapshot& workspace)
{
    json plates = json::array();
    for (const auto& plate : workspace.plates) {
        json objects = json::array();
        for (const auto& object : plate.objects)
            objects.push_back(json{{"id", std::to_string(object.id.value())}, {"name", object.name},
                                   {"instances", object.instances.size()}});
        plates.push_back(json{{"id", std::to_string(plate.id.value())}, {"name", plate.name},
                              {"active", plate.active}, {"sliced", plate.sliced}, {"objects", std::move(objects)}});
    }
    json selected = json::array();
    for (const auto id : workspace.selected_objects)
        selected.push_back(std::to_string(id.value()));
    return json{{"sessionId", std::to_string(workspace.session.value())},
                {"revision", workspace.revision},
                {"projectName", workspace.setup.project_name},
                {"printerPreset", workspace.setup.printer_preset},
                {"filamentPreset", workspace.setup.filament_preset},
                {"selectedObjectIds", std::move(selected)},
                {"plates", std::move(plates)}};
}

bool available_in_app(const ToolDefinition& definition, bool allow_import)
{
    return has_exposure(definition.exposure, ToolExposure::InApp) &&
           (definition.availability == ToolAvailability::Always || allow_import);
}

json tools_for(bool allow_import)
{
    json tools = json::array();
    for (const ToolDefinition& definition : ToolRegistry::instance().definitions()) {
        if (!available_in_app(definition, allow_import))
            continue;
        tools.push_back(json{{"type", "function"},
                             {"name", definition.name},
                             {"description", definition.description},
                             {"strict", true},
                             {"parameters", definition.input_schema}});
    }
    return tools;
}

} // namespace

OpenAIResponsesAgent::OpenAIResponsesAgent(OpenAIResponsesConfig config,
                                           std::unique_ptr<IAgentHttpTransport> transport)
    : m_config(std::move(config)), m_transport(std::move(transport))
{}

OpenAIResponsesAgent::~OpenAIResponsesAgent() { cancel(); }

bool OpenAIResponsesAgent::ready() const
{
    return m_transport != nullptr && !m_config.api_key.empty() && !m_config.model.empty() && !m_config.endpoint.empty();
}

bool OpenAIResponsesAgent::busy() const { return m_busy; }

json OpenAIResponsesAgent::initial_input(const AgentRequest& request) const
{
    json input = json::array();
    for (const auto& message : request.conversation)
        input.push_back(json{{"role", message.role}, {"content", message.text}});

    json content = json::array();
    std::ostringstream context;
    context << request.user_text << "\n\nAuthoritative JusPrin workspace context:\n" << workspace_json(request.workspace).dump();
    for (const auto& attachment : request.attachments) {
        context << "\nAttachment " << attachment.id << ": " << attachment.name << " [" << attachment.kind << "]";
        if (!attachment.summary.empty())
            context << " — " << attachment.summary;
        if (!attachment.text.empty())
            context << "\n<attachment id=\"" << attachment.id << "\">\n" << attachment.text << "\n</attachment>";
    }
    content.push_back(json{{"type", "input_text"}, {"text", context.str()}});
    for (const auto& attachment : request.attachments) {
        if (attachment.bytes.empty())
            continue;
        const std::string mime = attachment.mime.empty() ? "application/octet-stream" : attachment.mime;
        const std::string data_url = "data:" + mime + ";base64," + base64_encode(attachment.bytes);
        if (attachment.kind == "image")
            content.push_back(json{{"type", "input_image"}, {"image_url", data_url}});
        else if (attachment.kind == "pdf")
            content.push_back(json{{"type", "input_file"}, {"filename", attachment.name}, {"file_data", data_url}});
    }
    input.push_back(json{{"role", "user"}, {"content", std::move(content)}});
    return input;
}

json OpenAIResponsesAgent::request_body(json input) const
{
    return json{{"model", m_config.model}, {"store", false}, {"stream", true}, {"parallel_tool_calls", false},
                {"instructions",
                 "You are the JusPrin assistant inside OrcaSlicer. Use only IDs from the authoritative workspace context. "
                 "Native tools are proposals: never claim a change succeeded until a function_call_output says it did. "
                 "When asked to make a supported change, call the matching tool. After its result, briefly explain the actual result."},
                {"tools", tools_for(m_allow_import)}, {"input", std::move(input)}};
}

bool OpenAIResponsesAgent::start(const AgentRequest& request)
{
    if (!ready() || m_busy)
        return false;
    m_prior_output = json::array();
    m_pending_call_id.clear();
    m_waiting_for_tool = false;
    m_request_id = request.request_id;
    m_request_sequence = 0;
    m_allow_import = std::any_of(request.attachments.begin(), request.attachments.end(),
                                 [](const AgentAttachmentContext& attachment) { return attachment.importable; });
    return post(initial_input(request));
}

bool OpenAIResponsesAgent::continue_after_tool(const AgentToolResult& result)
{
    if (!m_busy || !m_waiting_for_tool || result.call_id != m_pending_call_id)
        return false;
    json input = m_prior_output;
    input.push_back(json{{"type", "function_call_output"}, {"call_id", result.call_id}, {"output", result.output_json}});
    m_waiting_for_tool = false;
    m_pending_call_id.clear();
    return post(std::move(input));
}

bool OpenAIResponsesAgent::post(json input)
{
    m_sse_buffer.clear();
    m_terminal_seen = false;
    m_busy = true;
    AgentHttpRequest request;
    request.url = m_config.endpoint;
    request.authorization = "Bearer " + m_config.api_key;
    request.idempotency_key = m_request_id + "-request-" + std::to_string(++m_request_sequence);
    request.body = request_body(std::move(input)).dump();
    const std::uint64_t generation = ++m_http_generation;
    if (!m_transport->post(std::move(request), [this, generation](AgentHttpEvent event) {
            accept_http(generation, std::move(event));
        })) {
        m_busy = false;
        return false;
    }
    return true;
}

void OpenAIResponsesAgent::accept_http(std::uint64_t generation, AgentHttpEvent event)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_http_events.push_back(QueuedHttpEvent{generation, std::move(event)});
}

void OpenAIResponsesAgent::parse_sse_frame(const std::string& frame)
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
    const json event = json::parse(payload, nullptr, false);
    if (!event.is_object()) {
        fail(AgentError{"malformed_response", "The Agent service returned an unreadable streaming response.", false});
        return;
    }
    const std::string type = event.value("type", "");
    if (std::getenv("JUSPRIN_AGENT_RECORD_USAGE") != nullptr &&
        (type == "response.completed" || type == "response.failed" || type == "response.incomplete" || type == "error"))
        std::cerr << "JUSPRIN LIVE EVENT type=" << (type.empty() ? "missing" : type) << '\n';
    if (type == "response.output_text.delta" && event.contains("delta") && event["delta"].is_string())
        m_events.push_back(AgentEvent::delta(event["delta"].get<std::string>()));
    else if (type == "response.completed" && event.contains("response"))
        finish_response(event["response"]);
    else if (type == "response.incomplete")
        fail(AgentError{"incomplete_response", "The Agent service stopped before finishing its response.", true});
    else if (type == "response.failed" || type == "error")
        fail(AgentError{"agent_service_error", "The Agent service could not complete the response.", true});
}

void OpenAIResponsesAgent::finish_response(const json& response)
{
    if (m_terminal_seen)
        return;
    m_terminal_seen = true;
    if (!response.is_object() || !response.contains("output") || !response["output"].is_array()) {
        fail(AgentError{"malformed_response", "The Agent service response did not contain valid output.", false});
        return;
    }
    m_prior_output = response["output"];
    if (m_config.usage_listener && response.contains("usage") && response["usage"].is_object()) {
        const json& usage = response["usage"];
        m_config.usage_listener(usage.value("input_tokens", std::uint64_t{0}),
                                usage.value("output_tokens", std::uint64_t{0}),
                                usage.value("total_tokens", std::uint64_t{0}));
    }
    for (const json& item : m_prior_output) {
        if (!item.is_object() || item.value("type", "") != "function_call")
            continue;
        const std::string call_id = item.value("call_id", "");
        ToolRequest request{item.value("name", ""), item.value("arguments", "{}")};
        const ToolDefinition* definition = ToolRegistry::instance().find(request.tool);
        const bool available = definition != nullptr && available_in_app(*definition, m_allow_import);
        ToolValidationResult validation;
        if (available)
            validation = ToolRegistry::instance().validate_call(*definition, request.arguments_json);
        if (call_id.empty() || !available || !validation.valid()) {
            fail(AgentError{"malformed_tool_call", "The Agent returned an invalid tool proposal.", false});
            return;
        }
        request.arguments_json = std::move(validation.arguments_json);
        m_pending_call_id = call_id;
        m_waiting_for_tool = true;
        AgentToolCall call{call_id, std::move(request), true};
        m_events.push_back(AgentEvent::tool_call(std::move(call)));
        return;
    }
    m_busy = false;
    m_events.push_back(AgentEvent::completed());
}

void OpenAIResponsesAgent::parse_available()
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

std::optional<AgentEvent> OpenAIResponsesAgent::poll()
{
    parse_available();
    if (m_events.empty())
        return std::nullopt;
    AgentEvent event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}

AgentError OpenAIResponsesAgent::http_error(unsigned status, const std::string& detail) const
{
    if (status == 401 || status == 403)
        return {"invalid_credentials", "The OpenAI API key was rejected. Update the Agent configuration and try again.", false};
    if (status == 429)
        return {"rate_limited", "The OpenAI service is rate limited. Try again shortly.", true};
    if (status >= 500)
        return {"service_unavailable", "The OpenAI service is temporarily unavailable.", true};
    if (status == 408 || detail.find("timed out") != std::string::npos || detail.find("Timeout") != std::string::npos)
        return {"timeout", "The Agent request timed out.", true};
    return {"network_error", "The Agent request could not reach the OpenAI service.", true};
}

void OpenAIResponsesAgent::fail(AgentError error)
{
    if (!m_busy && m_terminal_seen)
        return;
    m_busy = false;
    m_waiting_for_tool = false;
    m_terminal_seen = true;
    m_events.push_back(AgentEvent::failed(std::move(error)));
}

void OpenAIResponsesAgent::cancel()
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
    m_busy = false;
    m_waiting_for_tool = false;
    m_terminal_seen = true;
}

} // namespace Slic3r::GUI::JusPrin::Agent
