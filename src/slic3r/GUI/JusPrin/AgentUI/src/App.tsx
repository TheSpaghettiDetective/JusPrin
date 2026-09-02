import { useEffect, useMemo, useReducer, useRef, useState } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import { AttachmentSource, Envelope } from './bridge/protocol';
import { AgentUiState, initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { ContextSummary } from './components/ContextSummary';
import { ConversationBar } from './components/ConversationBar';
import { MessageList } from './components/MessageList';
import { Composer } from './components/Composer';
import {
  AgentNotConfiguredHeader,
  AgentNotConfiguredPane,
  AgentUnavailableNotice,
  BridgeErrorPane,
  ConnectingPane,
} from './components/Panels';
import { ConnectedBanner, DEFAULT_PROVIDER, SetupApiKey, SetupChooser } from './components/Setup';

let clientMessageCounter = 0;
function nextClientMessageId(): string {
  clientMessageCounter += 1;
  return `c-${clientMessageCounter}-${Math.random().toString(36).slice(2, 10)}`;
}

let clientAttachmentCounter = 0;
function nextClientAttachmentId(): string {
  clientAttachmentCounter += 1;
  return `ca-${clientAttachmentCounter}-${Math.random().toString(36).slice(2, 10)}`;
}

function readAsDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : '');
    reader.onerror = () => reject(reader.error ?? new Error('read failed'));
    reader.readAsDataURL(file);
  });
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
      openSetup(): void;
      checkKey(provider: string, apiKey: string): void;
      attach(name: string, dataBase64: string, mime?: string): void;
      removeAttachment(attachmentId: string): void;
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

  // Which setup screen the dock is showing. This is page-local on purpose:
  // the host cares which credentials it was asked to check, not which panel
  // is on screen, so navigating setup costs no bridge traffic.
  const [setupScreen, setSetupScreen] = useState<'offer' | 'chooser' | 'apiKey'>('offer');
  // The one-time confirmation after setup succeeds. The page knows what it
  // just submitted, so this needs nothing from the host.
  const [connected, setConnected] = useState<{ provider: string; warning?: string } | null>(null);

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

  // The composer shows staged attachments plus any that failed to attach, so a
  // rejection stays visible until the user dismisses it; only staged ones are
  // actually sent.
  const stagedAttachments = state.attachments.filter(
    (attachment) => attachment.state === 'staged' || attachment.state === 'error',
  );

  const sendMessage = (text: string) => {
    const attachmentIds = stagedAttachments.filter((a) => a.state === 'staged').map((a) => a.id);
    client.send('user_message', { clientMessageId: nextClientMessageId(), text, attachmentIds });
  };

  const attachFiles = (files: File[], source: AttachmentSource) => {
    for (const file of files) {
      readAsDataUrl(file)
        .then((dataUrl) => {
          client.send('attach_file', {
            clientAttachmentId: nextClientAttachmentId(),
            name: file.name,
            mime: file.type,
            source,
            dataBase64: dataUrl,
          });
        })
        .catch(() => {
          // A file the browser could not read never becomes a staged
          // attachment; the host only sees files it can decode.
        });
    }
  };

  const removeAttachment = (attachmentId: string) => {
    client.send('remove_attachment', { attachmentId });
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

  const checkKey = (provider: string, apiKey: string) => {
    client.send('setup_check_key', { provider, apiKey });
  };

  const cancelCheck = () => {
    client.send('setup_cancel', {});
  };

  useEffect(() => {
    if (state.setup.phase !== 'verified') return;
    // The host installs the verified service right after saying so, so the
    // dock is about to become a working chat; carry the confirmation across.
    // The key screen stays up until that happens, so the round-trip the check
    // measured is actually readable rather than flashing past.
    setConnected({ provider: state.setup.provider ?? DEFAULT_PROVIDER, warning: state.setup.warning });
  }, [state.setup.phase, state.setup.provider, state.setup.warning]);

  useEffect(() => {
    // A connected Agent replaces the setup surface entirely; if the dock ever
    // returns to being unconfigured it starts from the offer, not mid-flow.
    if (state.agentStatus === 'ready') setSetupScreen('offer');
  }, [state.agentStatus]);

  useEffect(() => {
    window.__jusprinTest = {
      send: sendMessage,
      decide: sendToolDecision,
      cancelTool: sendToolCancel,
      createConversation: () => client.send('create_conversation', {}),
      switchConversation: (conversationId: string) => client.send('switch_conversation', { conversationId }),
      revert: sendRevert,
      setDraft: (text: string) => client.send('draft_update', { text }),
      openSetup: () => setSetupScreen('chooser'),
      checkKey,
      attach: (name: string, dataBase64: string, mime?: string) =>
        client.send('attach_file', {
          clientAttachmentId: nextClientAttachmentId(),
          name,
          mime: mime ?? '',
          source: 'picker',
          dataBase64,
        }),
      removeAttachment,
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
  // Nothing has been delegated yet, so the dock's whole surface becomes the
  // one offer. A conversation carried in from a previously configured session
  // keeps its history and gets the banner above it instead.
  const notConfigured = unavailable && state.messages.length === 0;
  const streaming = state.streamingMessageId !== null;

  // The dock body is one of three things: the conversation, the offer, or a
  // setup screen. Setup replaces the body rather than covering it, so backing
  // out returns to exactly what was there before.
  const body = () => {
    if (!notConfigured)
      return (
        <MessageList
          messages={state.messages}
          attachments={state.attachments}
          streamingMessageId={state.streamingMessageId}
          toolActivities={state.toolActivities}
          revisions={state.revisions.filter((revision) => revision.conversationId === state.activeConversationId)}
          builds={state.builds}
          exportedCopies={state.exportedCopies}
          physicalPrints={state.physicalPrints}
          onRetry={(messageId) => client.send('retry_message', { messageId })}
          onToolDecision={sendToolDecision}
          onToolCancel={sendToolCancel}
          onRevert={sendRevert}
        />
      );
    if (setupScreen === 'chooser')
      return <SetupChooser onUseApiKey={() => setSetupScreen('apiKey')} onDismiss={() => setSetupScreen('offer')} />;
    if (setupScreen === 'apiKey')
      return (
        <SetupApiKey
          setup={state.setup}
          onCheck={checkKey}
          onCancel={cancelCheck}
          onBack={() => {
            cancelCheck();
            setSetupScreen('chooser');
          }}
        />
      );
    return <AgentNotConfiguredPane onSetUp={() => setSetupScreen('chooser')} />;
  };

  return (
    <div className="app">
      {notConfigured ? (
        <AgentNotConfiguredHeader />
      ) : (
        <>
          <ContextSummary context={state.context} />
          <ConversationBar
            conversations={state.conversations}
            activeConversationId={state.activeConversationId}
            busy={streaming}
            onSwitch={(conversationId) => client.send('switch_conversation', { conversationId })}
            onCreate={() => client.send('create_conversation', {})}
          />
        </>
      )}
      {unavailable && !notConfigured && <AgentUnavailableNotice />}
      {!notConfigured && connected && (
        <ConnectedBanner
          provider={connected.provider}
          warning={connected.warning}
          onDismiss={() => setConnected(null)}
        />
      )}
      {body()}
      <Composer
        disabled={unavailable}
        disabledReason={notConfigured ? 'ask, or steer this chat…' : unavailable ? 'The Agent is not available' : undefined}
        streaming={streaming}
        initialText={state.draft}
        attachments={stagedAttachments}
        onSend={sendMessage}
        onStop={() => {
          if (state.streamingMessageId) client.send('stop_generation', { messageId: state.streamingMessageId });
        }}
        onAttachFiles={attachFiles}
        onRemoveAttachment={removeAttachment}
        onDraftChange={(text) => client.send('draft_update', { text })}
        draftDebounceMs={draftDebounceMs}
      />
    </div>
  );
}
