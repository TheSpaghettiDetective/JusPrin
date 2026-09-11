// Typed view of the jusprin-agent-bridge protocol. The canonical shared
// source is resources/jusprin/agent/protocol.json; the constants here are
// derived from it at build time so the page and the native host cannot
// silently disagree about the protocol name or version.

import protocolJson from '@resources/jusprin/agent/protocol.json';

export const PROTOCOL_NAME: string = protocolJson.name;
export const PROTOCOL_VERSION: number = protocolJson.version;
export const PAGE_CAPABILITIES: string[] = protocolJson.capabilities;

export type PageMessageType =
  | 'hello'
  | 'state_request'
  | 'user_message'
  | 'stop_generation'
  | 'retry_message'
  | 'tool_decision'
  | 'tool_cancel'
  | 'create_conversation'
  | 'switch_conversation'
  | 'rename_conversation'
  | 'delete_conversation'
  | 'draft_update'
  | 'attach_file'
  | 'remove_attachment'
  | 'setup_check_key'
  | 'setup_cancel'
  | 'mcp_catalog'
  | 'mcp_preview'
  | 'mcp_connect'
  | 'reveal_path';

export type HostMessageType =
  | 'hello_ack'
  | 'hello_reject'
  | 'state'
  | 'conversations_updated'
  | 'context'
  | 'appearance'
  | 'agent_status'
  | 'message_added'
  | 'assistant_started'
  | 'assistant_delta'
  | 'assistant_completed'
  | 'assistant_failed'
  | 'assistant_stopped'
  | 'tool_activity'
  | 'bridge_error'
  | 'attachment_updated'
  | 'setup_status'
  | 'mcp_catalog'
  | 'mcp_preview'
  | 'mcp_status';

export interface Envelope<T = unknown> {
  protocol: string;
  version: number;
  id: string;
  type: string;
  correlationId?: string;
  sessionId?: string;
  revision?: number;
  payload: T;
}

// 'note' is the host's own voice: a short factual line the native shell posts
// when it changes something the reader should see in the thread (a spool swap,
// for example). It is never a turn -- no reply follows it, and the host never
// sends it to the model -- so it renders as a plain line, not a bubble.
export type MessageRole = 'user' | 'assistant' | 'note';
export type MessageStateName = 'complete' | 'streaming' | 'failed' | 'stopped';
export type AgentStatus = 'ready' | 'unavailable';
export type Appearance = 'light' | 'dark';

export interface AgentErrorInfo {
  code: string;
  message: string;
  retryable: boolean;
}

export interface WireMessage {
  id: string;
  role: MessageRole;
  state: MessageStateName;
  text: string;
  attempt: number;
  clientMessageId?: string;
  inReplyTo?: string;
  error?: AgentErrorInfo;
  attachments?: string[]; // sent attachment IDs, resolved against StatePayload.attachments
}

// How a file entered the composer.
export type AttachmentSource = 'picker' | 'drop' | 'clipboard' | 'project';

// The host's classification of an attachment. Model/project files go through
// Orca's native importer and reach the Agent as a native summary, never as
// decoded bytes; unknown/unsupported binaries are rejected visibly.
export type AttachmentKind =
  | 'text'
  | 'image'
  | 'svg'
  | 'pdf'
  | 'gcode'
  | 'model'
  | 'unsupported';

// Lifecycle of one attachment. 'staged' is in the composer but not yet sent;
// 'sent' belongs to a durable user message; 'error' failed to decode/store.
export type AttachmentState = 'staged' | 'sent' | 'error';

export interface AttachmentInfo {
  id: string;
  name: string; // original file name (display only)
  kind: AttachmentKind;
  mime: string;
  sizeBytes: number;
  source: AttachmentSource;
  state: AttachmentState;
  // Host-decoded preview, present where the kind supports it and the blob is
  // within the inline cap. Reconstructed from native state after a reload.
  previewText?: string; // text/gcode/svg source, truncated
  previewDataUrl?: string; // image thumbnails
  // A native, non-binary description of an imported model for Agent context.
  summary?: string;
  error?: { code: string; message: string };
}

// Where a credential check has got to. 'verified' means the provider
// answered and the Agent is being connected; 'warning' is set when the key
// worked but could not be written to the machine's credential store.
export type SetupPhase = 'idle' | 'checking' | 'verified' | 'error';

export interface SetupStatusPayload {
  phase: SetupPhase;
  provider?: string;
  elapsedMs?: number;
  error?: AgentErrorInfo;
  warning?: string;
}

export interface McpToolInfo {
  id: string;
  name: string;
  subtitle?: string;
  detected: boolean;
  cli: boolean;
  text: string;
  configPath?: string;
}

export interface McpCatalogPayload {
  tools: McpToolInfo[];
  liveUrl?: string;
  helperPresent: boolean;
  startupError?: string;
}

export interface McpPreviewPayload {
  toolId: string;
  path: string;
  // Absent when the file has no JusPrin entry yet. The host omits the field
  // rather than sending a serialised null, so "nothing there yet" stays
  // distinguishable from a real entry the user should read.
  previous?: string;
  next: string;
  root: string;
}

export interface McpStatusPayload {
  phase: 'idle' | 'writing' | 'saved' | 'error';
  toolId?: string;
  backup?: string;
  diagnostic?: string;
  error?: AgentErrorInfo;
}

// Time and material for one plate's current slice. Null whenever that plate
// holds no valid slice result: OrcaSlicer has no background slicing, so there
// is no honest estimate before Slice and the card omits the row rather than
// showing a zero. materialCost is null on its own whenever the filament
// profile carries no price, which is the default.
export interface SliceEstimateInfo {
  printTimeSeconds: number;
  materialGrams: number;
  materialCost: number | null;
}

