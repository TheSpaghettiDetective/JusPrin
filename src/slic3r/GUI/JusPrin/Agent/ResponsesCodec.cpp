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

json input_part(const RecordPart& part)
{
    if (part.kind == RecordPart::Kind::Image)
        return json{{"type", "input_image"}, {"image_url", data_url(part.mime, part.base64)}};
    if (part.kind == RecordPart::Kind::Pdf)
        return json{{"type", "input_file"}, {"filename", part.filename}, {"file_data", data_url(part.mime, part.base64)}};
    return text_part(part.text);
}

json input_items(const TurnTranscript& transcript)
{
    json input = json::array();
    for (const RecordItem& item : transcript.items) {
        switch (item.kind) {
        case RecordItem::Kind::User: {
            json content = json::array();
            for (const RecordPart& part : item.parts)
                content.push_back(input_part(part));
            input.push_back(json{{"role", "user"}, {"content", std::move(content)}});
            break;
        }
        // An app note or a fired skill, in the app's voice.
        case RecordItem::Kind::Operator: input.push_back(json{{"role", "developer"}, {"content", item.text}}); break;
        case RecordItem::Kind::Assistant:
            // The response's own items, reasoning included, exactly as sent.
            input.insert(input.end(), item.output.begin(), item.output.end());
            break;
        case RecordItem::Kind::Results:
            for (const RecordResult& result : item.results)
                input.push_back(json{{"type", "function_call_output"}, {"call_id", result.call_id}, {"output", result.output_json}});
            // A function result is text only; the pictures follow the results
            // as image inputs.
            for (const RecordResult& result : item.results)
                if (result.image)
                    input.push_back(json{{"role", "user"},
                                         {"content", json::array({text_part("The image returned by call " + result.call_id + "."),
                                                                  input_part(*result.image)})}});
            break;
        }
    }
    return input;
}

} // namespace

ProviderRequest ResponsesCodec::build(const TurnTranscript& transcript, const ProviderProfile& profile, RequestKind kind) const
{
    ProviderRequest request;
    request.headers.push_back({"Idempotency-Key", transcript.request_id + "-request-" + std::to_string(transcript.request_sequence)});
    json input = input_items(transcript);
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
    // OpenAI and OpenRouter read the same field.
    if (!profile.effort.empty())
        request.body["reasoning"] = json{{"effort", profile.effort}};
    // Every chat opens with the same instructions and tools, which is what
    // the provider picks a cache machine by; the key keeps each chat on its
    // own. JusPrin's gateway refuses a field it does not list, and does not
    // list this one yet.
    if (!transcript.chat_key.empty() && !profile.account_sign_in)
        request.body["prompt_cache_key"] = transcript.chat_key;
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
