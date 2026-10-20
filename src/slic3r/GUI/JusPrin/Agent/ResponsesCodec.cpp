#include "ResponsesCodec.hpp"

#include <algorithm>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

json text_part(std::string text) { return json{{"type", "input_text"}, {"text", std::move(text)}}; }

std::string data_url(const std::string& mime, const std::string& base64) { return "data:" + mime + ";base64," + base64; }

bool has_closed_required_objects(const json& schema)
{
    if (schema.value("type", json()) == "object") {
        if (schema.value("additionalProperties", json()) != false)
            return false;
        const auto properties = schema.value("properties", json::object());
        const auto required = schema.value("required", json::array());
        for (const auto& property : properties.items())
            if (std::find(required.begin(), required.end(), property.key()) == required.end() ||
                !has_closed_required_objects(property.value()))
                return false;
    }
    return !schema.contains("items") || has_closed_required_objects(schema["items"]);
}

json tools_json(const std::vector<OfferedTool>& tools)
{
    json items = json::array();
    for (const OfferedTool& tool : tools)
        items.push_back(json{{"type", "function"},
                             {"name", tool.definition->name},
                             {"description", tool.description},
                             // OpenAI strict mode cannot express optional
                             // fields or dynamic maps. Preserve the registry
                             // schema; native validation still owns acceptance.
                             {"strict", has_closed_required_objects(tool.definition->input_schema)},
                             {"parameters", tool.definition->input_schema}});
    return items;
}

json user_message(const std::vector<TurnContentPart>& parts)
{
    json content = json::array();
    for (const TurnContentPart& part : parts) {
        switch (part.kind) {
        case TurnContentPart::Kind::Text: content.push_back(text_part(part.text)); break;
        case TurnContentPart::Kind::Image:
            content.push_back(json{{"type", "input_image"}, {"image_url", data_url(part.mime, part.base64)}});
            break;
        case TurnContentPart::Kind::Pdf:
            content.push_back(json{{"type", "input_file"}, {"filename", part.filename}, {"file_data", data_url(part.mime, part.base64)}});
            break;
        }
    }
    return json{{"role", "user"}, {"content", std::move(content)}};
}

json input_items(const TurnTranscript& transcript, RequestKind kind)
{
    json input = json::array();
    for (const auto& message : transcript.opening) {
        if (message.call_id.empty()) {
            if (message.issue_context.empty())
                input.push_back(json{{"role", message.role}, {"content", message.text}});
            else
                input.push_back(json{{"role", message.role},
                                     {"content", json::array({text_part(message.text),
                                                              text_part(tagged_block("print_issue", message.issue_context))})}});
            continue;
        }
        // A call from an earlier turn, replayed for this stateless request.
        // Its call_id pairs it with its output. The item id the service gave
        // the call is not sent: with store:false it names nothing kept.
        input.push_back(json{{"type", "function_call"}, {"call_id", message.call_id}, {"name", message.tool},
                             {"arguments", message.arguments_json}});
        input.push_back(json{{"type", "function_call_output"}, {"call_id", message.call_id}, {"output", message.output_json}});
    }
    if (kind == RequestKind::Reply && !transcript.user_content.empty())
        input.push_back(user_message(transcript.user_content));
    for (const TurnStep& step : transcript.steps) {
        // The response's own items, reasoning included, exactly as sent.
        input.insert(input.end(), step.assistant_turn.begin(), step.assistant_turn.end());
        for (const TurnStepResult& result : step.results)
            input.push_back(json{{"type", "function_call_output"}, {"call_id", result.call_id}, {"output", result.output_json}});
        // A function result is text only; the pictures follow the results as
        // image inputs.
        for (const TurnStepResult& result : step.results)
            if (result.image)
                input.push_back(json{{"role", "user"},
                                     {"content", json::array({json{{"type", "input_text"}, {"text", "The image returned by call " + result.call_id + "."}},
                                                              json{{"type", "input_image"},
                                                                   {"image_url", data_url(result.image->mime_type, result.image->base64)}}})}});
    }
    // A fired skill as the request's last items: its picture, as an image
    // input of its own, then its text in the app's voice.
    for (const AgentSkillUse& skill : newest_once(transcript)) {
        if (skill.picture)
            input.push_back(json{{"role", "user"},
                                 {"content", json::array({json{{"type", "input_image"},
                                                               {"image_url", data_url(skill.picture->mime_type, skill.picture->base64)}}})}});
        input.push_back(json{{"role", "developer"}, {"content", skill.text}});
    }
    return input;
}

} // namespace

