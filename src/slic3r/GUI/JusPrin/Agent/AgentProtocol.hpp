#pragma once

// Versioned JSON bridge protocol between the native AgentHost and the local
// React Agent page. The canonical protocol description shared with the web
// package is resources/jusprin/agent/protocol.json; the contract tests assert
// that these constants and that file agree. This header is GUI-free.

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

namespace Protocol {

inline constexpr int         kVersion = 1;
inline constexpr const char* kName    = "jusprin-agent-bridge";

inline const std::vector<std::string>& capabilities()
{
    static const std::vector<std::string> values{"streaming", "stop", "retry", "context", "appearance"};
    return values;
}

// Messages the page may send to the host.
inline constexpr const char* kHello          = "hello";
inline constexpr const char* kStateRequest   = "state_request";
inline constexpr const char* kUserMessage    = "user_message";
inline constexpr const char* kStopGeneration = "stop_generation";
inline constexpr const char* kRetryMessage   = "retry_message";

// Messages the host sends to the page.
inline constexpr const char* kHelloAck           = "hello_ack";
inline constexpr const char* kHelloReject        = "hello_reject";
inline constexpr const char* kState              = "state";
inline constexpr const char* kContext            = "context";
inline constexpr const char* kAppearance         = "appearance";
inline constexpr const char* kAgentStatus        = "agent_status";
inline constexpr const char* kMessageAdded       = "message_added";
inline constexpr const char* kAssistantStarted   = "assistant_started";
inline constexpr const char* kAssistantDelta     = "assistant_delta";
inline constexpr const char* kAssistantCompleted = "assistant_completed";
inline constexpr const char* kAssistantFailed    = "assistant_failed";
inline constexpr const char* kAssistantStopped   = "assistant_stopped";
inline constexpr const char* kBridgeError        = "bridge_error";

} // namespace Protocol

// The Agent service and the bridge are different failures: Unavailable is the
// clean "service not configured" empty state, while bridge errors are internal
// connection failures surfaced separately.
enum class AgentAvailability { Ready, Unavailable };

struct AgentError
{
    std::string code;
    std::string message;
    bool        retryable{false};
};

enum class MessageRole : std::uint8_t { User, Assistant };
enum class MessageState : std::uint8_t { Complete, Streaming, Failed, Stopped };

struct ConversationMessage
{
    std::string               id;                // host-assigned, stable across reloads
    MessageRole               role{MessageRole::User};
    MessageState              state{MessageState::Complete};
    std::string               text;
    std::string               client_message_id; // user messages: page-supplied dedup ID
    std::string               in_reply_to;       // assistant messages: the user message ID
    std::optional<AgentError> error;
    int                       attempt{1};
};

} // namespace Slic3r::GUI::JusPrin::Agent
