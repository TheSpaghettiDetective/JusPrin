#include "RecordConversion.hpp"

#include "ProjectStateDocument.hpp"
#include "ProviderProfile.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace Slic3r::GUI::JusPrin::Agent {

std::string anthropic_tool_id(const std::string& id)
{
    std::string safe = id;
    for (char& c : safe)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            c = '_';
    return safe;
}

namespace {

json responses_from_anthropic(const json& output)
{
    json items = json::array();
    for (const json& block : output) {
        const std::string type = block.at("type").get<std::string>();
        if (type == "text")
            items.push_back(json{{"type", "message"}, {"role", "assistant"},
                                 {"content", json::array({json{{"type", "output_text"}, {"text", block.at("text")}}})}});
        else if (type == "tool_use")
            items.push_back(json{{"type", "function_call"}, {"call_id", block.at("id")}, {"name", block.at("name")},
                                 {"arguments", block.at("input").dump()}});
    }
    return items;
}

json anthropic_from_responses(const json& output)
{
    json blocks = json::array();
    for (const json& item : output) {
        const std::string type = item.at("type").get<std::string>();
        if (type == "message") {
            for (const json& part : item.at("content"))
                if (part.at("type") == "output_text")
                    blocks.push_back(json{{"type", "text"}, {"text", part.at("text")}});
        } else if (type == "function_call") {
            // A call the model wrote badly is still the call it made; the
            // record says so with an empty input rather than losing the pair.
            const json input = json::parse(item.at("arguments").get<std::string>(), nullptr, false);
            blocks.push_back(json{{"type", "tool_use"}, {"id", anthropic_tool_id(item.at("call_id").get<std::string>())},
                                  {"name", item.at("name")}, {"input", input.is_object() ? input : json::object()}});
        }
    }
    return blocks;
}

// The app's tool calls in one reply, by id, in order.
std::vector<std::string> call_ids(const json& output, const std::string& format)
{
    std::vector<std::string> ids;
    if (!output.is_array())
        return ids;
    for (const json& part : output) {
        const std::string type = part.is_object() ? part.value("type", "") : std::string();
        if (format == kFormatAnthropic && type == "tool_use")
            ids.push_back(part.at("id").get<std::string>());
        else if (format == kFormatResponses && type == "function_call")
            ids.push_back(part.at("call_id").get<std::string>());
    }
    return ids;
}

RecordResult turn_ended(const std::string& call_id)
{
    const json output{{"state", "failed"},
                      {"error", json{{"code", "turn_ended"},
                                     {"message", "No result: the turn ended before this call's result was sent back. It may or "
                                                 "may not have run. Check before calling it again."}}}};
    return RecordResult{call_id, output.dump(), true, std::nullopt};
}

} // namespace

std::vector<RecordItem> with_ended_turns_closed(std::vector<RecordItem> items, const std::string& format)
{
    // Every server tool result in the chat, by the call it answers: a search's
    // result comes in the reply after the one that asked for it.
    std::set<std::string> answered_server_calls;
    for (const RecordItem& item : items)
        if (item.kind == RecordItem::Kind::Assistant && item.output.is_array() &&
            (item.output_format.empty() ? format : item.output_format) == kFormatAnthropic)
            for (const json& block : item.output)
                if (block.is_object() && block.contains("tool_use_id") && block.value("type", "") != "tool_result")
                    answered_server_calls.insert(block.at("tool_use_id").get<std::string>());

    std::vector<RecordItem> closed;
    for (std::size_t i = 0; i < items.size(); ++i) {
        RecordItem item = std::move(items[i]);
        if (item.kind != RecordItem::Kind::Assistant) {
            closed.push_back(std::move(item));
            continue;
        }
        const std::string item_format = item.output_format.empty() ? format : item.output_format;
        if (item_format == kFormatAnthropic && item.output.is_array()) {
            json kept = json::array();
            for (const json& block : item.output)
                if (!block.is_object() || block.value("type", "") != "server_tool_use" ||
                    answered_server_calls.count(block.value("id", "")) > 0)
                    kept.push_back(block);
            item.output = std::move(kept);
        }
        const std::vector<std::string> calls = call_ids(item.output, item_format);
        closed.push_back(std::move(item));
        if (calls.empty())
            continue;
        // The reply's results follow it, or nothing of them does.
        const bool has_results = i + 1 < items.size() && items[i + 1].kind == RecordItem::Kind::Results;
        RecordItem results = has_results ? std::move(items[++i]) : RecordItem{RecordItem::Kind::Results};
        for (const std::string& call : calls)
            if (std::none_of(results.results.begin(), results.results.end(), [&call](const RecordResult& r) { return r.call_id == call; }))
                results.results.push_back(turn_ended(call));
        closed.push_back(std::move(results));
    }
    return closed;
}

json assistant_output_as(const json& output, const std::string& from, const std::string& to)
{
    if (from == to)
        return output;
    if (from == kFormatAnthropic && to == kFormatResponses)
        return responses_from_anthropic(output);
    if (from == kFormatResponses && to == kFormatAnthropic)
        return anthropic_from_responses(output);
    throw std::logic_error("a record item in format " + from + " read as " + to);
}

} // namespace Slic3r::GUI::JusPrin::Agent
