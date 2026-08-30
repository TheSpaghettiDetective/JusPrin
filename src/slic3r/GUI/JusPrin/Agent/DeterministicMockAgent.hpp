#pragma once

// Deterministic mock Agent for the first production release. It produces the
// same semantic activity a future Agent or MCP adapter must produce (streamed
// assistant replies with success, failure, and retry behavior) so the host,
// bridge, and page cannot special-case mock behavior. GUI-free.

#include "AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

class DeterministicMockAgent
{
public:
    struct Reply
    {
        // Streamed in order; on error the stream fails after the chunks.
        std::vector<std::string>  chunks;
        std::optional<AgentError> error;
    };

    // Deterministic scenarios, documented for tests and manual scripts:
    //   text starting with "/fail"  -> fails on every attempt (retryable);
    //   text starting with "/flaky" -> fails on attempt 1, succeeds after;
    //   text starting with "/slow"  -> long streamed reply;
    //   anything else               -> a streamed summary of the workspace
    //                                  context and the selected objects.
    static Reply reply_for(const std::string& user_text, int attempt, const Workspace::WorkspaceSnapshot& context);
};

} // namespace Slic3r::GUI::JusPrin::Agent
