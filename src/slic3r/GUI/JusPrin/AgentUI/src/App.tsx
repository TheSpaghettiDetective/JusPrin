import { useEffect, useMemo, useReducer, useRef, useState } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import { AttachmentSource, Envelope, SliceEstimateInfo } from './bridge/protocol';
import { AgentUiState, initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { RECENT_CHANGE_MS, SetupCard } from './components/SetupCard';
import { AgentPaneToggle, ChatHeader, ChatList, Dialog } from './components/ChatNavigation';
import { MessageList } from './components/MessageList';
import { Composer } from './components/Composer';
import { PrinterCredentialForm } from './components/PrinterPanel';
import { printerInstructions } from './printerInstructions';
import { filamentInstructions } from './filamentInstructions';
import { fileReportInstructions } from './fileReportInstructions';
import { opening, placeholder } from './printerWords';
import {
  AgentNotConfiguredHeader,
  AgentNotConfiguredPane,
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
  // Temporary printer task chat: the same thread and composer, with the
  // printer session's own header and cards instead of the project's.
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
  const [confirmChatRestore, setConfirmChatRestore] = useState(false);
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
    if (!printerPanel || !state.session || state.connection !== 'connected') return;
    if (sentOpening.current || state.messages.length > 0) return;
    sentOpening.current = true;
    client.send('printer_opening', { text: opening(state.session) });
  }, [printerPanel, state.session, state.connection, state.messages.length, client]);

  // The printer panel writes the model's instructions from the facts the app
  // sends; the app holds them for the session's requests. Sent again only
  // when the words change, such as after a change to the printer.
  const sentInstructions = useRef<string | null>(null);
  useEffect(() => {
    if (!printerPanel || !state.session || state.connection !== 'connected') return;
    const text = printerInstructions(state.session);
    if (text === sentInstructions.current) return;
    sentInstructions.current = text;
    client.send('printer_instructions', { text });
  }, [printerPanel, state.session, state.connection, client]);

  // What the project Agent is told about a file the person brings in; the app
  // holds it for the turn a file report opens. Once per connection: a reload
  // reconnects, and the words do not change.
  const sentFileReportInstructions = useRef(false);
  useEffect(() => {
    if (printerPanel || embedded || state.connection !== 'connected') {
      if (state.connection !== 'connected') sentFileReportInstructions.current = false;
      return;
    }
    if (sentFileReportInstructions.current) return;
    sentFileReportInstructions.current = true;
    client.send('file_report_instructions', { text: fileReportInstructions() });
  }, [printerPanel, embedded, state.connection, client]);

  // The filament chat's instructions, likewise: written from the facts the
  // app sends, sent again when they change, such as after a save as a copy.
  const sentFilamentInstructions = useRef<string | null>(null);
  useEffect(() => {
    if (!state.filamentSession || state.connection !== 'connected') return;
    const text = filamentInstructions(state.filamentSession);
    if (text === sentFilamentInstructions.current) return;
    sentFilamentInstructions.current = text;
    client.send('filament_instructions', { text });
  }, [state.filamentSession, state.connection, client]);

  // The printer panel's Back asks first once leaving would lose something:
  // what the person said, or what they are about to send. The composer's
  // words are mirrored here, not in state, so typing renders nothing more.
  const [confirmPrinterClose, setConfirmPrinterClose] = useState(false);
  const printerDraft = useRef('');

  useEffect(() => { setView('chat'); setCommandError(null); }, [state.context?.sessionId]);
  useEffect(() => { if (state.navigation.focused) setView('chat'); }, [state.navigation.focused]);

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

  const sendMessage = (text: string, includeStagedAttachments = true) => {
    // From the ref, not this render: the test hook keeps the first render's
    // sendMessage, which would otherwise never see a later attachment.
    const attachmentIds = includeStagedAttachments
      ? stateRef.current.attachments.filter((a) => a.state === 'staged').map((a) => a.id)
      : [];
    client.send('user_message', { clientMessageId: nextClientMessageId(), text, attachmentIds,
      conversationId: stateRef.current.activeConversationId });
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

  // A printer access code goes straight to the printer session and never
  // enters a tool activity or chat.
  const connectPrinter = (actionId: string, credential: string) => {
    client.send('printer_action', { action: 'connect', actionId, credential });
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

  useEffect(() => {
    const viewed = state.viewedConversationId || state.activeConversationId;
    if (state.connection !== 'connected' || viewed !== state.activeConversationId ||
        view !== 'chat' || !state.context) return;
    const timer = window.setInterval(() => client.send('state_request', {}), 30000);
    return () => window.clearInterval(timer);
  }, [client, state.connection, state.viewedConversationId, state.activeConversationId, view, state.context?.sessionId]);

  // "Just changed" is a window of time, so something has to end it: one
  // timer per newest change, set for the moment that change stops being new.
  const [now, setNow] = useState(() => Date.now());
  const newestChange = state.changes.at(-1);
  useEffect(() => {
    setNow(Date.now());
    if (!newestChange) return;
    const left = Date.parse(newestChange.createdAt) + RECENT_CHANGE_MS - Date.now();
    if (!Number.isFinite(left) || left <= 0) return;
    const timer = window.setTimeout(() => setNow(Date.now()), left + 50);
    return () => window.clearTimeout(timer);
  }, [newestChange?.seq, newestChange?.createdAt]);

  // The pair behind "Estimate: a → b": the plate's last current estimate, and
  // the one that was in force when the newest change arrived. Both are native
  // figures for the same plate; nothing here is computed.
  const activePlate = state.context?.plates.find((plate) => plate.active);
  const lastEstimate = useRef<{ plateId: string; estimate: SliceEstimateInfo } | null>(null);
  const beforeChange = useRef<{ seq: number; plateId: string; estimate: SliceEstimateInfo } | null>(null);
  useEffect(() => {
    if (!newestChange || beforeChange.current?.seq === newestChange.seq) return;
    beforeChange.current = lastEstimate.current ? { seq: newestChange.seq, ...lastEstimate.current } : null;
  }, [newestChange?.seq]);
  useEffect(() => {
    if (activePlate?.estimate && activePlate.estimateStatus === 'current')
      lastEstimate.current = { plateId: activePlate.id, estimate: activePlate.estimate };
  }, [activePlate?.id, activePlate?.estimate, activePlate?.estimateStatus]);

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
  // The printer greeting and a focused filament context note must not hide
  // setup. File reports are notes, so they do not turn an otherwise empty
  // project conversation into history. Other saved history stays visible.
  const onlyFileReports = state.messages.every((message) => message.role === 'note' && !!message.fileReport);
  const fileReportNotes = state.messages.flatMap((message) =>
    message.role === 'note' && message.fileReport && message.fileReportCard
      ? [{ id: message.id, report: message.fileReport }]
      : [],
  );
  const notConfigured = unavailable && (printerPanel || state.navigation.focused || onlyFileReports);
  const streaming = state.streamingMessageId !== null;
  const busy = streaming || state.conversationBusy || state.projectChatBlocked;
  const viewedId = state.viewedConversationId || state.activeConversationId;
  const viewedChat = state.conversations.find((chat) => chat.id === viewedId);
  const historical = viewedId !== state.activeConversationId;
  const resumeStatus = state.chatResume.status;
  // What the notice on an earlier chat says depends on what its checkpoint is
  // worth: only a project that really moved on is described as updated.
  const boundary = resumeStatus === 'changed'
    ? 'There have been project updates since this chat. Restore its saved project version to continue.'
    : resumeStatus === 'unchanged'
      ? 'This chat is inactive. Resume from its saved setup to continue.'
      : 'This chat has no recoverable project checkpoint. Its saved state is missing or corrupt.';
  const returnToActiveChat = () => {
    client.send('switch_conversation', { conversationId: state.activeConversationId });
    setView('chat');
    collapseSetup();
  };
  const askCurrentChat = () => {
    const latest = [...stateRef.current.messages].reverse().find((message) => message.text.trim());
    const excerpt = latest ? `\nExcerpt: “${latest.text.trim().slice(0, 600)}”` : '';
    const reference = `From earlier chat “${viewedChat?.title || viewedId}”${state.chatResume.savedAt ?
      ` (saved ${state.chatResume.savedAt})` : ''}:${excerpt}\n\nWhat should we do with this in the current project?`;
    client.send('switch_conversation', { conversationId: state.activeConversationId });
    client.send('draft_update', { append: reference });
    client.send('state_request', {});
    setView('chat');
    collapseSetup();
  };
  const resumeChat = () => {
    if (resumeStatus === 'changed') { setConfirmChatRestore(true); return; }
    if (resumeStatus === 'unchanged') client.send('restore_conversation', {
      conversationId: viewedId, activeConversationId: state.activeConversationId, docRevision: state.docRevision,
    });
  };
  // The notice on an earlier chat: why it cannot continue as it is, and the
  // ways on. It sits in the composer's band, above the field it has locked.
  const projectUpdates = historical && <div className="project-updates" role="note">
    <p>{boundary}</p>
    <div className="project-updates-actions">
      {resumeStatus !== 'unavailable' && <button type="button" className="primary"
        disabled={busy || state.projectChatBlocked} onClick={resumeChat}>{resumeStatus === 'changed' ? 'Restore and resume' : 'Resume saved setup'}</button>}
      <button type="button" className="project-updates-link" onClick={returnToActiveChat}>Return to active chat</button>
      <button type="button" className="project-updates-link" onClick={askCurrentChat}>Ask current chat about this</button>
    </div>
  </div>;
  const blockedAlert = state.projectChatBlocked && <div className="composer-alert" role="alert">
    Project restoration needs recovery. This chat cannot run project actions.
  </div>;
  const cardContext = historical ? state.chatResume.summary ?? null : state.context;
  // A saved summary belongs to a send when this chat sent the plate it shows.
  const sentBuild = historical ? [...state.builds].reverse().find((record) => record.conversationId === viewedId &&
    !!record.sentAt && record.plateIndex === cardContext?.plates.findIndex((plate) => plate.active)) : undefined;
  // The card sits in its own pinned band above the thread, as the design has
  // it: the band is the canvas the tinted card sits on.
  const setupCard = view === 'chat' && cardContext && <div className="pinned-setup">
    <SetupCard context={cardContext} historical={historical} savedAt={historical ? state.chatResume.savedAt : undefined}
      agentAvailable={!unavailable} expanded={setupExpanded} working={!historical && busy}
      changes={historical ? undefined : state.changes.filter((change) => change.conversationId === viewedId)}
      now={now}
      estimateBefore={!historical && beforeChange.current?.seq === newestChange?.seq &&
        beforeChange.current?.plateId === activePlate?.id ? beforeChange.current?.estimate : null}
      sent={sentBuild?.sentAt ? { at: sentBuild.sentAt, printer: sentBuild.printer } : undefined}
      onCompute={historical ? undefined : () => {
        const current = stateRef.current.context;
        const plate = current?.plates.find((item) => item.active);
        if (current && plate) client.send('shell_action', {
          action: 'compute_setup_estimates', sessionId: current.sessionId, plateId: plate.id,
        });
      }}
      onUndo={historical ? undefined : (changeSeq) => {
        const current = stateRef.current.context;
        if (current) client.send('shell_action', { action: 'undo_setup_change', sessionId: current.sessionId, changeSeq });
      }}
      onToggle={() => setSetupExpanded((open) => !open)} />
  </div>;
  const pendingAction = state.toolActivities.some((activity) =>
    state.messages.some((message) => message.id === activity.correlationId) &&
    activity.state === 'running');
  const createChat = () => { client.send('create_conversation', {}); setView('chat'); collapseSetup(); };
  const collapseAgentPane = () => client.send('shell_action', { action: 'collapse_agent_pane' });
  const returnToWorkspace = () => client.send('shell_action', { action: 'return_to_workspace' });
  const closeSetup = () => { cancelCheck(); setSetupScreen('offer'); setView(setupReturn.current); collapseSetup(); };

  const errorNotice = commandError && <div className="chat-error" role="alert">
    <span>{commandError}</span><button className="icon" aria-label="Dismiss error" title="Dismiss error" onClick={() => setCommandError(null)}><span className="jp-icon jp-icon-close" aria-hidden="true" /></button>
  </div>;

  const chatList = <ChatList conversations={state.conversations} activeId={state.activeConversationId} busy={busy}
      onSwitch={(conversationId) => {
        if (conversationId !== viewedId) client.send('switch_conversation', { conversationId });
        setView('chat');
        collapseSetup();
      }} onCreate={createChat} agentUnavailable={unavailable} onConfigure={() => {
        setupReturn.current = 'list'; setSetupScreen('chooser'); setView('setup');
      }} onCollapse={collapseAgentPane} />;

  // The dock body is one of three things: the conversation, the offer, or a
  // setup screen. Setup replaces the body rather than covering it, so backing
  // out returns to exactly what was there before.
  const body = () => {
    // An embedded, setup-only instance never has a conversation to show, so
    // it always renders one of the setup screens below regardless of view.
    if (!embedded && !notConfigured && view !== 'setup')
      return (
        <MessageList
          dimmed={setupExpanded}
          key={`messages-${state.context?.sessionId}-${viewedId}`}
          messages={state.messages}
          attachments={state.attachments}
          streamingMessageId={state.streamingMessageId}
          toolActivities={state.toolActivities}
          builds={historical ? state.builds.filter((record) => record.conversationId === viewedId) : state.builds}
          exportedCopies={historical ? state.exportedCopies.filter((record) => record.conversationId === viewedId) : state.exportedCopies}
          physicalPrints={historical ? state.physicalPrints.filter((record) => record.conversationId === viewedId) : state.physicalPrints}
          changes={state.changes.filter((change) => change.conversationId === viewedId)}
          restorePoints={historical ? [] : state.restorePoints}
          onRevert={(versionId) => client.send('shell_action', { action: 'revert_to_here', versionId })}
          answeredState={!state.navigation.focused}
          onRetry={(messageId) => client.send('retry_message', { messageId, conversationId: viewedId })}
          onToolCancel={sendToolCancel}
          onSend={sendMessage}
          replyDisabled={unavailable || streaming || historical || state.projectChatBlocked}
          readOnly={historical || state.projectChatBlocked}
          onDiscussFailure={(text) => sendMessage(text, false)}
          onSetUpAgent={openSetup}
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
    return <AgentNotConfiguredPane onSetUp={openSetup} fileReports={fileReportNotes} />;
  };

  if (printerPanel) {
    const session = state.session;
    const printerAction = (action: string) => client.send('printer_action', { action });
    // No tool closes the panel for the model: the person decides when they
    // are done, with Back or by opening the printer's own settings.
    const back = () => {
      const started = state.messages.some((message) => message.role === 'user') ||
        state.attachments.some((attachment) => attachment.state === 'staged') || printerDraft.current.trim() !== '';
      if (started) setConfirmPrinterClose(true);
      else printerAction('close');
    };
    return (
      // The whole panel takes a photo, not only the composer: a picture of
      // the printer is dropped where the person is looking.
      <div
        className="app app--printer app--printer-setup"
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
          <header className="chat-header printer-header">
            <button type="button" className="printer-link-button printer-back-link" onClick={back}>
              <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" /> Back
            </button>
            <button type="button" className="printer-link-button" onClick={() => printerAction(session?.printerName ? 'open_printer_settings' : 'manual_setup')}>
              {session?.printerName ? 'Open printer settings' : 'Browse the full printer list'}
            </button>
          </header>
          {confirmPrinterClose && (
            <Dialog title="Close this conversation?" onClose={() => setConfirmPrinterClose(false)}>
              <p>This conversation will be closed. You can't continue it at a later point. Are you sure?</p>
              <div className="chat-dialog-buttons">
                <button type="button" onClick={() => setConfirmPrinterClose(false)}>Cancel</button>
                <button type="button" className="danger decisive" onClick={() => { setConfirmPrinterClose(false); printerAction('close'); }}>
                  Close conversation
                </button>
              </div>
            </Dialog>
          )}
          {notConfigured ? (
            body()
          ) : (
            <MessageList
              messages={state.messages}
              attachments={state.attachments}
              streamingMessageId={state.streamingMessageId}
              onSend={sendMessage}
              replyDisabled={unavailable || streaming}
              toolActivities={state.toolActivities.filter((activity) => activity.tool === 'settings_apply_patch' ||
                activity.actionId === session?.credentialRequest?.actionId)}
              renderActivity={(activity) => activity.actionId !== session?.credentialRequest?.actionId ? undefined : (
                <PrinterCredentialForm request={session.credentialRequest} onConnect={connectPrinter} />
              )}
              builds={[]}
              exportedCopies={[]}
              physicalPrints={[]}
              changes={[]}
              printerBlocks={session?.blocks ?? []}
              onUndoAdd={(blockId) => client.send('printer_action', { action: 'undo_add', blockId })}
              onInstallPlugin={() => client.send('printer_action', { action: 'install_network_plugin' })}
              answeredState={false}
              onRetry={(messageId) => client.send('retry_message', { messageId })}
              onToolCancel={sendToolCancel}
            />
          )}
          {!notConfigured && (
            <Composer
              disabled={unavailable}
              disabledReason={unavailable ? 'The Agent is not available' : undefined}
              placeholder={session ? placeholder(session) : undefined}
              photoButton
              streaming={streaming}
              attachments={stagedAttachments}
              onDraftChange={(text) => { printerDraft.current = text; }}
              draftDebounceMs={0}
              onSend={(text) => { printerDraft.current = ''; sendMessage(text); }}
              onStop={() => {
                if (state.streamingMessageId) client.send('stop_generation', { messageId: state.streamingMessageId });
              }}
              onAttachFiles={attachFiles}
              onRemoveAttachment={removeAttachment}
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
    <div className={state.navigation.focused ? 'app app--focused-chat' : 'app'}>
      {errorNotice}
      {view === 'list' && !state.navigation.focused && chatList}
      <div className="chat-content" hidden={view === 'list'}>
      {notConfigured && state.conversations.length === 1 && view !== 'setup' && !state.navigation.focused ? (
        <>
          <AgentNotConfiguredHeader onCollapse={collapseAgentPane} />
          {setupCard}
        </>
      ) : state.navigation.focused ? (
        <header className="chat-header printer-header">
          <button type="button" className="printer-link-button printer-back-link" aria-label={state.navigation.returnLabel ?? 'Back to Prepare'}
            onClick={() => { if (view === 'setup') closeSetup(); else returnToWorkspace(); }}><span className="jp-icon jp-icon-chevron-left" aria-hidden="true" /> Back</button>
          {view !== 'setup' && <button type="button" className="printer-link-button"
            onClick={() => client.send('shell_action', { action: 'open_filament_settings' })}>Open filament settings</button>}
          <AgentPaneToggle onCollapse={collapseAgentPane} />
        </header>
      ) : (
        <>
          <ChatHeader key={`header-${state.context?.sessionId}-${viewedId}`} title={viewedChat?.title || 'New chat'} busy={busy || pendingAction}
            onBack={() => { collapseSetup(); if (view === 'setup') closeSetup(); else { client.send('state_request', {}); setView('list'); } }}
            onCreate={createChat}
            onRename={(title) => client.send('rename_conversation', { conversationId: viewedId, title })}
            onDelete={() => { client.send('delete_conversation', { conversationId: viewedId }); setView('list'); }}
            onCollapse={collapseAgentPane} />
          {setupCard}
        </>
      )}
      {!notConfigured && connected && (
        <ConnectedBanner
          provider={connected.provider}
          warning={connected.warning}
          onDismiss={() => setConnected(null)}
        />
      )}
      {body()}
      {/* An earlier chat is read-only: its composer is an empty, disabled
          shell under the notice, with no draft and no attachments. */}
      {view === 'chat' && historical && <Composer
        key={`composer-historical-${viewedId}`}
        disabled
        disabledReason=""
        attachable={false}
        streaming={false}
        attachments={[]}
        notice={<>{blockedAlert}{projectUpdates}</>}
        onSend={() => {}}
        onStop={() => {}}
        onAttachFiles={() => {}}
        onRemoveAttachment={() => {}}
      />}
      {!historical && state.navigation.focused && notConfigured && blockedAlert}
      {!historical && !(state.navigation.focused && notConfigured) && <div hidden={view === 'setup'}><Composer
        key={`composer-${state.context?.sessionId}-${viewedId}`}
        disabled={unavailable || state.projectChatBlocked}
        disabledReason={state.projectChatBlocked ? 'Project restoration needs recovery.' : notConfigured ? 'ask, or steer this chat…' : unavailable ? 'The Agent is not available' : undefined}
        attachable={!state.projectChatBlocked}
        notice={blockedAlert || undefined}
        placeholder={state.navigation.focused ? 'Ask about this filament or request a change…' : undefined}
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
      /></div>}
      {confirmChatRestore && historical && <Dialog title="Restore this chat's project setup?"
        onClose={() => setConfirmChatRestore(false)}>
        <p>Restore “{viewedChat?.title || 'this chat'}” from {state.chatResume.savedAt || 'its saved checkpoint'}? Its earlier model and settings will replace the working version, with the matching intent and plan. Your current version will remain in history.</p>
        <div className="chat-dialog-buttons">
          <button type="button" onClick={() => setConfirmChatRestore(false)}>Cancel</button>
          <button type="button" className="primary" disabled={busy} onClick={() => {
            setConfirmChatRestore(false);
            client.send('restore_conversation', {
              conversationId: viewedId, activeConversationId: state.activeConversationId, docRevision: state.docRevision,
            });
          }}>Restore and resume</button>
        </div>
      </Dialog>}
      </div>
    </div>
  );
}
