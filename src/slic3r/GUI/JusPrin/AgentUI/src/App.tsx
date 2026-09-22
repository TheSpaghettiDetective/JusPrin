import { useEffect, useMemo, useReducer, useRef, useState } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import { AttachmentSource, Envelope } from './bridge/protocol';
import { AgentUiState, initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { SetupCard } from './components/SetupCard';
import { ChatHeader, ChatList } from './components/ChatNavigation';
import { MessageList } from './components/MessageList';
import { ToolActivityCard } from './components/ToolActivityCard';
import { PlanActivityCard, planHeadline, planKey, planMembers } from './components/PlanActivityCard';
import { Composer } from './components/Composer';
import { PrinterAccessCode, PrinterChangeCard, PrinterChipRow } from './components/PrinterPanel';
import { PrinterConnection } from './components/PrinterConnection';
import { printerInstructions } from './printerInstructions';
import { ACCESS_CODE_NOTE, opening, placeholder, rejectedNote, workingText } from './printerWords';
import {
  AgentNotConfiguredHeader,
  AgentNotConfiguredPane,
  AgentUnavailableNotice,
  BridgeErrorPane,
  ConnectingPane,
} from './components/Panels';
import { ConnectedBanner, DEFAULT_PROVIDER, SetupApiKey, SetupChooser } from './components/Setup';
import { SetupLocalTool } from './components/SetupLocalTool';

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
      renameConversation(conversationId: string, title: string): void;
      deleteConversation(conversationId: string): void;
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
  // A throwaway, setup-only instance (e.g. embedded in the Add a printer
  // dialog rather than the docked panel): show only the setup sub-component,
  // never the conversation header, chat list, or composer around it.
  embedded?: boolean;
  // The printer panel on Home: the same thread and composer, with the printer
  // session's own header, pinned card and chips instead of the project's.
  printerPanel?: boolean;
}

const errorTitles: Partial<Record<ConnectionState, string>> = {
  'no-transport': 'The Agent panel has no connection to JusPrin',
  timeout: 'The Agent panel could not reach JusPrin',
  incompatible: 'This Agent panel does not match this JusPrin build',
};

