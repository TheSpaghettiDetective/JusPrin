#pragma once

// A chat can hold turns in both wire formats: on a JusPrin account the turns
// the app starts run on the Expert alias, which may be served in another
// format than the chat's model, and on any provider the app's reply to an
// opened file runs before the person picks the chat's model. A record item keeps the
// provider's own output, so a codec reading another format's turn converts it
// here. The same input always gives the same bytes, so a later request repeats
// an earlier one's and the provider's cache still hits.

#include "ProjectStateDocument.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

// One assistant turn's output as `to` reads it: its words and its tool calls.
// Thinking and reasoning are left out, as each provider's are signed for itself.
nlohmann::json assistant_output_as(const nlohmann::json& output, const std::string& from, const std::string& to);

// A tool call's id as Anthropic accepts it. An id of its own is unchanged.
std::string anthropic_tool_id(const std::string& id);

// A chat's earlier turns as the next request sends them. A reply is kept as
// it arrives, so a turn that ended (stopped, failed, the app closed) before
// the app sent back its reply's results leaves calls without one, and the
// provider refuses a call without its result. Each such call gets a failed
// result saying the turn ended, right after its reply. A server tool call
// with no result anywhere is left out: Anthropic runs a web search asked for
// beside an app call only once the app's results come back, so a search
// whose turn ended first never ran, and Anthropic refuses a search without
// its result. The same items always give the same output, so later requests
// repeat it. `format` is the record's own, for replies that name none.
std::vector<RecordItem> with_ended_turns_closed(std::vector<RecordItem> items, const std::string& format);

} // namespace Slic3r::GUI::JusPrin::Agent
