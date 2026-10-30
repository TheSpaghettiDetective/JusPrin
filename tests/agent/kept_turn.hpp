#pragma once

// What a provider agent keeps of a turn in the chat's record, for the test
// agents that stand in for one: the person's side as it was sent, the app's
// notes after it (the turn's own last), and the replies, so a later request carries them the way
// the real loop's record does. Pictures and skills are left to the tests that
// run the real loop.

#include "slic3r/GUI/JusPrin/Agent/ProviderCodec.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace JusPrinTest {

inline Slic3r::GUI::JusPrin::Agent::RecordPart kept_text(const std::string& words)
{
    return {Slic3r::GUI::JusPrin::Agent::RecordPart::Kind::Text, words};
}

inline std::vector<Slic3r::GUI::JusPrin::Agent::RecordItem> kept_person_side(const Slic3r::GUI::JusPrin::Agent::AgentRequest& request)
{
    using namespace Slic3r::GUI::JusPrin::Agent;
    // The host hands the earlier messages over as they go; this turn's own
    // is the words, then any print issue's context in its tag.
    std::vector<RecordItem> items;
    for (const RecordItem& item : request.unsent)
        if (item.kind == RecordItem::Kind::User)
            items.push_back(item);
    if (!request.source.empty() && request.note.empty()) {
        RecordItem turn{RecordItem::Kind::User, request.source, {kept_text(request.user_text)}};
        if (!request.issue_context.empty())
            turn.parts.push_back(kept_text(tagged_block("print_issue", request.issue_context)));
        items.push_back(std::move(turn));
    }
    for (const RecordItem& item : request.unsent)
        if (item.kind == RecordItem::Kind::Operator)
            items.push_back(item);
    if (!request.note.empty())
        items.push_back({RecordItem::Kind::Operator, request.source, {}, request.note});
    return items;
}

// A reply as a Responses message item.
inline Slic3r::GUI::JusPrin::Agent::RecordItem kept_reply(const std::string& words)
{
    return {Slic3r::GUI::JusPrin::Agent::RecordItem::Kind::Assistant, {}, {}, {},
            nlohmann::json{{"type", "message"}, {"role", "assistant"}, {"content", words}}};
}

// A call as a Responses function_call item.
inline Slic3r::GUI::JusPrin::Agent::RecordItem kept_call(const std::string& call_id, const std::string& tool,
                                                         const std::string& arguments_json)
{
    return {Slic3r::GUI::JusPrin::Agent::RecordItem::Kind::Assistant, {}, {}, {},
            nlohmann::json{{"type", "function_call"}, {"call_id", call_id}, {"name", tool}, {"arguments", arguments_json}}};
}

inline Slic3r::GUI::JusPrin::Agent::RecordItem kept_result(const Slic3r::GUI::JusPrin::Agent::AgentToolResult& result)
{
    Slic3r::GUI::JusPrin::Agent::RecordItem item{Slic3r::GUI::JusPrin::Agent::RecordItem::Kind::Results};
    item.results.push_back({result.call_id, result.output_json, result.state != "succeeded"});
    return item;
}

// A reply request whose record goes nowhere, for tests of one request's wire:
// every blob is written and every item dropped.
inline Slic3r::GUI::JusPrin::Agent::AgentRequest& kept_nowhere(Slic3r::GUI::JusPrin::Agent::AgentRequest& request)
{
    request.on_recorded = [](const std::string&, std::vector<Slic3r::GUI::JusPrin::Agent::RecordItem>) {};
    request.keep_blob   = [](const std::string&) -> std::optional<std::string> { return "chats/test/blob"; };
    return request;
}

inline void keep(const Slic3r::GUI::JusPrin::Agent::AgentRequest& request,
                 std::vector<Slic3r::GUI::JusPrin::Agent::RecordItem> items)
{
    if (request.on_recorded && !items.empty())
        request.on_recorded("responses", std::move(items));
}

} // namespace JusPrinTest