ProviderRequest ResponsesCodec::build(const TurnTranscript& transcript, const ProviderProfile& profile, RequestKind kind) const
{
    ProviderRequest request;
    request.headers.push_back({"Idempotency-Key", transcript.request_id + "-request-" + std::to_string(transcript.request_sequence)});
    json input = input_items(transcript, kind);
    if (kind == RequestKind::ConversationTitle) {
        request.body = json{{"model", profile.model}, {"store", false}, {"stream", true},
                            {"instructions", kConversationTitleInstructions}, {"input", std::move(input)}};
        return request;
    }
    // Every session's instructions are written by its page and arrive in the
    // session profile, the project assistant's included
    // (AgentUI/src/projectInstructions.ts); this codec adds no words of its
    // own. The text is the same on every request of a conversation, which the
    // provider's prompt cache depends on. A response may hold several calls:
    // a read and the change it does not depend on belong in one step, and
    // some models send several whatever this says (ProviderAgent runs them
    // all, in order).
    request.body = json{{"model", profile.model}, {"store", false}, {"stream", true}, {"parallel_tool_calls", true},
                        {"instructions", transcript.instructions}, {"tools", tools_json(transcript.tools)}, {"input", std::move(input)}};
    return request;
}

std::vector<StreamEvent> ResponsesCodec::parse(const std::string& payload)
{
    std::vector<StreamEvent> events;
    const auto               push = [&events](StreamEvent::Kind kind) -> StreamEvent& {
        events.push_back(StreamEvent{});
        events.back().kind = kind;
        return events.back();
    };
    const json event = json::parse(payload, nullptr, false);
    if (!event.is_object()) {
        push(StreamEvent::Kind::Failed).error = {"malformed_response", "The Agent service returned an unreadable streaming response.", false};
        return events;
    }
    const std::string type = event.value("type", "");
    if (type == "response.output_text.delta" && event.contains("delta") && event["delta"].is_string()) {
        push(StreamEvent::Kind::TextDelta).text = event["delta"].get<std::string>();
    } else if (type == "response.completed" && event.contains("response")) {
        if (m_completed)
            return events;
        m_completed = true;
        const json& response = event["response"];
        if (!response.is_object() || !response.contains("output") || !response["output"].is_array()) {
            push(StreamEvent::Kind::Failed).error = {"malformed_response", "The Agent service response did not contain valid output.", false};
            return events;
        }
        StreamEvent& complete = push(StreamEvent::Kind::TurnComplete);
        complete.assistant_turn = response["output"];
        for (const json& item : complete.assistant_turn)
            if (item.is_object() && item.value("type", "") == "function_call")
                complete.calls.push_back({item.value("call_id", ""), item.value("name", ""), item.value("arguments", "{}")});
        if (response.contains("usage") && response["usage"].is_object()) {
            const json& usage = response["usage"];
            AgentUsage   reported;
            reported.input  = usage.value("input_tokens", std::uint64_t{0});
            reported.output = usage.value("output_tokens", std::uint64_t{0});
            reported.total  = usage.value("total_tokens", std::uint64_t{0});
            if (usage.contains("input_tokens_details") && usage["input_tokens_details"].is_object())
                reported.cached_input = usage["input_tokens_details"].value("cached_tokens", std::uint64_t{0});
            complete.usage = reported;
        }
    } else if (type == "response.incomplete") {
        push(StreamEvent::Kind::Incomplete);
    } else if (type == "response.failed" || type == "error") {
        push(StreamEvent::Kind::Failed).error = {"agent_service_error", "The Agent service could not complete the response.", true};
    }
    return events;
}

} // namespace Slic3r::GUI::JusPrin::Agent
