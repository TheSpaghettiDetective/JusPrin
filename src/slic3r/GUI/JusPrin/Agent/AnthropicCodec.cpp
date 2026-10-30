#include "AnthropicCodec.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

json ephemeral() { return json{{"type", "ephemeral"}}; }

json text_block(std::string text) { return json{{"type", "text"}, {"text", std::move(text)}}; }

json image_block(const std::string& mime, const std::string& base64)
{
    return json{{"type", "image"}, {"source", json{{"type", "base64"}, {"media_type", mime}, {"data", base64}}}};
}

// The objects-closed rule strict mode needs: every object closes its
// properties and requires all of them. The same rule the Responses codec
// applies, read here on the projected schema.
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

void project(json& node)
{
    if (node.is_array()) {
        for (json& child : node)
            project(child);
        return;
    }
    if (!node.is_object())
        return;
    static const std::array<const char*, 9> kMoved{"minimum", "maximum",  "multipleOf",    "minLength",    "maxLength",
                                                   "minItems", "maxItems", "minProperties", "maxProperties"};
    std::string moved;
    for (const char* keyword : kMoved) {
        if (!node.contains(keyword))
            continue;
        // minItems of 0 or 1 is the one array constraint strict mode takes.
        if (std::string(keyword) == "minItems" && node[keyword].is_number_integer() && node[keyword].get<long long>() <= 1)
            continue;
        moved += (moved.empty() ? "" : ", ") + std::string(keyword) + " " + node[keyword].dump();
        node.erase(keyword);
    }
    if (!moved.empty()) {
        const std::string description = node.value("description", "");
        node["description"]           = description + (description.empty() ? "(" : " (") + moved + ")";
    }
    for (const char* child : {"properties", "$defs", "definitions"})
        if (node.contains(child) && node[child].is_object())
            for (auto& [key, value] : node[child].items())
                project(value);
    for (const char* child : {"items", "additionalProperties", "anyOf", "oneOf", "allOf", "not"})
        if (node.contains(child))
            project(node[child]);
}

json tools_json(const std::vector<OfferedTool>& tools)
{
    json items = json::array();
    for (const OfferedTool& tool : tools) {
        json schema = anthropic_strict_schema(tool.definition->input_schema);
        const bool strict = has_closed_required_objects(schema);
        items.push_back(json{{"name", tool.definition->name},
                             {"description", tool.description},
                             {"input_schema", std::move(schema)},
                             {"strict", strict}});
    }
    // Tools render first, so one breakpoint on the last caches them all.
    if (!items.empty())
        items.back()["cache_control"] = ephemeral();
    return items;
}

// One message of the request as it is built: a role and its blocks, or for
// role "system" its text and whether it clears at the next user message.
// Adjacent messages of one role are merged after.
struct Message
{
    std::string role;
    json        blocks{json::array()};
    std::string text; // system only
    bool        clears{false};
};

void push(std::vector<Message>& messages, std::string role, json blocks)
{
    if (blocks.empty())
        return;
    messages.push_back(Message{std::move(role), std::move(blocks), {}});
}

void push_system(std::vector<Message>& messages, std::string text, bool clears)
{
    if (!text.empty())
        messages.push_back(Message{"system", json::array(), std::move(text), clears});
}

// The API takes alternating roles, and mid-conversation system messages
// only right after a user message, as the last message or before an
// assistant one; a run of them is judged as one. Adjacent messages of one
// role become one, except system messages that clear and ones that stay;
// a run of system messages anywhere else becomes text blocks of the user
// message it then joins. What a message becomes depends only on its
// neighbours, which a later request keeps, so the history stays
// append-only.
std::vector<Message> normalized(std::vector<Message> messages)
{
    for (bool changed = true; changed;) {
        changed = false;
        std::vector<Message> merged;
        for (Message& message : messages) {
            if (!merged.empty() && merged.back().role == message.role && merged.back().clears == message.clears) {
                Message& last = merged.back();
                if (message.role == "system")
                    last.text += "\n\n" + message.text;
                else
                    last.blocks.insert(last.blocks.end(), message.blocks.begin(), message.blocks.end());
                changed = true;
                continue;
            }
            merged.push_back(std::move(message));
        }
        messages = std::move(merged);
        for (std::size_t start = 0; start < messages.size(); ++start) {
            if (messages[start].role != "system")
                continue;
            std::size_t end = start;
            while (end + 1 < messages.size() && messages[end + 1].role == "system")
                ++end;
            const bool after_user  = start > 0 && messages[start - 1].role == "user";
            const bool then_answer = end + 1 == messages.size() || messages[end + 1].role == "assistant";
            if (!after_user || !then_answer) {
                for (std::size_t i = start; i <= end; ++i) {
                    messages[i].role   = "user";
                    messages[i].blocks = json::array({text_block(messages[i].text)});
                    messages[i].text.clear();
                    messages[i].clears = false;
                }
                changed = true;
            }
            start = end;
        }
    }
    return messages;
}

