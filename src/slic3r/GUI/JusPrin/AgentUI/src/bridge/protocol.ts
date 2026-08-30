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
  | 'retry_message';

export type HostMessageType =
  | 'hello_ack'
  | 'hello_reject'
  | 'state'
  | 'context'
  | 'appearance'
  | 'agent_status'
  | 'message_added'
  | 'assistant_started'
  | 'assistant_delta'
  | 'assistant_completed'
  | 'assistant_failed'
  | 'assistant_stopped'
  | 'bridge_error';

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

export type MessageRole = 'user' | 'assistant';
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
}

export interface WorkspaceContext {
  sessionId: string;
  revision: number;
  projectName: string;
  projectDirty: boolean;
  printer: { preset: string; filament: string };
  plates: {
    id: string;
    name: string;
    active: boolean;
    sliced: boolean;
    objects: { id: string; name: string; instances: number; selected: boolean }[];
  }[];
  selection: { status: 'none' | 'objects' | 'unsupported'; objectIds: string[] };
  history: { canUndo: boolean; canRedo: boolean };
}

export interface StatePayload {
  agent: { status: AgentStatus };
  appearance: Appearance;
  conversation: WireMessage[];
  streamingMessageId: string | null;
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
