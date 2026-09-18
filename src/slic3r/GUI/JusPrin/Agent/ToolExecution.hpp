#pragma once

// Tool activity records and the approval policy for Agent-initiated project
// changes. The records carry the same semantic fields a future MCP-backed
// Agent must produce (tool and server identity, typed arguments, stable IDs,
// approval requirement, workspace session and expected revision, lifecycle
// state, progress, and structured result), so the coordinator, bridge, and
// page cannot special-case the deterministic mock. GUI-free.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

// Lifecycle of one tool action. Pending awaits a user decision (or is
// auto-approved for read-only actions); Rejected, Cancelled, Succeeded, and
// Failed are terminal. A stale proposal fails with error code
// "stale_revision" rather than getting its own state.
enum class ToolState : std::uint8_t { Pending, Approved, Running, Succeeded, Failed, Cancelled, Rejected };

constexpr bool tool_state_terminal(ToolState state)
{
    return state == ToolState::Succeeded || state == ToolState::Failed || state == ToolState::Cancelled ||
           state == ToolState::Rejected;
}

// The name the page, the saved state and the tools use.
constexpr const char* tool_state_name(ToolState state)
{
    switch (state) {
    case ToolState::Pending: return "pending";
    case ToolState::Approved: return "approved";
    case ToolState::Running: return "running";
    case ToolState::Succeeded: return "succeeded";
    case ToolState::Failed: return "failed";
    case ToolState::Cancelled: return "cancelled";
    case ToolState::Rejected: return "rejected";
    }
    return "pending";
}

// Approval classes from the handoff policy. ReadOnly actions do not change
// durable project or external state; Mutation actions durably change the
// project; Destructive actions revert, delete, overwrite, discard, print, or
// export.
enum class ActionClass : std::uint8_t { ReadOnly, Mutation, Destructive };

// Assigned by the native adapter, never by untrusted tool arguments. External
// requests belong to the project, not to an in-app conversation message.
enum class ToolSource : std::uint8_t { Agent, Mcp };

// The first production release asks for approval before every durable
// project mutation; read-only actions run without approval.
//
// One exemption: a mutation declared computation-only in the registry. It
// changes no project geometry, preset, file, printer, or durable product
// state; its only effect is computation, replacing a previously computed
// result, or recording the agent's own statement; and the person can see and
// reverse it in the UI. Without it every "check this print" would cost a card
// and the card would stop meaning anything. Destructive actions never
// qualify, whatever they declare.
constexpr bool approval_required(ActionClass action_class, bool computation_only = false)
{
    return action_class == ActionClass::Destructive ||
           (action_class == ActionClass::Mutation && !computation_only);
}

// Destructive actions always require action-time approval and must never use
// a remembered approval. (No remembered approvals exist in this release; the
// policy still records which class may ever gain them.)
constexpr bool remembered_approval_allowed(ActionClass action_class)
{
    return action_class == ActionClass::Mutation;
}

struct ToolError
{
    std::string code;
    std::string message;
    std::string details_json{"{}"};
};

// Untrusted call data supplied by an adapter. The coordinator resolves title,
// action class, validation, and implementation from the registry.
struct ToolRequest
{
    std::string tool;
    std::string arguments_json; // typed arguments, serialized
};

// A picture a read returns beside its structured result: PNG or JPEG,
// base64-encoded, at most 1280 pixels on its long edge and 2 MB. Held in
// memory for the adapters to project; never written to the project.
struct ToolImage
{
    std::string mime_type;
    std::string base64;
    int         width{0};
    int         height{0};
};

inline constexpr int         kToolImageEdge  = 1280;
inline constexpr std::size_t kToolImageBytes = 2 * 1024 * 1024;

struct ToolActivity
{
    std::string   action_id;      // coordinator-assigned, stable across reloads
    std::string   correlation_id; // assistant message or adapter request correlation
    std::string   server;
    std::string   tool;
    std::string   title;
    std::string   arguments_json;
    ActionClass   action_class{ActionClass::ReadOnly};
    bool          requires_approval{false};
    std::uint64_t session{0};           // workspace session at proposal time
    std::uint64_t expected_revision{0}; // workspace revision at proposal time
    ToolState     state{ToolState::Pending};
    int           progress_current{0};
    int           progress_total{1};
    std::string   result_json; // structured result when Succeeded
    std::shared_ptr<const ToolImage> image; // a picture beside the result, when the tool returns one
    // Calls sharing a plan id wait for one approval and run in the order they
    // were proposed; empty for a call on its own.
    std::string   plan_id;
    // Where a plan id is unique: the chat for the in-app agent, empty for MCP.
    // A plan is its source, scope and id together, so two clients choosing
    // the same id never share a card.
    std::string   plan_scope;
    std::optional<ToolError> error;
    ToolSource source{ToolSource::Agent};
    // A human summary the card draws instead of the tool's name and server,
    // for a call whose session could preview it at proposal time: what
    // changed from what to what, and what it means, in the caller's own
    // words. Empty subtitle leaves the card in its generic, title/tool/server
    // shape. See ToolExecutionCoordinator::set_approval_preview_executor.
    std::string   subtitle;      // "0.4 mm -> 0.6 mm on Bambu Lab A1 mini"
    std::string   consequence;   // "Every project that uses this printer slices for 0.6 mm."
    std::string   accept_label;  // "Set 0.6 mm"; empty means the generic "Approve"
    std::string   decline_label; // "Keep 0.4 mm"; empty means the generic "Reject"
};

inline bool same_plan(const ToolActivity& lhs, const ToolActivity& rhs)
{
    return !lhs.plan_id.empty() && lhs.plan_id == rhs.plan_id && lhs.source == rhs.source && lhs.plan_scope == rhs.plan_scope;
}

// What a session hands the card in place of the tool's name, for a call it
// can preview without applying it. Returned by an ApprovalPreviewExecutor;
// nullopt leaves the generic title the registry already computed.
struct ApprovalPreview
{
    // Replaces the registry's title when non-empty; empty keeps it.
    std::string title;
    std::string subtitle;
    std::string consequence;
    std::string accept_label;
    std::string decline_label;
};

} // namespace Slic3r::GUI::JusPrin::Agent