json message_json(const Message& message)
{
    if (message.role == "system" && message.clears)
        return json{{"role", "system"}, {"content", message.text}, {"clear_at", "next_user_message"}};
    if (message.role == "system")
        return json{{"role", "system"}, {"content", message.text}};
    return json{{"role", message.role}, {"content", message.blocks}};
}

json part_block(const RecordPart& part)
{
    if (part.kind == RecordPart::Kind::Image)
        return image_block(part.mime, part.base64);
    if (part.kind == RecordPart::Kind::Pdf)
        return json{{"type", "document"}, {"source", json{{"type", "base64"}, {"media_type", "application/pdf"}, {"data", part.base64}}}};
    return text_block(part.text);
}

// A fired skill clears only where the provider can clear it; elsewhere it
// stays in place, as on Responses.
std::vector<Message> conversation(const TurnTranscript& transcript, bool clears)
{
    std::vector<Message> messages;
    for (const RecordItem& item : transcript.items) {
        switch (item.kind) {
        case RecordItem::Kind::User: {
            json blocks = json::array();
            for (const RecordPart& part : item.parts)
                blocks.push_back(part_block(part));
            push(messages, "user", std::move(blocks));
            break;
        }
        case RecordItem::Kind::Operator: push_system(messages, item.text, item.clears && clears); break;
        case RecordItem::Kind::Assistant:
            // The response's own content, thinking included, exactly as it came.
            push(messages, "assistant", item.output);
            break;
        case RecordItem::Kind::Results: {
            json results = json::array();
            for (const RecordResult& result : item.results) {
                json block{{"type", "tool_result"}, {"tool_use_id", result.call_id}};
                json parts = json::array({text_block(result.output_json)});
                if (result.image)
                    parts.push_back(part_block(*result.image));
                block["content"] = std::move(parts);
                if (result.failed)
                    block["is_error"] = true;
                results.push_back(std::move(block));
            }
            push(messages, "user", std::move(results));
            break;
        }
        }
    }
    return messages;
}

std::uint64_t count(const json& usage, const char* field)
{
    return usage.contains(field) && usage[field].is_number_unsigned() ? usage[field].get<std::uint64_t>() : 0;
}

} // namespace

json anthropic_strict_schema(const json& schema)
{
    json projected = schema;
    project(projected);
    return projected;
}

ProviderRequest AnthropicCodec::build(const TurnTranscript& transcript, const ProviderProfile& profile, RequestKind kind) const
{
    const bool title = kind == RequestKind::ConversationTitle;
    json       messages = json::array();
    std::vector<Message> built = normalized(conversation(transcript, clears_skills(profile)));
    for (const Message& message : built)
        messages.push_back(message_json(message));
    // A breakpoint on the newest user content caches the turn so far for the
    // next request of the same turn.
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        if ((*it)["role"] == "user") {
            (*it)["content"].back()["cache_control"] = ephemeral();
            break;
        }

    ProviderRequest request;
    if (clears_skills(profile))
        request.headers.push_back({"anthropic-beta", kClearAtBeta});
    request.body = json{{"model", profile.model},
                        {"max_tokens", profile.max_tokens},
                        {"stream", true},
                        {"system", json::array({json{{"type", "text"},
                                                     {"text", title ? std::string(kConversationTitleInstructions) : transcript.instructions},
                                                     {"cache_control", ephemeral()}}})},
                        {"messages", std::move(messages)}};
    if (!title && !transcript.tools.empty())
        request.body["tools"] = tools_json(transcript.tools);
    // The model list offers levels only for models with adaptive thinking,
    // where the level is effort and thinking stays adaptive.
    if (!title && !profile.effort.empty()) {
        request.body["thinking"]      = json{{"type", "adaptive"}};
        request.body["output_config"] = json{{"effort", profile.effort}};
    }
    // An empty system text is not accepted; the loop never builds one for a
    // reply (the page supplies the instructions), but a test may.
    if (request.body["system"][0]["text"] == "")
        request.body.erase("system");
    return request;
}

void AnthropicCodec::reset()
{
    m_blocks.clear();
    m_usage = AgentUsage{};
    m_stop_reason.clear();
    m_stop_details = json();
}

