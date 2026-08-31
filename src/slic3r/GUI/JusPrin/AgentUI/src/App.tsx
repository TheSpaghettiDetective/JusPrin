import { useEffect, useMemo, useReducer, useRef } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import { Envelope } from './bridge/protocol';
import { AgentUiState, initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { ContextSummary } from './components/ContextSummary';
import { ConversationBar } from './components/ConversationBar';
import { MessageList } from './components/MessageList';
import { Composer } from './components/Composer';
import { AgentUnavailableNotice, BridgeErrorPane, ConnectingPane } from './components/Panels';

let clientMessageCounter = 0;
function nextClientMessageId(): string {
  clientMessageCounter += 1;
  return `c-${clientMessageCounter}-${Math.random().toString(36).slice(2, 10)}`;
}

declare global {
  interface Window {
    // Deterministic hooks for the native shell harness; harmless in
    // production and never a second control path for users.
    __jusprinTest?: {
      send(text: string): void;
      decide(actionId: string, decision: 'approve' | 'reject'): void;
      cancelTool(actionId: string): void;
      createConversation(): void;
      switchConversation(conversationId: string): void;
      revert(revisionId: string): void;
      setDraft(text: string): void;
      state(): AgentUiState;
    };
  }
}

export interface AppProps {
  getTransport: () => Transport | null;
  handshakeTimeoutMs?: number;
  transportRetryMs?: number;
  transportRetryLimit?: number;
  draftDebounceMs?: number;
}

const errorTitles: Partial<Record<ConnectionState, string>> = {
  'no-transport': 'The Agent panel has no connection to JusPrin',
  timeout: 'The Agent panel could not reach JusPrin',
  incompatible: 'This Agent panel does not match this JusPrin build',
};

export function App({ getTransport, handshakeTimeoutMs, transportRetryMs, transportRetryLimit, draftDebounceMs }: AppProps) {
  const [state, dispatch] = useReducer(reducer, initialState);
  const stateRef = useRef<AgentUiState>(state);
  stateRef.current = state;

  const client = useMemo(() => {
    const created: BridgeClient = new BridgeClient({
      getTransport,
      handshakeTimeoutMs,
      transportRetryMs,
      transportRetryLimit,
      onConnectionChange: (connection, detail) => dispatch({ kind: 'connection', state: connection, detail }),
      onEnvelope: (envelope: Envelope) => {
        if (envelope.type === 'bridge_error') {
          const payload = envelope.payload as { code?: string };
          // The host restarts after a reload on its side; if it forgot us,
          // simply shake hands again.
          if (payload.code === 'handshake_required') created.retry();
        }
        dispatch({ kind: 'host', envelope });
      },
    });
    return created;
    // The transport is fixed for the lifetime of the page.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    client.start();
  }, [client]);

  useEffect(() => {
    applyAppearance(state.appearance);
  }, [state.appearance]);

  useEffect(() => {
    if (state.needsResync) {
      client.send('state_request', {});
      dispatch({ kind: 'resync-requested' });
    }
  }, [state.needsResync, client]);

  const sendMessage = (text: string) => {
    client.send('user_message', { clientMessageId: nextClientMessageId(), text });
  };

  const sendToolDecision = (actionId: string, decision: 'approve' | 'reject') => {
    client.send('tool_decision', { actionId, decision });
  };

  const sendToolCancel = (actionId: string) => {
    client.send('tool_cancel', { actionId });
  };

  const sendRevert = (revisionId: string) => {
    client.send('revert_to_revision', { revisionId });
  };

  useEffect(() => {
    window.__jusprinTest = {
      send: sendMessage,
      decide: sendToolDecision,
      cancelTool: sendToolCancel,
      createConversation: () => client.send('create_conversation', {}),
      switchConversation: (conversationId: string) => client.send('switch_conversation', { conversationId }),
      revert: sendRevert,
      setDraft: (text: string) => client.send('draft_update', { text }),
      state: () => stateRef.current,
    };
    return () => {
      delete window.__jusprinTest;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  if (state.connection === 'connecting') return <div className="app"><ConnectingPane /></div>;

  if (state.connection !== 'connected') {
    return (
      <div className="app">
        <BridgeErrorPane
          title={errorTitles[state.connection] ?? 'The Agent panel could not connect'}
          detail={state.connectionDetail}
          diagnostics={state.diagnostics}
          onRetry={() => client.retry()}
        />
      </div>
    );
  }

  const unavailable = state.agentStatus === 'unavailable';
  const streaming = state.streamingMessageId !== null;
  return (
    <div className="app">
      <ContextSummary context={state.context} />
      <ConversationBar
        conversations={state.conversations}
        activeConversationId={state.activeConversationId}
        busy={streaming}
        onSwitch={(conversationId) => client.send('switch_conversation', { conversationId })}
        onCreate={() => client.send('create_conversation', {})}
      />
      {unavailable && <AgentUnavailableNotice />}
      <MessageList
        messages={state.messages}
        streamingMessageId={state.streamingMessageId}
        toolActivities={state.toolActivities}
        revisions={state.revisions.filter((revision) => revision.conversationId === state.activeConversationId)}
        onRetry={(messageId) => client.send('retry_message', { messageId })}
        onToolDecision={sendToolDecision}
        onToolCancel={sendToolCancel}
        onRevert={sendRevert}
      />
      <Composer
        disabled={unavailable}
        disabledReason={unavailable ? 'The Agent is not available' : undefined}
        streaming={streaming}
        initialText={state.draft}
        onSend={sendMessage}
        onStop={() => {
          if (state.streamingMessageId) client.send('stop_generation', { messageId: state.streamingMessageId });
        }}
        onDraftChange={(text) => client.send('draft_update', { text })}
        draftDebounceMs={draftDebounceMs}
      />
    </div>
  );
}