export function App({
  getTransport,
  handshakeTimeoutMs,
  transportRetryMs,
  transportRetryLimit,
  draftDebounceMs,
  embedded,
  printerPanel,
}: AppProps) {
  const [state, dispatch] = useReducer(reducer, initialState);
  const stateRef = useRef<AgentUiState>(state);
  stateRef.current = state;

  // Which setup screen the dock is showing. This is page-local on purpose:
  // the host cares which credentials it was asked to check, not which panel
  // is on screen, so navigating setup costs no bridge traffic.
  const [setupScreen, setSetupScreen] = useState<'offer' | 'chooser' | 'apiKey' | 'localTool'>('offer');
  const [view, setView] = useState<'chat' | 'list' | 'setup'>('chat');
  // The setup card's expansion is a temporary layer over the thread, so it is
  // page-local and closes on its own the moment the user does something else.
  const [setupExpanded, setSetupExpanded] = useState(false);
  // Every way out of this chat closes the card's expansion. Kept explicit
  // rather than derived from an effect: an effect on (chat, view) re-ran
  // whenever the host resent state and re-closed the card mid-click.
  const collapseSetup = () => setSetupExpanded(false);
  const [commandError, setCommandError] = useState<string | null>(null);
  // "‹ Printers" or "Set it up myself" mid-conversation: which one is
  // pending confirmation, since leaving would discard what was said. Null
  // means no dialog is showing.
  const [confirmLeavePrinter, setConfirmLeavePrinter] = useState<'close' | 'manual_setup' | null>(null);
  // What the printer panel's own composer holds right now, so the leave
  // check can see it. The printer panel's composer sends no draft_update --
  // unlike the main chat's, it is never saved -- so state.draft, which only
  // the backend ever sets, stays empty through an entire typing session and
  // cannot answer "is there unsent text" for this panel. Reset whenever the
  // conversation itself changes, so text left over from a printer session
  // that already ended cannot gate the next one's leave check.
  const [printerDraftText, setPrinterDraftText] = useState('');
  useEffect(() => {
    setPrinterDraftText('');
  }, [state.activeConversationId]);
  // The printer panel's access-code field. Held here, not in host state: the
  // code goes to the app with Add and nowhere else.
  const [accessCode, setAccessCode] = useState('');
  const setupReturn = useRef<'chat' | 'list'>('chat');
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
          const payload = envelope.payload as { code?: string; message?: string };
          setCommandError(payload.message ?? 'The action could not be completed.');
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

  // The printer panel's first line, which the model is shown as having said:
  // written here, posted by the app, once per session. Before the
  // instructions, so the thread opens with it whatever is sent next.
  const sentOpening = useRef(false);
  useEffect(() => {
    if (!printerPanel || !state.session || state.connection !== 'connected' || state.session.mode === 'connect') return;
    if (sentOpening.current || state.messages.length > 0) return;
    sentOpening.current = true;
    client.send('printer_opening', { text: opening(state.session) });
  }, [printerPanel, state.session, state.connection, state.messages.length, client]);

  // The printer panel writes the model's instructions from the facts the app
  // sends; the app holds them for the session's requests. Sent again only
  // when the words change, such as after a change to the printer.
  const sentInstructions = useRef<string | null>(null);
  useEffect(() => {
    if (!printerPanel || !state.session || state.connection !== 'connected' || state.session.mode === 'connect') return;
    const text = printerInstructions(state.session);
    if (text === sentInstructions.current) return;
    sentInstructions.current = text;
    client.send('printer_instructions', { text });
  }, [printerPanel, state.session, state.connection, client]);

  useEffect(() => { setView('chat'); setCommandError(null); }, [state.context?.sessionId]);

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
    // From the ref, not this render: the test hook keeps the first render's
    // sendMessage, which would otherwise never see a later attachment.
    const attachmentIds = stateRef.current.attachments.filter((a) => a.state === 'staged').map((a) => a.id);
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

  const checkKey = (provider: string, apiKey: string) => {
    client.send('setup_check_key', { provider, apiKey });
  };

  const cancelCheck = () => {
    client.send('setup_cancel', {});
  };

  // The one way into setup: the dock's own button, and the host when another
  // JusPrin surface sends the person here.
  const openSetup = () => { setupReturn.current = 'chat'; setSetupScreen('chooser'); setView('setup'); };

  useEffect(() => {
    if (state.setupRequests > 0) openSetup();
  }, [state.setupRequests]);

  useEffect(() => {
    if (state.setup.phase !== 'verified') return;
    // The host installs the verified service right after saying so, so the
    // dock is about to become a working chat; carry the confirmation across.
    // The key screen stays up until that happens, so the round-trip the check
    // measured is actually readable rather than flashing past.
    setConnected({ provider: state.setup.provider ?? DEFAULT_PROVIDER, warning: state.setup.warning });
    if (state.agentStatus === 'ready') setView('chat');
  }, [state.setup.phase, state.setup.provider, state.setup.warning, state.agentStatus]);

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
      renameConversation: (conversationId, title) => client.send('rename_conversation', { conversationId, title }),
      deleteConversation: (conversationId) => client.send('delete_conversation', { conversationId }),
      setDraft: (text: string) => client.send('draft_update', { text }),
      openSetup,
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
  // The printer greeting is added before an Agent is configured, so it must
  // not hide setup. The project dock keeps saved history visible instead.
  const notConfigured = unavailable && (printerPanel || state.messages.length === 0);
  const streaming = state.streamingMessageId !== null;
  const busy = streaming || state.conversationBusy;
  const activeChat = state.conversations.find((chat) => chat.id === state.activeConversationId);
  const externalActions = state.toolActivities.filter((activity) => activity.source === 'mcp' && activity.requiresApproval);
  const externalPlans = planMembers(externalActions);
  const pendingAction = state.toolActivities.some((activity) =>
    state.messages.some((message) => message.id === activity.correlationId) &&
    ['pending', 'approved', 'running'].includes(activity.state));
  const createChat = () => { client.send('create_conversation', {}); setView('chat'); collapseSetup(); };
  const closeSetup = () => { cancelCheck(); setSetupScreen('offer'); setView(setupReturn.current); collapseSetup(); };

  const errorNotice = commandError && <div className="chat-error" role="alert">
    <span>{commandError}</span><button aria-label="Dismiss error" onClick={() => setCommandError(null)}>×</button>
  </div>;

  const chatList = <ChatList conversations={state.conversations} activeId={state.activeConversationId} busy={busy}
      onSwitch={(conversationId) => {
        if (conversationId !== state.activeConversationId) client.send('switch_conversation', { conversationId });
        setView('chat');
        collapseSetup();
      }} onCreate={createChat} onConfigure={() => {
        setupReturn.current = 'list'; setSetupScreen('chooser'); setView('setup');
      }} />;

  // The dock body is one of three things: the conversation, the offer, or a
  // setup screen. Setup replaces the body rather than covering it, so backing
  // out returns to exactly what was there before.
  const body = (onManualSetup?: () => void) => {
    // An embedded, setup-only instance never has a conversation to show, so
    // it always renders one of the setup screens below regardless of view.
    if (!embedded && !notConfigured && view !== 'setup')
      return (
        <MessageList
          dimmed={setupExpanded}
          key={`messages-${state.context?.sessionId}-${state.activeConversationId}`}
          messages={state.messages}
          attachments={state.attachments}
          streamingMessageId={state.streamingMessageId}
          toolActivities={state.toolActivities}
          builds={state.builds}
          exportedCopies={state.exportedCopies}
          physicalPrints={state.physicalPrints}
          changes={state.changes.filter((change) => change.conversationId === state.activeConversationId)}
          onRetry={(messageId) => client.send('retry_message', { messageId })}
          onToolDecision={sendToolDecision}
          onToolCancel={sendToolCancel}
        />
      );
    if (setupScreen === 'chooser')
      return <SetupChooser onUseApiKey={() => setSetupScreen('apiKey')} onConnectTool={() => setSetupScreen('localTool')} onDismiss={closeSetup} />;
    if (setupScreen === 'localTool')
      return (
        <SetupLocalTool
          catalog={state.mcpCatalog}
          preview={state.mcpPreview}
          status={state.mcpStatus}
          onRefresh={() => client.send('mcp_catalog', {})}
          onPreview={(toolId) => client.send('mcp_preview', { toolId })}
          onConnect={(toolId) => client.send('mcp_connect', { toolId })}
          onReveal={(toolId) => client.send('reveal_path', { toolId })}
          onBack={() => setSetupScreen('chooser')}
          onDone={closeSetup}
        />
      );
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
    return <AgentNotConfiguredPane onSetUp={openSetup} onManualSetup={onManualSetup} />;
  };

  if (printerPanel) {
    const session = state.session;
    const printerClass = `app app--printer${session?.mode === 'add' ? ' app--printer-setup' : ''}`;
    const offerManualPrinterSetup = notConfigured && session?.mode === 'add' && view === 'chat';
    const printerAction = (action: string, id = '', extra: Record<string, string> = {}) =>
      client.send('printer_action', { action, id, ...extra });
    if (session && (session.connection || (session.added?.length ?? 0) > 0)) {
      return <div className={printerClass}>{errorNotice}<PrinterConnection session={session} onAction={printerAction} /></div>;
    }
    // F7 review fix: message count alone caught a saved, applied change too
    // (Change mode stays open after a printer_change succeeds), warning
    // about words that were not actually going to be lost. Gate on unsaved
    // work instead: a draft still in the composer, a photo staged but not
    // sent, an Add proposal sitting on its card waiting for a tap, or a
    // Change still waiting on its own approval card.
    const hasUnsavedPrinterWork = () => {
      if (printerDraftText.trim().length > 0) return true;
      if (state.attachments.some((attachment) => attachment.state === 'staged')) return true;
      if (!session) return false;
      if (session.mode === 'add') return session.canAdd;
      return state.toolActivities.some(
        (activity) => activity.tool === 'printer_change' && activity.requiresApproval && activity.state === 'pending',
      );
    };
    const gatedPrinterAction = (action: 'close' | 'manual_setup') => {
      if (hasUnsavedPrinterWork()) setConfirmLeavePrinter(action);
      else printerAction(action);
    };
    return (
      // The whole panel takes a photo, not only the composer: a picture of
      // the printer is dropped where the person is looking.
      <div
        className={printerClass}
        onDragOver={(event) => event.preventDefault()}
        onDrop={(event) => {
          const files = Array.from(event.dataTransfer.files ?? []);
          if (files.length === 0) return;
          event.preventDefault();
          attachFiles(files, 'drop');
        }}
      >
        {errorNotice}
        <div className="chat-content">
          {/* The label names where ‹ leads, not the printer this is about. */}
          <header className="chat-header printer-header">
            <button type="button" className={session?.mode === 'add' ? 'printer-manual-link' : 'icon-button'} aria-label={session?.mode === 'add' ? 'Back to Home' : 'Back to printers'} onClick={() => gatedPrinterAction('close')}>
              {session?.mode === 'add' ? '‹ Back to Home' : '‹'}
            </button>
            {session?.mode !== 'add' && <h1>Printers</h1>}
            {!offerManualPrinterSetup && (
              <button type="button" className="printer-manual-link" onClick={() => gatedPrinterAction('manual_setup')}>
                {session?.mode === 'add' ? 'Choose printer manually' : 'Set it up myself'}
              </button>
            )}
          </header>
          {session?.mode === 'add' && <h1 className="printer-setup-title">Let’s add your printer</h1>}
          {session?.mode === 'add' && <p className="printer-setup-explanation">Choose your printer so JusPrin can prepare prints for it. You can connect it afterward.</p>}
          {session?.manualApplied && !session.added?.length && <p className="printer-setup-explanation" role="status">No new printers were added. Your existing printers are available from Home.</p>}
          {session?.mode === 'change' && <button type="button" className="printer-manual-link" onClick={() => printerAction('connect', session.context?.printer?.name || session.facts.printer.name)}>Connect printer</button>}
          {confirmLeavePrinter && (
            <div className="chat-dialog-shade printer-leave-shade">
              <div className="chat-dialog" role="dialog" aria-modal="true" aria-labelledby="printer-leave-title">
                <h2 id="printer-leave-title">Leave this conversation?</h2>
                <p>What you said here will be lost.</p>
                <div className="chat-dialog-buttons">
                  <button type="button" onClick={() => setConfirmLeavePrinter(null)}>
                    Cancel
                  </button>
                  <button
                    type="button"
                    className="danger"
                    onClick={() => {
                      const action = confirmLeavePrinter;
                      setConfirmLeavePrinter(null);
                      printerAction(action);
                    }}
                  >
                    Leave
                  </button>
                </div>
              </div>
            </div>
          )}
          {notConfigured ? (
            body(offerManualPrinterSetup ? () => gatedPrinterAction('manual_setup') : undefined)
          ) : (
            <MessageList
              messages={state.messages}
              attachments={state.attachments}
              streamingMessageId={state.streamingMessageId}
              // In this panel a tool's result is the card it draws, so only a
              // change to confirm, or one that failed, is worth a card of its
              // own. A printer the model named wrongly is its to explain.
              toolActivities={state.toolActivities.filter(
                (activity) => activity.tool === 'printer_change' && (activity.requiresApproval || activity.state === 'failed'),
              )}
              renderActivity={(activity) =>
                activity.tool === 'printer_change' ? (
                  <PrinterChangeCard activity={activity} onDecision={sendToolDecision} />
                ) : undefined
              }
              builds={[]}
              exportedCopies={[]}
              physicalPrints={[]}
              changes={[]}
              printerBlocks={session?.blocks}
              printerActivityText={session ? workingText(session.mode) : undefined}
              onPrinterAction={(action, id, tap) => {
                const extra: Record<string, string> = {};
                if (tap.blockId) extra.blockId = tap.blockId;
                if (tap.note) extra.note = tap.note;
                printerAction(action, id, extra);
              }}
              answeredState={false}
              onRetry={(messageId) => client.send('retry_message', { messageId })}
              onToolDecision={sendToolDecision}
              onToolCancel={sendToolCancel}
            />
          )}
          {!notConfigured && session && (
            <>
              {session.accessCode && <PrinterAccessCode value={accessCode} onChange={setAccessCode} />}
              <PrinterChipRow
                canAdd={session.canAdd}
                disabled={busy}
                onAdd={() =>
                  printerAction('add', '', session.accessCode && accessCode ? { accessCode, note: ACCESS_CODE_NOTE } : {})
                }
                onReject={() => printerAction('reject', '', { note: rejectedNote(session.facts.printer.name) })}
              />
            </>
          )}
          {!notConfigured && (
            <Composer
              disabled={unavailable}
              disabledReason={unavailable ? 'The Agent is not available' : undefined}
              placeholder={session ? placeholder(session) : undefined}
              photoButton
              streaming={streaming}
              attachments={stagedAttachments}
              onSend={(text) => {
                setPrinterDraftText('');
                sendMessage(text);
              }}
              onStop={() => {
                if (state.streamingMessageId) client.send('stop_generation', { messageId: state.streamingMessageId });
              }}
              onAttachFiles={attachFiles}
              onRemoveAttachment={removeAttachment}
              onDraftChange={setPrinterDraftText}
            />
          )}
        </div>
      </div>
    );
  }

  if (embedded) {
    // No conversation header, chat list, composer, or external-tool banner --
    // just the setup sub-component itself, at whatever size its host gives it.
    return (
      <div className="app app--embedded">
        {errorNotice}
        <div className="chat-content">{body()}</div>
      </div>
    );
  }

  return (
    <div className="app">
      {errorNotice}
      {externalActions.length > 0 && <section className="external-actions" aria-label="External AI tools">
        <h2>External AI tools</h2>
        {externalActions.map((activity) => {
          const key = planKey(activity);
          const found = key ? externalPlans.get(key) : undefined;
          // A plan of one change is decided like any other call.
          const members = found && found.length > 1 ? found : undefined;
          if (members && members[0].actionId !== activity.actionId) return null;
          return members
            ? <PlanActivityCard key={activity.actionId} members={members} headline={planHeadline(state.toolActivities)}
                onDecision={sendToolDecision} onCancel={sendToolCancel} />
            : <ToolActivityCard key={activity.actionId} activity={activity}
                onDecision={sendToolDecision} onCancel={sendToolCancel} />;
        })}
      </section>}
      {view === 'list' && chatList}
      <div className="chat-content" hidden={view === 'list'}>
      {notConfigured && state.conversations.length === 1 && view !== 'setup' ? (
        <AgentNotConfiguredHeader />
      ) : (
        <>
          <ChatHeader key={`header-${state.context?.sessionId}-${state.activeConversationId}`} title={activeChat?.title || 'New chat'} busy={busy || pendingAction}
            onBack={() => { collapseSetup(); if (view === 'setup') closeSetup(); else { client.send('state_request', {}); setView('list'); } }}
            onCreate={createChat}
            onRename={(title) => client.send('rename_conversation', { conversationId: state.activeConversationId, title })}
            onDelete={() => { client.send('delete_conversation', { conversationId: state.activeConversationId }); setView('list'); }} />
          {view === 'chat' && !notConfigured && state.context && (
            // The card sits in its own pinned band above the thread, as the
            // design has it: the band is the canvas the tinted card sits on.
            <div className="pinned-setup">
              <SetupCard context={state.context} expanded={setupExpanded} working={busy}
                onToggle={() => setSetupExpanded((open) => !open)} />
            </div>
          )}
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
      <div hidden={view === 'setup'}><Composer
        key={`composer-${state.context?.sessionId}-${state.activeConversationId}`}
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
        onTyping={collapseSetup}
        onDraftChange={(text) => client.send('draft_update', { text })}
        draftDebounceMs={draftDebounceMs}
      /></div>
      </div>
    </div>
  );
}
