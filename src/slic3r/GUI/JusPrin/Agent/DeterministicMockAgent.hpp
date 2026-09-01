#pragma once

// Deterministic mock Agent for the first production release. It produces the
// same semantic activity a future Agent or MCP adapter must produce (streamed
// assistant replies with success, failure, and retry behavior, plus typed
// tool requests for the native coordinator) so the host, bridge, and page
// cannot special-case mock behavior. GUI-free.

#include "AgentService.hpp"

#include <deque>

namespace Slic3r::GUI::JusPrin::Agent {

class DeterministicMockAgent : public IAgentService
{
public:
    // A compact, non-binary view of one sent attachment. Model/project files
    // reach the Agent only through `summary`, never as decoded bytes.
    struct AttachmentContext
    {
        std::string id;
        std::string name;
        std::string kind;
        std::string summary;
        bool        importable{false}; // a file-based model that can be imported
    };

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
    //   text starting with "/build"    -> records the sliced active plate;
    //   text starting with "/export"   -> records a verified exported copy of
    //                                     the latest build;
    //   text starting with "/print"    -> records a completed physical print
    //                                     in the non-revertible ledger;
    //   text containing "duplicate"    -> proposes duplicating the selected
    //                                     object (approval required);
    //   anything else                  -> a streamed summary of the workspace
    //                                     context and the selected objects.
    static Reply reply_for(const std::string& user_text, int attempt, const Workspace::WorkspaceSnapshot& context,
                           const std::vector<AttachmentContext>& attachments = {});

    bool ready() const override { return true; }
    bool busy() const override { return m_busy; }
    bool start(const AgentRequest& request) override;
    bool continue_after_tool(const AgentToolResult&) override { return false; }
    void cancel() override;
    std::optional<AgentEvent> poll() override;

private:
    std::deque<AgentEvent> m_events;
    bool                   m_busy{false};
};

} // namespace Slic3r::GUI::JusPrin::Agent
