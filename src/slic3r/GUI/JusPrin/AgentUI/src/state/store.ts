// Conversation store: a pure reducer that applies validated host envelopes to
// page state. The host is authoritative — the reducer never invents messages,
// it only mirrors what the host reports, so a full `state` payload can always
// replace everything after a reload or resync.

import {
  AgentErrorInfo,
  AgentStatus,
  Appearance,
  AttachmentInfo,
  BuildInfo,
  ConversationInfo,
  Envelope,
  ExportedCopyInfo,
  PhysicalPrintInfo,
  RevisionInfo,
  SetupStatusPayload,
  StatePayload,
  ToolActivityInfo,
  WireMessage,
  WorkspaceContext,
} from '../bridge/protocol';
import { ConnectionState } from '../bridge/client';

export interface Message extends WireMessage {
  lastSeq: number;
}

export interface AgentUiState {
  connection: ConnectionState;
  connectionDetail?: string;
  agentStatus: AgentStatus;
  appearance: Appearance;
  conversations: ConversationInfo[];
  activeConversationId: string;
  messages: Message[];
  streamingMessageId: string | null;
  conversationBusy: boolean;
  toolActivities: ToolActivityInfo[];
  revisions: RevisionInfo[];
  builds: BuildInfo[];
  exportedCopies: ExportedCopyInfo[];
  physicalPrints: PhysicalPrintInfo[];
  draft: string;
  // Staged (composer) and sent (history) attachments, keyed by id in the UI.
  attachments: AttachmentInfo[];
  context: WorkspaceContext | null;
  // Progress of a credential check. The host owns it; the page only mirrors
  // it, so a reload cannot leave a check looking live when it is not.
  setup: SetupStatusPayload;
  // Set when a delta arrived out of order; the app answers by requesting a
  // full state resync from the host.
  needsResync: boolean;
  diagnostics: string[];
}

export const initialState: AgentUiState = {
  connection: 'connecting',
  agentStatus: 'ready',
  appearance: 'light',
  conversations: [],
  activeConversationId: '',
  messages: [],
  streamingMessageId: null,
  conversationBusy: false,
  toolActivities: [],
  revisions: [],
  builds: [],
  exportedCopies: [],
  physicalPrints: [],
  draft: '',
  attachments: [],
  context: null,
  setup: { phase: 'idle' },
  needsResync: false,
  diagnostics: [],
};

export type Action =
  | { kind: 'connection'; state: ConnectionState; detail?: string }
  | { kind: 'host'; envelope: Envelope }
  | { kind: 'resync-requested' };

function fromWire(message: WireMessage): Message {
  return { ...message, lastSeq: -1 };
}

function upsert(messages: Message[], incoming: Message): Message[] {
  const index = messages.findIndex((m) => m.id === incoming.id);
  if (index < 0) return [...messages, incoming];
  const next = messages.slice();
  next[index] = incoming;
  return next;
}

function withDiagnostic(state: AgentUiState, line: string): AgentUiState {
  return { ...state, diagnostics: [...state.diagnostics.slice(-19), line] };
}

export function reducer(state: AgentUiState, action: Action): AgentUiState {
  switch (action.kind) {
    case 'connection':
      return { ...state, connection: action.state, connectionDetail: action.detail };
    case 'resync-requested':
      return { ...state, needsResync: false };
    case 'host':
      return applyHostEnvelope(state, action.envelope);
  }
}

