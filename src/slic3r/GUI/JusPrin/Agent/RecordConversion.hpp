#pragma once

// A chat can hold turns in both wire formats: on a JusPrin account the turns
// the app starts run on the Expert alias, which may be served in another
// format than the chat's model, and on any provider the app's reply to an
// opened file runs before the person picks the chat's model. A record item keeps the
// provider's own output, so a codec reading another format's turn converts it
// here. The same input always gives the same bytes, so a later request repeats
// an earlier one's and the provider's cache still hits.

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

// One assistant turn's output as `to` reads it: its words and its tool calls.
// Thinking and reasoning are left out, as each provider's are signed for itself.
nlohmann::json assistant_output_as(const nlohmann::json& output, const std::string& from, const std::string& to);

// A tool call's id as Anthropic accepts it. An id of its own is unchanged.
std::string anthropic_tool_id(const std::string& id);

} // namespace Slic3r::GUI::JusPrin::Agent
