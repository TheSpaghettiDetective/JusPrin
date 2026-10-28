// Conversation store: a pure reducer that applies validated host envelopes to
// page state. The host is authoritative — the reducer never invents messages,
// it only mirrors what the host reports, so a full `state` payload can always
// replace everything after a reload or resync.

import {
  AccountStatusPayload,
  AgentErrorInfo,
  AgentStatus,
  Appearance,
  AttachmentInfo,
  ChatModelChoice,
  ModelCatalogPayload,
  ChangeInfo,
  WorkInfo,
  RestorePointInfo,
  ConversationInfo,
  Envelope,
  GcodeMarkerInfo,
  SavedKeyInfo,
  SetupKeysPayload,
  SetupStatusPayload,
  ConfirmPayload,
  StatePayload,
  ToolActivityInfo,
  WelcomeBackPayload,
  WireMessage,
  WorkspaceContext,
  McpCatalogPayload,
  McpPreviewPayload,
  McpStatusPayload,
  FilamentSessionPayload,
  PrinterSessionPayload,
  PreferencesPayload,
  SettingsCatalogPayload,
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
  // What the host said it can do in hello_ack, for the additive features a
  // page uses only when its host has them.
  hostCapabilities: string[];
  // Each configured provider's models, and the active chat's choice; both
  // absent from hosts without the model_choice capability.
  modelCatalog?: ModelCatalogPayload;
  chatModel?: ChatModelChoice;
  conversations: ConversationInfo[];
  activeConversationId: string;
  viewedConversationId: string;
  docRevision: number;
  projectChatBlocked: boolean;
  chatResume: NonNullable<StatePayload['chatResume']>;
  messages: Message[];
  streamingMessageId: string | null;
  conversationBusy: boolean;
  toolActivities: ToolActivityInfo[];
  gcodeMarkers: GcodeMarkerInfo[];
  changes: ChangeInfo[];
  work: WorkInfo[];
  restorePoints: RestorePointInfo[];
  draft: string;
  // Staged (composer) and sent (history) attachments, keyed by id in the UI.
  attachments: AttachmentInfo[];
  context: WorkspaceContext | null;
  // What the printer panel shows above and inside its thread; null in the
  // project's own conversation.
  session: PrinterSessionPayload | null;
  // The filament chat's, in its place.
  filamentSession: FilamentSessionPayload | null;
  // The Preferences window's settings, in its place.
  preferences: PreferencesPayload | null;
  // The welcome-back window's list of what came from JusPrin v1.
  welcomeBack: WelcomeBackPayload | null;
  navigation: { focused: boolean; returnLabel?: string };
  // The All settings panel's table, read from hosts with all_settings; the
  // revision says which project state it describes.
  settingsCatalog: { sessionId: string; revision: number; catalog: SettingsCatalogPayload } | null;
  // Progress of a credential check. The host owns it; the page only mirrors
  // it, so a reload cannot leave a check looking live when it is not.
  setup: SetupStatusPayload;
  // Providers with a stored key, by hint: never the key. The host sends it
  // after each handshake and whenever a key is stored, removed or refused.
  savedKeys: SavedKeyInfo[];
  // Progress of a JusPrin sign-in, mirrored from the host like `setup`.
  account: AccountStatusPayload;
  mcpCatalog: McpCatalogPayload | null;
  mcpPreview: McpPreviewPayload | null;
  mcpStatus: McpStatusPayload;
  // The host's open question, until the person answers it; a newer one
  // replaces it.
  confirmation: ConfirmPayload | null;
  // The chat item the host last asked to bring into view (the Print
  // history's Open in chat), and a count that moves with each ask, so the
  // same item asked twice is scrolled to twice.
  reveal: { itemId: string; count: number } | null;
  // Set when a delta arrived out of order; the app answers by requesting a
  // full state resync from the host.
  needsResync: boolean;
  diagnostics: string[];
}

export const initialState: AgentUiState = {
  connection: 'connecting',
  agentStatus: 'ready',
  hostCapabilities: [],
  appearance: 'light',
  conversations: [],
  activeConversationId: '',
  viewedConversationId: '',
  docRevision: 0,
  projectChatBlocked: false,
  chatResume: { status: 'active' },
  messages: [],
  streamingMessageId: null,
  conversationBusy: false,
  toolActivities: [],
  gcodeMarkers: [],
  changes: [],
  work: [],
  restorePoints: [],
  draft: '',
  attachments: [],
  context: null,
  setup: { phase: 'idle' },
  savedKeys: [],
  account: { phase: 'idle' },
  mcpCatalog: null,
  mcpPreview: null,
  mcpStatus: { phase: 'idle' },
  session: null,
  filamentSession: null,
  preferences: null,
  welcomeBack: null,
  navigation: { focused: false },
  settingsCatalog: null,
  confirmation: null,
  reveal: null,
  needsResync: false,
  diagnostics: [],
};

