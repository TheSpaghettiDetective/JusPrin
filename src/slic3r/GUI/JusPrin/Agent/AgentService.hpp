#pragma once

// Replaceable input to AgentHost. Implementations may be deterministic and
// pump-driven or backed by an asynchronous network service, but they expose
// the same typed event stream. AgentHost remains the owner of conversation
// persistence and ToolExecutionCoordinator remains the only workspace
// execution path.

#include "AgentProtocol.hpp"
#include "ToolExecution.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct AgentAttachmentContext
{
    std::string id;
    std::string name;
    std::string kind;
    std::string mime;
    std::string summary;
    std::string text;  // decoded UTF-8 context, size bounded by AgentHost
    std::string bytes; // image/PDF bytes, size bounded by AgentHost
    bool        importable{false};
};

struct AgentConversationContext
{
    std::string role; // user|assistant
    std::string text;
};

// What one conversation is for. The project conversation leaves this at its
// defaults and gets the app's own assistant, the project workspace and every
// in-app tool; a session with its own subject (the printer panel on Home)
// states its instructions and names the tools that belong to it.
struct AgentSessionProfile
{
    std::string              instructions;             // empty: the app's assistant
    std::vector<std::string> tool_names;               // empty: every in-app tool
    bool                     include_workspace{true};  // the open project's state
    // The thread's notes reach the model as the app's own statements. Off
    // where the workspace is sent: there a note restates project state the
    // snapshot already carries, and a stale one would contradict it.
    bool                     notes_in_context{false};
};

struct AgentRequest
{
    enum class Purpose { Reply, ConversationTitle };
    Purpose                              purpose{Purpose::Reply};
    std::string                           request_id;
    std::string                           user_text;
    int                                   attempt{1};
    AgentSessionProfile                   session;
    Workspace::WorkspaceSnapshot          workspace;
    std::vector<AgentConversationContext> conversation;
    std::vector<AgentAttachmentContext>   attachments;
};

struct AgentToolCall
{
    std::string call_id; // provider call ID, opaque outside the service
    ToolRequest request;
    // A live provider normally needs the structured result to continue its
    // response. The deterministic mock keeps its historical one-message
    // behavior and sets this false.
    bool await_result{true};
    // Deterministic mock/test pacing. This is deliberately outside the
    // untrusted ToolRequest and is not part of any public tool contract.
    int test_run_ticks{1};
};

struct AgentToolResult
{
    std::string call_id;
    std::string state;
    std::string output_json;
    std::shared_ptr<const ToolImage> image;
};

enum class AgentEventKind : std::uint8_t { TextDelta, ToolCall, Completed, Failed };

struct AgentEvent
{
    AgentEventKind               kind{AgentEventKind::Completed};
    std::string                  text;
    std::optional<AgentToolCall> tool;
    std::optional<AgentError>    error;

    static AgentEvent delta(std::string value)
    {
        AgentEvent event;
        event.kind = AgentEventKind::TextDelta;
        event.text = std::move(value);
        return event;
    }

    static AgentEvent tool_call(AgentToolCall value)
    {
        AgentEvent event;
        event.kind = AgentEventKind::ToolCall;
        event.tool = std::move(value);
        return event;
    }

    static AgentEvent completed()
    {
        AgentEvent event;
        event.kind = AgentEventKind::Completed;
        return event;
    }

    static AgentEvent failed(AgentError value)
    {
        AgentEvent event;
        event.kind  = AgentEventKind::Failed;
        event.error = std::move(value);
        return event;
    }
};

class IAgentService
{
public:
    virtual ~IAgentService() = default;

    virtual bool ready() const = 0;
    virtual bool busy() const = 0;

    // Starts a user turn. Exactly one turn may be active at a time.
    virtual bool start(const AgentRequest& request) = 0;
    // Continues a provider tool call after the native coordinator reaches a
    // terminal state. The result is data, never authority to execute again.
    virtual bool continue_after_tool(const AgentToolResult& result) = 0;
    virtual void cancel() = 0;

    // Called on the GUI thread. Network-backed services queue worker-thread
    // events internally and release at most one here.
    virtual std::optional<AgentEvent> poll() = 0;
};

using AgentServicePtr = std::unique_ptr<IAgentService>;

} // namespace Slic3r::GUI::JusPrin::Agent