std::vector<StreamEvent> AnthropicCodec::parse(const std::string& payload)
{
    std::vector<StreamEvent> events;
    const auto               failed = [&events](AgentError error) {
        StreamEvent event;
        event.kind  = StreamEvent::Kind::Failed;
        event.error = std::move(error);
        events.push_back(std::move(event));
    };
    const json event = json::parse(payload, nullptr, false);
    if (!event.is_object()) {
        failed({"malformed_response", "The Agent service returned an unreadable streaming response.", false});
        return events;
    }
    const std::string type = event.value("type", "");
    if (type == "message_start") {
        const json usage   = event.value("message", json::object()).value("usage", json::object());
        m_usage.cached_input   = count(usage, "cache_read_input_tokens");
        m_usage.cache_creation = count(usage, "cache_creation_input_tokens");
        // Anthropic counts cache reads and writes apart from input_tokens;
        // AgentUsage's input is all of it, as OpenAI reports it.
        m_usage.input  = count(usage, "input_tokens") + m_usage.cached_input + m_usage.cache_creation;
        m_usage.output = count(usage, "output_tokens");
    } else if (type == "content_block_start") {
        m_blocks[event.value("index", 0)] = Block{event.value("content_block", json::object()), {}};
    } else if (type == "content_block_delta") {
        const auto found = m_blocks.find(event.value("index", 0));
        if (found == m_blocks.end()) {
            failed({"malformed_response", "The Agent service streamed a delta for a block it never opened.", false});
            return events;
        }
        Block&            block = found->second;
        const json        delta = event.value("delta", json::object());
        const std::string kind  = delta.value("type", "");
        if (kind == "text_delta") {
            const std::string text = delta.value("text", "");
            block.content["text"]  = block.content.value("text", "") + text;
            StreamEvent out;
            out.kind = StreamEvent::Kind::TextDelta;
            out.text = text;
            events.push_back(std::move(out));
        } else if (kind == "input_json_delta") {
            block.partial_json += delta.value("partial_json", "");
        } else if (kind == "thinking_delta") {
            block.content["thinking"] = block.content.value("thinking", "") + delta.value("thinking", "");
        } else if (kind == "signature_delta") {
            block.content["signature"] = block.content.value("signature", "") + delta.value("signature", "");
        }
    } else if (type == "content_block_stop") {
        const auto found = m_blocks.find(event.value("index", 0));
        if (found != m_blocks.end() && found->second.content.value("type", "") == "tool_use") {
            const std::string& text  = found->second.partial_json;
            const json         input = json::parse(text.empty() ? "{}" : text, nullptr, false);
            if (input.is_discarded()) {
                failed({"malformed_response", "The Agent service sent a tool call whose arguments are not JSON.", false});
                return events;
            }
            found->second.content["input"] = input;
        }
    } else if (type == "message_delta") {
        const json delta = event.value("delta", json::object());
        if (delta.contains("stop_reason") && delta["stop_reason"].is_string())
            m_stop_reason = delta["stop_reason"].get<std::string>();
        if (delta.contains("stop_details"))
            m_stop_details = delta["stop_details"];
        const json usage = event.value("usage", json::object());
        if (usage.contains("output_tokens"))
            m_usage.output = count(usage, "output_tokens");
    } else if (type == "message_stop") {
        events.push_back(finish());
    } else if (type == "error") {
        failed({"agent_service_error", "The Agent service could not complete the response.", true});
    }
    return events;
}

StreamEvent AnthropicCodec::finish()
{
    StreamEvent event;
    if (m_stop_reason == "max_tokens" || m_stop_reason == "model_context_window_exceeded") {
        event.kind = StreamEvent::Kind::Incomplete;
        return event;
    }
    if (m_stop_reason == "refusal") {
        event.kind = StreamEvent::Kind::Failed;
        std::string category = m_stop_details.is_object() && m_stop_details.value("category", json()).is_string() ?
                                   m_stop_details["category"].get<std::string>() : std::string("unspecified");
        event.error = {"refusal", "The Agent service declined to answer (" + category + ").", false};
        return event;
    }
    if (m_stop_reason != "end_turn" && m_stop_reason != "tool_use" && m_stop_reason != "stop_sequence" &&
        m_stop_reason != "pause_turn") {
        event.kind  = StreamEvent::Kind::Failed;
        event.error = {"malformed_response", "The Agent service ended its response for a reason this build does not know.", false};
        return event;
    }
    event.kind           = StreamEvent::Kind::TurnComplete;
    event.assistant_turn = json::array();
    for (auto& [index, block] : m_blocks) {
        if (block.content.value("type", "") == "tool_use")
            event.calls.push_back({block.content.value("id", ""), block.content.value("name", ""),
                                   block.partial_json.empty() ? "{}" : block.partial_json});
        event.assistant_turn.push_back(block.content);
    }
    m_usage.total = m_usage.input + m_usage.output;
    event.usage   = m_usage;
    event.resend  = m_stop_reason == "pause_turn";
    return event;
}

} // namespace Slic3r::GUI::JusPrin::Agent