export type Action =
  | { kind: 'connection'; state: ConnectionState; detail?: string }
  | { kind: 'host'; envelope: Envelope }
  | { kind: 'resync-requested' }
  | { kind: 'confirmation-answered' };

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
    case 'confirmation-answered':
      return { ...state, confirmation: null };
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
      const capabilities = payload.capabilities as string[] | undefined;
      return {
        ...state,
        agentStatus: agent?.status ?? state.agentStatus,
        appearance: appearance ?? state.appearance,
        hostCapabilities: capabilities ?? state.hostCapabilities,
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
        chatModel: full.chatModel ?? state.chatModel,
        conversations: full.conversations ?? [],
        activeConversationId: full.activeConversationId ?? '',
        viewedConversationId: full.viewedConversationId ?? full.activeConversationId ?? '',
        docRevision: full.docRevision ?? 0,
        projectChatBlocked: full.projectChatBlocked ?? false,
        chatResume: full.chatResume ?? { status: 'active' },
        messages: full.conversation.map(fromWire),
        streamingMessageId: full.streamingMessageId,
        conversationBusy: full.conversationBusy ?? full.streamingMessageId !== null,
        toolActivities: full.toolActivities ?? [],
        gcodeMarkers: full.gcodeMarkers ?? [],
        changes: full.changes ?? [],
        work: full.work ?? [],
        restorePoints: full.restorePoints ?? [],
        draft: full.draft ?? '',
        attachments: full.attachments ?? [],
        context: full.context,
        session: full.session && !('kind' in full.session) ? full.session : null,
        filamentSession: full.session && 'kind' in full.session && full.session.kind === 'filament' ? full.session : null,
        preferences: full.session && 'kind' in full.session && full.session.kind === 'preferences' ? full.session : null,
        welcomeBack: full.session && 'kind' in full.session && full.session.kind === 'welcome_back' ? full.session : null,
        navigation: full.navigation ?? { focused: false },
        settingsCatalog: full.context?.sessionId === state.context?.sessionId ? state.settingsCatalog : null,
        // A full state answers a fresh handshake; any check that was in
        // flight before belonged to the previous page.
        setup: { phase: 'idle' },
        mcpStatus: { phase: 'idle' },
        needsResync: false,
      };
    }
    case 'context': {
      const context = payload.context as WorkspaceContext;
      return { ...state, context,
        settingsCatalog: context.sessionId === state.context?.sessionId ? state.settingsCatalog : null };
    }
    case 'settings_catalog':
      // The host stamps every envelope with the project it describes; a
      // page fixture that leaves it out means the project on show.
      return { ...state, settingsCatalog: { sessionId: envelope.sessionId ?? state.context?.sessionId ?? '', revision: envelope.revision ?? 0,
        catalog: envelope.payload as SettingsCatalogPayload } };
    case 'preferences':
      return { ...state, preferences: envelope.payload as PreferencesPayload };
    case 'printer_session':
      return { ...state, session: envelope.payload as PrinterSessionPayload };
    case 'conversations_updated':
      return { ...state, conversations: payload.conversations as ConversationInfo[], conversationBusy: payload.busy as boolean };
    case 'appearance':
      return { ...state, appearance: payload.appearance as Appearance };
    case 'agent_status':
      return { ...state, agentStatus: payload.status as AgentStatus };
    case 'setup_status':
      return { ...state, setup: envelope.payload as SetupStatusPayload };
    case 'confirm':
      return { ...state, confirmation: envelope.payload as ConfirmPayload };
    case 'reveal_item':
      return { ...state, reveal: { itemId: payload.itemId as string, count: (state.reveal?.count ?? 0) + 1 } };
    case 'setup_keys':
      return { ...state, savedKeys: (envelope.payload as SetupKeysPayload).keys };
    case 'account_status':
      return { ...state, account: envelope.payload as AccountStatusPayload };
    case 'model_catalog':
      return { ...state, modelCatalog: envelope.payload as ModelCatalogPayload };
    case 'chat_model': {
      const update = envelope.payload as { conversationId: string; chatModel: ChatModelChoice };
      return update.conversationId === state.activeConversationId ? { ...state, chatModel: update.chatModel } : state;
    }
    case 'mcp_catalog':
      return { ...state, mcpCatalog: envelope.payload as McpCatalogPayload };
    case 'mcp_preview':
      return { ...state, mcpPreview: envelope.payload as McpPreviewPayload };
    case 'mcp_status':
      return { ...state, mcpStatus: envelope.payload as McpStatusPayload };
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
    case 'work_updated': {
      // One entry per piece of work: it is added when the work starts and
      // replaced in place as it moves on and when it ends.
      const work = payload.work as WorkInfo;
      const known = state.work.some((entry) => entry.id === work.id);
      return { ...state, work: known ? state.work.map((entry) => (entry.id === work.id ? work : entry)) : [...state.work, work] };
    }
    case 'change_added': {
      const change = payload.change as ChangeInfo;
      // seq is unique across the document; a redelivery changes nothing.
      if (state.changes.some((known) => known.seq === change.seq)) return state;
      return { ...state, changes: [...state.changes, change] };
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
