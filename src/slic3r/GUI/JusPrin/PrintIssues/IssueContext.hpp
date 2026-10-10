#pragma once

// What a press of "Ask AI" or "Resolve with AI" sends to the chat: the
// sentence the person is shown as having said, the issue and its freshness as
// structured data for the model, and the id that makes a repeated press one
// message. GUI-free.

#include "PrintIssue.hpp"

#include <cstdint>
#include <string>

namespace Slic3r::GUI::JusPrin::PrintIssues {

// Explain asks for an explanation and nothing else; the host offers that turn
// only the tools that read. Resolve is an ordinary turn.
enum class IssueIntent : std::uint8_t { Explain, Resolve };

const char* intent_name(IssueIntent intent);

struct IssueMessage
{
    std::string client_message_id;
    std::string text;
    std::string context_json;
};

struct IssueMessageRequest
{
    IssueIntent   intent{IssueIntent::Explain};
    // The request in the person's language ("Explain this issue and my
    // options."); OrcaSlicer's own words for the issue are quoted beneath it.
    std::string   request_sentence;
    // What the tools call the workspace the issue belongs to.
    std::uint64_t workspace_session{0};
    std::uint64_t workspace_revision{0};
    std::string   conversation_id;
};

// `issue` must be one of `snapshot`'s.
IssueMessage make_issue_message(const PrintIssue& issue, const PrintIssueSnapshot& snapshot, const IssueMessageRequest& request);

} // namespace Slic3r::GUI::JusPrin::PrintIssues
