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
                                                 "conversations", "attachments",
                                                 "manufacturing_history", "agent_setup", "conversation_management",
                                                 "mcp_setup", "system_notes", "reveal_path", "change_log",
                                                 "printer_panel", "file_reports", "setup_differences", "work_log",
                                                 "project_instructions", "project_skills", "native_file_picker",
                                                 "model_choice", "all_settings", "settings_actions", "saved_keys",
                                                 "preferences"};
    return values;
}

// Messages the page may send to the host.
inline constexpr const char* kHello              = "hello";
inline constexpr const char* kStateRequest       = "state_request";
inline constexpr const char* kUserMessage        = "user_message";
inline constexpr const char* kStopGeneration     = "stop_generation";
inline constexpr const char* kRetryMessage       = "retry_message";
inline constexpr const char* kToolCancel         = "tool_cancel";
inline constexpr const char* kWorkCancel         = "work_cancel";
inline constexpr const char* kCreateConversation = "create_conversation";
inline constexpr const char* kSwitchConversation = "switch_conversation";
inline constexpr const char* kRestoreConversation = "restore_conversation";
inline constexpr const char* kRenameConversation = "rename_conversation";
inline constexpr const char* kDeleteConversation = "delete_conversation";
inline constexpr const char* kConversationsUpdated = "conversations_updated";
inline constexpr const char* kDraftUpdate        = "draft_update";
inline constexpr const char* kAttachFile         = "attach_file";
inline constexpr const char* kRemoveAttachment   = "remove_attachment";
inline constexpr const char* kSetupCheckKey      = "setup_check_key";
inline constexpr const char* kSetupCancel        = "setup_cancel";
// Deletes one provider's stored key: {provider}. The keys still stored follow
// as setup_keys; a refusal comes back as bridge_error. Added additively; the
// saved_keys capability advertises it.
inline constexpr const char* kSetupRemoveKey     = "setup_remove_key";
inline constexpr const char* kMcpCatalog         = "mcp_catalog";
inline constexpr const char* kMcpPreview         = "mcp_preview";
inline constexpr const char* kMcpConnect         = "mcp_connect";
// Reveal one host-known file in the desktop's file manager. The page names a
// catalog tool, never a path: the host resolves it, so nothing the page can
// say reaches the shell.
inline constexpr const char* kRevealPath         = "reveal_path";
// Opens the operating system's file dialog for one kind of attachment
// (image, model, gcode, pdf), filtered to the extensions the host reads as
// that kind. The page names the kind only; the chosen files arrive as
// attachment_updated, the same as a file the page read itself. Added
// additively; the native_file_picker capability advertises it.
inline constexpr const char* kPickAttachment     = "pick_attachment";
// The active chat's provider, model and thinking level, from the composer's
// pickers: {provider, model, effort}. It applies from the next turn. Added
// additively; the model_choice capability advertises it.
inline constexpr const char* kSetChatModel       = "set_chat_model";
// The All settings panel asks for every setting in force, in one message;
// the host answers with settings_catalog. Added additively; the all_settings
// capability advertises it.
inline constexpr const char* kSettingsCatalogRequest = "settings_catalog_request";
// A press on one of the panel's object-view links: {sessionId, objectId,
// action ("use_project_value" or "remove"), kind, target, key, value}, naming
// the row as the catalog listed it. The host makes the edit as one undo step
// of the person's; the new catalog follows the context revision. A refusal
// comes back as bridge_error. Added additively; the settings_actions
// capability advertises it.
inline constexpr const char* kSettingsAction         = "settings_action";

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
inline constexpr const char* kBridgeError        = "bridge_error";
inline constexpr const char* kAttachmentUpdated  = "attachment_updated";
inline constexpr const char* kSetupStatus        = "setup_status";
// Which providers have a stored key, never the key: {keys: [{provider, hint,
// rejected}]}, hint as api_key_hint() writes it, rejected when the provider
// refused that key in this session. Sent after the handshake and whenever one
// of them changes. saved_keys capability.
inline constexpr const char* kSetupKeys          = "setup_keys";
inline constexpr const char* kMcpStatus          = "mcp_status";
// One new change log entry; the full list travels in `state`.
inline constexpr const char* kChangeAdded        = "change_added";
// Work the application runs outside the chat: started, moved on, or ended.
inline constexpr const char* kWorkUpdated        = "work_updated";
// Each configured provider's models, sent after the handshake and again as
// each list arrives: {defaultProvider, providers: [{id, label, state, models:
// [{id, label, efforts, defaultEffort}]}]}. model_choice capability.
inline constexpr const char* kModelCatalog       = "model_catalog";
// The active chat's choice after a set_chat_model: {conversationId, chatModel}.
inline constexpr const char* kChatModel          = "chat_model";
// Every setting in force, for the All settings panel, in answer to
// settings_catalog_request: the process, printer and each filament slot's
// settings under OrcaSlicer's tab pages, each plate's own settings and each
// object's customizations. all_settings capability.
inline constexpr const char* kSettingsCatalog    = "settings_catalog";
// The printer panel's own: what its surface shows above and inside the
// thread, a tap on one of the cards it draws there, and the model's
// instructions, which the page writes.
inline constexpr const char* kPrinterSession      = "printer_session";
inline constexpr const char* kPrinterAction       = "printer_action";
inline constexpr const char* kPrinterInstructions = "printer_instructions";
// Its opening line, which the page writes and the app posts.
inline constexpr const char* kPrinterOpening      = "printer_opening";
// The filament chat's instructions, which the page writes from the facts
// the app sends as the state's session.
inline constexpr const char* kFilamentInstructions = "filament_instructions";
inline constexpr const char* kShellAction          = "shell_action";
// The project assistant's whole prompt, which the page writes
// (projectInstructions.ts); the app has no default text.
inline constexpr const char* kProjectInstructions    = "project_instructions";
// Every project skill, which the page writes (AgentUI/src/skills/*/SKILL.md),
// in one message: text the app adds to one request when the event its trigger
// names happens.
inline constexpr const char* kSkills                 = "skills";
// The Preferences window's own: one change or command from the page,
// {action: "set", name, value} or {action: <command>}, and the settings it
// shows, {kind: "preferences", ...}, sent again after every change. The same
// settings travel in `state` as its session. preferences capability.
inline constexpr const char* kPreferencesAction      = "preferences_action";
inline constexpr const char* kPreferences            = "preferences";

