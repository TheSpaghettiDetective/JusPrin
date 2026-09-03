#pragma once

// Tool activity records and the approval policy for Agent-initiated project
// changes. The records carry the same semantic fields a future MCP-backed
// Agent must produce (tool and server identity, typed arguments, stable IDs,
// approval requirement, workspace session and expected revision, lifecycle
// state, progress, and structured result), so the coordinator, bridge, and
// page cannot special-case the deterministic mock. GUI-free.

#include <cstdint>
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

// Approval classes from the handoff policy. ReadOnly actions do not change
// durable project or external state; Mutation actions durably change the
// project; Destructive actions revert, delete, overwrite, discard, print, or
// export.
enum class ActionClass : std::uint8_t { ReadOnly, Mutation, Destructive };

// The first production release asks for approval before every durable
// project mutation; read-only actions run without approval.
constexpr bool approval_required(ActionClass action_class)
{
    return action_class != ActionClass::ReadOnly;
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
};

// Untrusted call data supplied by an adapter. The coordinator resolves title,
// action class, validation, and implementation from the registry.
struct ToolRequest
{
    std::string tool;
    std::string arguments_json; // typed arguments, serialized
};

struct ToolActivity
{
    std::string   action_id;      // coordinator-assigned, stable across reloads
    std::string   correlation_id; // the assistant message that proposed the action
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
    std::optional<ToolError> error;
};

} // namespace Slic3r::GUI::JusPrin::Agent