function applyHostEnvelope(state: AgentUiState, envelope: Envelope): AgentUiState {
  const payload = envelope.payload as Record<string, unknown>;
  switch (envelope.type) {
    case 'hello_ack': {
      const agent = payload.agent as { status: AgentStatus } | undefined;
      const appearance = payload.appearance as Appearance | undefined;
      return {
        ...state,
        agentStatus: agent?.status ?? state.agentStatus,
        appearance: appearance ?? state.appearance,
      };
    }
    case 'hello_reject':
      return state; // connection state handled by the client
    case 'state': {
      const full = envelope.payload as StatePayload;
      return {
        ...state,
        agentStatus: full.agent.status,
        appearance: full.appearance,
        conversations: full.conversations ?? [],
        activeConversationId: full.activeConversationId ?? '',
        messages: full.conversation.map(fromWire),
        streamingMessageId: full.streamingMessageId,
        conversationBusy: full.conversationBusy ?? full.streamingMessageId !== null,
        toolActivities: full.toolActivities ?? [],
        revisions: full.revisions ?? [],
        builds: full.builds ?? [],
        exportedCopies: full.exportedCopies ?? [],
        physicalPrints: full.physicalPrints ?? [],
        draft: full.draft ?? '',
        attachments: full.attachments ?? [],
        context: full.context,
        // A full state answers a fresh handshake; any check that was in
        // flight before belonged to the previous page.
        setup: { phase: 'idle' },
        needsResync: false,
      };
    }
    case 'context':
      return { ...state, context: payload.context as WorkspaceContext };
    case 'conversations_updated':
      return { ...state, conversations: payload.conversations as ConversationInfo[], conversationBusy: payload.busy as boolean };
    case 'appearance':
      return { ...state, appearance: payload.appearance as Appearance };
    case 'agent_status':
      return { ...state, agentStatus: payload.status as AgentStatus };
    case 'setup_status':
      return { ...state, setup: envelope.payload as SetupStatusPayload };
    case 'message_added': {
      const message = fromWire(payload.message as WireMessage);
      return { ...state, messages: upsert(state.messages, message) };
    }
    case 'assistant_started': {
      const id = payload.messageId as string;
      const existing = state.messages.find((m) => m.id === id);
      const started: Message = existing
        ? { ...existing, state: 'streaming', text: '', error: undefined, attempt: (payload.attempt as number) ?? existing.attempt, lastSeq: -1 }
        : {
            id,
            role: 'assistant',
            state: 'streaming',
            text: '',
            attempt: (payload.attempt as number) ?? 1,
            inReplyTo: payload.inReplyTo as string | undefined,
            lastSeq: -1,
          };
      return { ...state, messages: upsert(state.messages, started), streamingMessageId: id };
    }
    case 'assistant_delta': {
      const id = payload.messageId as string;
      const seq = payload.seq as number;
      const text = payload.text as string;
      const message = state.messages.find((m) => m.id === id);
      if (!message) return { ...state, needsResync: true };
      if (seq <= message.lastSeq) return state; // duplicate delivery
      if (seq !== message.lastSeq + 1) return { ...state, needsResync: true };
      const updated: Message = { ...message, text: message.text + text, lastSeq: seq };
      return { ...state, messages: upsert(state.messages, updated) };
    }
    case 'assistant_completed': {
      const id = payload.messageId as string;
      const message = state.messages.find((m) => m.id === id);
      if (!message) return { ...state, needsResync: true, streamingMessageId: null };
      return {
        ...state,
        messages: upsert(state.messages, { ...message, state: 'complete' }),
        streamingMessageId: state.streamingMessageId === id ? null : state.streamingMessageId,
      };
    }
    case 'assistant_failed': {
      const id = payload.messageId as string;
      const error = payload.error as AgentErrorInfo;
      const message = state.messages.find((m) => m.id === id);
      if (!message) return { ...state, needsResync: true, streamingMessageId: null };
      return {
        ...state,
        messages: upsert(state.messages, { ...message, state: 'failed', error }),
        streamingMessageId: state.streamingMessageId === id ? null : state.streamingMessageId,
      };
    }
    case 'assistant_stopped': {
      const id = payload.messageId as string;
      const message = state.messages.find((m) => m.id === id);
      if (!message) return { ...state, needsResync: true, streamingMessageId: null };
      return {
        ...state,
        messages: upsert(state.messages, { ...message, state: 'stopped' }),
        streamingMessageId: state.streamingMessageId === id ? null : state.streamingMessageId,
      };
    }
    case 'revision_added': {
      const revision = payload.revision as RevisionInfo;
      const cleared = state.revisions.map((r) => ({ ...r, current: false }));
      const index = cleared.findIndex((r) => r.id === revision.id);
      const revisions = index < 0 ? [...cleared, revision] : cleared.map((r, i) => (i === index ? revision : r));
      return { ...state, revisions };
    }
    case 'tool_activity': {
      const activity = payload.activity as ToolActivityInfo;
      const index = state.toolActivities.findIndex((a) => a.actionId === activity.actionId);
      const toolActivities =
        index < 0
          ? [...state.toolActivities, activity]
          : state.toolActivities.map((a, i) => (i === index ? activity : a));
      return { ...state, toolActivities };
    }
    case 'attachment_updated': {
      const attachment = payload.attachment as AttachmentInfo;
      const index = state.attachments.findIndex((a) => a.id === attachment.id);
      const attachments =
        index < 0
          ? [...state.attachments, attachment]
          : state.attachments.map((a, i) => (i === index ? attachment : a));
      return { ...state, attachments };
    }
    case 'bridge_error': {
      const code = payload.code as string;
      const message = payload.message as string;
      return withDiagnostic(state, `bridge_error ${code}: ${message}`);
    }
    default:
      return withDiagnostic(state, `unknown host message type: ${envelope.type}`);
  }
}
