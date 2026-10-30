#pragma once

// Replaceable input to AgentHost. Implementations may be deterministic and
// pump-driven or backed by an asynchronous network service, but they expose
// the same typed event stream. AgentHost remains the owner of conversation
// persistence and ToolExecutionCoordinator remains the only workspace
// execution path.

#include "AgentProtocol.hpp"
#include "ProjectStateDocument.hpp"
#include "ToolExecution.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <functional>
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

// A project skill as its page wrote it (AgentUI/src/skills/<name>/SKILL.md).
// A skill with a trigger is added by the app when that event happens; one
// with a description is listed for the model to read with skill_read when
// it judges it useful. A skill may have both.
struct AgentSkill
{
    std::string name;
    std::string trigger;     // Protocol::SkillTrigger, or empty
    std::string description; // one line, or empty
    std::string text;
};

// A skill the app fired for one request because its event happened: the text
// goes in as the request's last item, after the picture that goes with it.
// The chat's record keeps both as sent.
struct AgentSkillUse
{
    std::string                      name;
    std::string                      text;
    std::shared_ptr<const ToolImage> picture;
    std::string                      trigger;
    // Triggers of skills this one gives way to: when one of them fires for
    // the same request, this one is left out of it.
    std::vector<std::string>         yields_to;
};

// What one conversation is for. The project conversation leaves the tools and
// workspace at their defaults (every in-app tool, the project workspace) and
// takes its instructions from its page; a session with its own subject (the
// temporary printer task chat) states its instructions and names the tools
// that belong to it.
struct AgentSessionProfile
{
    // The whole system prompt, written by the session's page; the project
    // assistant's comes from AgentUI/src/projectInstructions.ts.
    std::string              instructions;
    std::vector<std::string> tool_names;               // empty: every in-app tool
    bool                     include_workspace{true};  // the open project's state
};

struct AgentRequest
{
    enum class Purpose { Reply, ConversationTitle };
    Purpose                              purpose{Purpose::Reply};
    std::string                           request_id;
    // The same on every request of one chat and different for every other
    // chat: what a provider routes its prompt cache by. Empty for a title.
    std::string                           chat_key;
    // What the chat has sent before, picture text loaded, and where the
    // items this turn sends go to be kept: the record of the chat the turn
    // belongs to, and the blob each picture or PDF is written to before the
    // request that first sends it (nullopt when it could not be written).
    // Required for a reply; a title keeps nothing.
    ChatRecord                            record;
    std::function<void(const std::string& format, std::vector<RecordItem> items)> on_recorded;
    std::function<std::optional<std::string>(const std::string& base64)>         keep_blob;
    // The chat message this turn answers, when the model has not been sent it
    // yet: its id, and its words or report, or the app's note that started
    // the turn, which the model reads in the operator's voice.
    std::string                           source;
    std::string                           user_text;
    std::string                           note;
    // A user message sent from a print-issue button: the app's JSON about the
    // issue, sent beside the person's words, never inside them.
    std::string                           issue_context;
    // The record holds the message this turn answers (a retry): the turn
    // adds no workspace of its own.
    bool                                  turn_recorded{false};
    int                                   attempt{1};
    AgentSessionProfile                   session;
    Workspace::WorkspaceSnapshot          workspace;
    // The chat's own record of this print, sent with the workspace: the
    // purpose the person gave (the setup card's title) and the plan the
    // model pinned, both recorded with setup_record.
    std::string                           setup_title;
    PlanRecord                            plan;
    // The presets or the plate type in force differ from those the model was
    // last asked to check against the recorded purpose.
    bool                                  setup_unchecked_change{false};
    // The G-code that most recently left the project, newest first, each with
    // whether its plate's manufacturing inputs are still what they were then:
    // what the model reads when the person says how a print went.
    struct GcodeHistoryEntry
    {
        GcodeRecord record;
        bool        same_inputs_as_now{false};
    };
    std::vector<GcodeHistoryEntry>        gcode_history;
    // Messages before this turn's the model has not been sent, as they go:
    // the person's side (words with any print issue, file-load reports) and
    // the app's notes in the operator's voice.
    std::vector<RecordItem>               unsent;
    std::vector<AgentAttachmentContext>   attachments;
    std::vector<AgentSkillUse>            skills;
    // The skills the model may read on its own, without their text: the
    // list skill_read's description carries. Empty, and no skill_read.
    std::vector<AgentSkill>               skill_index;
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
    // Skills this result fires, sent after every result of the response.
    std::vector<AgentSkillUse> skills;
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