// One process setting whose value in force differs from the preset it came
// from. The length of the list is the setup card's "N changes"; the entries
// are what the card shows when it is opened.
export interface PresetDeltaInfo {
  key: string;
  label: string;
  preset: string;
  value: string;
  // Who put this value in force. 'user' unless the host can prove the agent
  // wrote this exact value, so a setting the agent once set and you have since
  // edited by hand reads as yours again.
  origin: 'agent' | 'user';
}

// What the estimate is worth right now. The number is kept in all three: a
// slice in flight or invalidated does not make the last honest figure
// worthless, and blanking it kills the comparison the reader is making.
export type EstimateStatus = 'current' | 'recomputing' | 'stale';

export interface WorkspaceContext {
  sessionId: string;
  revision: number;
  projectName: string;
  projectDirty: boolean;
  // `process` is the preset the setting deltas are measured against, so it is
  // the name the setup card shows; `preset` is the machine and `filament` the
  // spool. Empty for a non-FFF printer, which has no process preset.
  printer: { preset: string; filament: string; process: string };
  plates: {
    id: string;
    name: string;
    active: boolean;
    sliced: boolean;
    estimate: SliceEstimateInfo | null;
    estimateStatus: EstimateStatus;
    // Which of the user's actions invalidated the slice, when it is known.
    // Empty otherwise: the card says nothing rather than attributing the loss
    // to something the reader did not do.
    invalidatedBy: string;
    objects: { id: string; name: string; instances: number; selected: boolean }[];
  }[];
  selection: { status: 'none' | 'objects' | 'unsupported'; objectIds: string[] };
  history: { canUndo: boolean; canRedo: boolean };
  presetDeltas: PresetDeltaInfo[];
  // ISO 4217 code from the machine's regional settings, empty when the OS does
  // not say. OrcaSlicer itself has no currency concept, so this is the only
  // thing that says what a cost is denominated in.
  currency: string;
  // The agent's restatement of what this chat asked the setup to be, in the
  // user's words. Empty until a change is applied: talking does not write it.
  setupIntent: string;
}

// Lifecycle of one native tool action. Terminal states are 'succeeded',
// 'failed', 'cancelled', and 'rejected'; a stale proposal arrives as
// state 'failed' with error code 'stale_revision'.
export type ToolStateName =
  | 'pending'
  | 'approved'
  | 'running'
  | 'succeeded'
  | 'failed'
  | 'cancelled'
  | 'rejected';

export type ActionClassName = 'read_only' | 'mutation' | 'destructive';

export interface ToolActivityInfo {
  actionId: string;
  correlationId: string; // assistant message or external adapter request
  source?: 'agent' | 'mcp'; // absent in older native builds: in-app Agent
  server: string;
  tool: string;
  title: string;
  arguments: Record<string, unknown>;
  actionClass: ActionClassName;
  requiresApproval: boolean;
  sessionId: string;
  expectedRevision: number;
  state: ToolStateName;
  progress: { current: number; total: number };
  result?: Record<string, unknown>;
  error?: { code: string; message: string };
}

export interface ConversationInfo {
  id: string;
  title: string;
  createdAt: string;
  updatedAt?: string;
  preview?: string;
}

export interface SliceStatisticsInfo {
  printTimeSeconds: number;
  filamentMm: number;
  materialGrams: number;
  materialCost: number;
  layerCount: number;
}

export interface BuildInfo {
  id: string;
  seq: number;
  createdAt: string;
  projectId: string;
  conversationId: string;
  afterMessageId: string;
  plateIndex: number;
  plateName: string;
  printer: string;
  material: string;
  manufacturingInputHash: string;
  outputHash: string;
  slicerVersion: string;
  configurationProvenance: string;
  statistics: SliceStatisticsInfo;
  warnings: string[];
  stale: boolean; // derived by the host from the current same-plate input hash
}

export interface ExportedCopyInfo {
  id: string;
  seq: number;
  createdAt: string;
  buildId: string;
  conversationId: string;
  afterMessageId: string;
  destination: string;
  expectedOutputHash: string;
  observedOutputHash: string;
  verified: boolean; // derived by checksum comparison
  modified: boolean; // derived by checksum comparison
}

export interface PhysicalPrintInfo {
  id: string;
  seq: number;
  startedAt: string;
  endedAt: string;
  outcome: 'completed' | 'failed' | 'cancelled';
  failure: string;
  buildId: string;
  projectId: string;
  conversationId: string;
  afterMessageId: string;
  plateIndex: number;
  plateName: string;
  printer: string;
  material: string;
  manufacturingInputHash: string;
  outputHash: string;
  gcodeHash: string;
  statistics: SliceStatisticsInfo;
}

export interface StatePayload {
  conversationBusy?: boolean;
  agent: { status: AgentStatus };
  appearance: Appearance;
  conversations: ConversationInfo[];
  activeConversationId: string;
  conversation: WireMessage[]; // messages of the active conversation
  streamingMessageId: string | null;
  toolActivities: ToolActivityInfo[];
  builds: BuildInfo[];
  exportedCopies: ExportedCopyInfo[];
  physicalPrints: PhysicalPrintInfo[];
  draft: string;
  attachments?: AttachmentInfo[]; // staged (composer) and sent (history) attachments
  context: WorkspaceContext;
}

export function isEnvelope(value: unknown): value is Envelope {
  if (typeof value !== 'object' || value === null) return false;
  const env = value as Record<string, unknown>;
  return (
    env.protocol === PROTOCOL_NAME &&
    typeof env.version === 'number' &&
    typeof env.id === 'string' &&
    typeof env.type === 'string'
  );
}
