#pragma once

// Deterministic mock Agent for the first production release. It produces the
// same semantic activity a future Agent or MCP adapter must produce (streamed
// assistant replies with success, failure, and retry behavior, plus typed
// tool requests for the native coordinator) so the host, bridge, and page
// cannot special-case mock behavior. GUI-free.

#include "AgentProtocol.hpp"
#include "ToolExecution.hpp"
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
        // Proposed to the ToolExecutionCoordinator when the stream completes
        // successfully. The coordinator, not the Agent, applies the approval
        // policy and executes through the workspace contract.
        std::optional<ToolRequest> tool;
    };

    // Deterministic scenarios, documented for tests and manual scripts:
    //   text starting with "/fail"     -> fails on every attempt (retryable);
    //   text starting with "/flaky"    -> fails on attempt 1, succeeds after;
    //   text starting with "/slow"     -> long streamed reply;
    //   text starting with "/toolfail" -> proposes duplicating an object that
    //                                     does not exist, so an approved run
    //                                     fails deterministically;
    //   text starting with "/toolslow" -> proposes the duplicate with a long
    //                                     progress run for cancellation tests;
    //   text starting with "/inspect"  -> read-only selection inspection that
    //                                     runs without approval by policy;
    //   text containing "duplicate"    -> proposes duplicating the selected
    //                                     object (approval required);
    //   anything else                  -> a streamed summary of the workspace
    //                                     context and the selected objects.
    static Reply reply_for(const std::string& user_text, int attempt, const Workspace::WorkspaceSnapshot& context);
};

} // namespace Slic3r::GUI::JusPrin::Agent
