#pragma once

// Tool activity records for Agent-initiated project changes. The records carry
// the same semantic fields a future MCP-backed
// Agent must produce (tool and server identity, typed arguments, stable IDs,
// workspace session and expected revision, lifecycle
// state, progress, and structured result), so the coordinator, bridge, and
// page cannot special-case the deterministic mock. GUI-free.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

// Lifecycle of one tool action. Cancelled, Succeeded, and Failed are terminal.
// A stale action fails with error code
// "stale_revision" rather than getting its own state.
enum class ToolState : std::uint8_t { Running, Succeeded, Failed, Cancelled };

constexpr bool tool_state_terminal(ToolState state)
{
    return state == ToolState::Succeeded || state == ToolState::Failed || state == ToolState::Cancelled;
}

// The name the page, the saved state and the tools use.
constexpr const char* tool_state_name(ToolState state)
{
    switch (state) {
    case ToolState::Running: return "running";
    case ToolState::Succeeded: return "succeeded";
    case ToolState::Failed: return "failed";
    case ToolState::Cancelled: return "cancelled";
    }
    return "running";
}

// Action classes describe effects and feed MCP annotations. ReadOnly actions do not change
// durable project or external state; Mutation actions durably change the
// project; Destructive actions revert, delete, overwrite, discard, print, or
// export.
enum class ActionClass : std::uint8_t { ReadOnly, Mutation, Destructive };

// Assigned by the native adapter, never by untrusted tool arguments. External
// requests belong to the project, not to an in-app conversation message.
enum class ToolSource : std::uint8_t { Agent, Mcp };

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
    // The provider's id for the call that started this activity, so a later
    // turn can replay the call with its result; empty for a call from MCP.
    std::string   call_id;
    std::string   server;
    std::string   tool;
    std::string   title;
    std::string   arguments_json;
    ActionClass   action_class{ActionClass::ReadOnly};
    std::uint64_t session{0};           // workspace session when queued
    std::uint64_t expected_revision{0}; // workspace revision when queued
    ToolState     state{ToolState::Running};
    int           progress_current{0};
    int           progress_total{1};
    std::string   result_json;                // structured result when Succeeded
    std::shared_ptr<const ToolImage> image; // a picture beside the result, when the tool returns one
    std::optional<ToolError> error;
    ToolSource   source{ToolSource::Agent};
};

} // namespace Slic3r::GUI::JusPrin::Agent
