#pragma once

// Versioned JSON bridge protocol between the native AgentHost and the local
// React Agent page. The canonical protocol description shared with the web
// package is resources/jusprin/agent/protocol.json; the contract tests assert
// that these constants and that file agree. This header is GUI-free.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

namespace Protocol {

// The page and host ship in the same build, so this only guards a page/host
// mismatch at runtime — which cannot happen pre-release. It stays 1 through
// development (additive features are negotiated via the capabilities list
// below) and bumps only after go-live, on a breaking change to an existing
// message's shape.
inline constexpr int         kVersion = 1;
inline constexpr const char* kName    = "jusprin-agent-bridge";

inline const std::vector<std::string>& capabilities()
{
    static const std::vector<std::string> values{"streaming",   "stop",        "retry",
                                                 "context",     "appearance",  "tools",
                                                 "conversations", "revisions", "attachments",
                                                 "manufacturing_history", "agent_setup", "conversation_management"};
    return values;
}

// Messages the page may send to the host.
inline constexpr const char* kHello              = "hello";
inline constexpr const char* kStateRequest       = "state_request";
inline constexpr const char* kUserMessage        = "user_message";
inline constexpr const char* kStopGeneration     = "stop_generation";
inline constexpr const char* kRetryMessage       = "retry_message";
inline constexpr const char* kToolDecision       = "tool_decision";
inline constexpr const char* kToolCancel         = "tool_cancel";
inline constexpr const char* kCreateConversation = "create_conversation";
inline constexpr const char* kSwitchConversation = "switch_conversation";
inline constexpr const char* kRenameConversation = "rename_conversation";
inline constexpr const char* kDeleteConversation = "delete_conversation";
inline constexpr const char* kConversationsUpdated = "conversations_updated";
inline constexpr const char* kRevertToRevision   = "revert_to_revision";
inline constexpr const char* kDraftUpdate        = "draft_update";
inline constexpr const char* kAttachFile         = "attach_file";
inline constexpr const char* kRemoveAttachment   = "remove_attachment";
inline constexpr const char* kSetupCheckKey      = "setup_check_key";
inline constexpr const char* kSetupCancel        = "setup_cancel";

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
inline constexpr const char* kToolActivity       = "tool_activity";
inline constexpr const char* kRevisionAdded      = "revision_added";
inline constexpr const char* kBridgeError        = "bridge_error";
inline constexpr const char* kAttachmentUpdated  = "attachment_updated";
inline constexpr const char* kSetupStatus        = "setup_status";

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
    std::vector<std::string>  attachment_ids;    // user messages: sent attachment IDs
};

} // namespace Slic3r::GUI::JusPrin::Agent