// The events a skill can name as its trigger. The app fires each one itself;
// the model never chooses a skill.
namespace SkillTrigger {
// A file was opened or imported: the person opened or dropped one, or a
// tool call of the in-app Agent opened or imported one.
inline constexpr const char* kFileLoaded = "file_loaded";
// A plate's slice did not succeed: OrcaSlicer refused to start it, or the run
// failed. Fired on the first request that carries the failure, whoever asked
// for the slice: the reply to the model's slice_start, or the next request
// after the person's.
inline constexpr const char* kSliceFailed  = "slice_failed";
// OrcaSlicer reported something about the print the model has not read
// yet: the person pressed a print-issue chip's button (the message carries a
// <print_issue> block), or a slice finished with warnings a plate's last
// slice did not report, whoever sliced it.
inline constexpr const char* kPrintIssues  = "print_issues";
// The setup is to be checked against a purpose recorded for this print:
// setup_record recorded one for the first time or changed it, the printer,
// filament, process or plate type changed since the model last checked them
// under a recorded one (by the person; the model's own printer_setup counts
// as checked and fires nothing), or the person restored an earlier version
// while one is recorded.
inline constexpr const char* kSetupChanged = "setup_changed";
} // namespace SkillTrigger

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

// Note is the host's own voice: a short factual line the shell posts when it
// changes something the person should see in the thread. It is not a turn --
// no reply follows it, it is never sent to the model, and it renders without a
// bubble. Added additively; the capability list advertises it.
enum class MessageRole : std::uint8_t { User, Assistant, Note };
enum class MessageState : std::uint8_t { Complete, Streaming, Failed, Stopped };

struct ConversationMessage
{
    std::string               id;                // host-assigned, stable across reloads
    MessageRole               role{MessageRole::User};
    MessageState              state{MessageState::Complete};
    std::string               text;
    std::string               client_message_id; // user messages: page-supplied dedup ID
    std::string               in_reply_to;       // assistant messages: the user message ID
    std::string               created_at;        // the host's timestamp when stored; empty before it is
    std::optional<AgentError> error;
    int                       attempt{1};
    std::vector<std::string>  attachment_ids;    // user messages: sent attachment IDs
    std::string               swatch;            // notes: a colour that leads the line, "#RRGGBB"
    // Notes: a file's load report (JSON), which the model reads; the page
    // draws it only as a card, when the Agent could not speak about it.
    std::string               file_report;
    bool                      file_report_card{false};
    // User messages sent from a print-issue bubble: the issue, its target and
    // the freshness of the evidence (JSON), which the model reads beside the
    // text and the page does not draw.
    std::string               issue_context;
};

} // namespace Slic3r::GUI::JusPrin::Agent
