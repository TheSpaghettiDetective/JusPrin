import { useEffect, useRef, useState } from 'react';
import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import { McpCatalogPayload, McpPreviewPayload, McpStatusPayload, McpToolInfo } from '../bridge/protocol';
import { ToolGroup } from './AgentConnection';

// Going back inside the panel: the quiet chevron, then the screen's own name,
// which is the title and not part of the button.
function SetupScreenTitle({ label, onBack }: { label: string; onBack: () => void }) {
  return (
    <div className="setup-title-row">
      <button type="button" className="chat-icon chat-back" onClick={onBack} aria-label={t`Back to setup options`}
        title={t`Back to setup options`}>
        <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" />
      </button>
      <h1 className="setup-title">{label}</h1>
    </div>
  );
}

type Step = 'pick' | 'prepare' | 'review' | 'saving' | 'saved';

export interface SetupLocalToolProps {
  catalog: McpCatalogPayload | null;
  preview: McpPreviewPayload | null;
  status: McpStatusPayload;
  onRefresh: () => void;
  onPreview: (toolId: string) => void;
  onConnect: (toolId: string) => void;
  onReveal: (toolId: string) => void;
  onBack: () => void;
  onDone: () => void;
  // Opens on this tool's own screen, as Preferences does from its row; that
  // screen's way back is then onBack, since the list it came from is not here.
  startTool?: string;
}

function errorCopy(status: McpStatusPayload, tool?: McpToolInfo, path?: string): { title: string; body: string } {
  const code = status.error?.code ?? '';
  const name = tool?.name;
  if (code === 'helper_missing')
    return { title: t`JusPrin's connection helper is missing.`, body: t`Reinstall or rebuild JusPrin, then try again.` };
  if (code === 'cli_missing')
    return {
      title: name ? t`${name} isn’t installed.` : t`This tool isn’t installed.`,
      body: t`Install it, or copy the command and run it later.`,
    };
  if (code === 'stale_preview')
    return { title: t`The file changed since you reviewed it.`, body: t`Look at the change again before saving.` };
  if (code === 'timeout')
    return { title: t`Timed out.`, body: t`Settings might have changed; inspect them before retrying.` };
  if (code === 'write_failed')
    return {
      title: path ? t`Can’t write ${path}.` : t`Can’t write the settings file.`,
      body: t`The file is invalid or not writable. Fix it, then try again.`,
    };
  return { title: t`The setup command failed.`, body: status.error?.message ?? status.diagnostic ?? t`Try again.` };
}

// The error lives on the screen that produced it, in the slot above the
// buttons, so the command or the diff the user was reading stays visible.
// Each code carries the one way out the design gives it.
function SetupInlineError({
  status,
  tool,
  path,
  onReveal,
  onReviewAgain,
}: {
  status: McpStatusPayload;
  tool?: McpToolInfo;
  path?: string;
  onReveal: (toolId: string) => void;
  onReviewAgain: (toolId: string) => void;
}) {
  const [details, setDetails] = useState(false);
  const copy = errorCopy(status, tool, path);
  const code = status.error?.code ?? '';
  const diagnostic = status.diagnostic ?? '';
  // A failed CLI reports the same string as the body text and as the raw
  // output. It belongs in the disclosure, not twice on the screen.
  const bodyIsDiagnostic = diagnostic !== '' && diagnostic === copy.body;
  return (
    <div className="setup-error-slot">
      <div className="setup-error" role="alert" data-testid="setup-local-error">
        <strong>{copy.title}</strong>
        {!bodyIsDiagnostic && <p>{copy.body}</p>}
        {tool && (code === 'write_failed' || code === 'timeout') && (
          <button type="button" className="link" onClick={() => onReveal(tool.id)}>
            {t`Show file`}
          </button>
        )}
        {tool && code === 'cli_missing' && (
          <button type="button" className="link" onClick={() => copyText(tool.text)}>
            {t`Copy`}
          </button>
        )}
        {tool && code === 'stale_preview' && (
          <button type="button" className="link" onClick={() => onReviewAgain(tool.id)}>
            {t`Review again`}
          </button>
        )}
      </div>
      {diagnostic && (
        <div className="setup-error-details">
          <button type="button" onClick={() => setDetails(!details)}>
            <span className={`jp-icon jp-icon-chevron-${details ? 'down' : 'right'}`} aria-hidden="true" /> {t`Technical output`}
          </button>
          {details && <pre>{diagnostic}</pre>}
        </div>
      )}
    </div>
  );
}

async function copyText(text: string) {
  await navigator.clipboard.writeText(text);
}

export function SetupLocalTool({
  catalog,
  preview,
  status,
  onRefresh,
  onPreview,
  onConnect,
  onReveal,
  onBack,
  onDone,
  startTool,
}: SetupLocalToolProps) {
  const started = startTool ? catalog?.tools.find((item) => item.id === startTool) : undefined;
  const [step, setStep] = useState<Step>(started ? (started.cli ? 'prepare' : 'review') : 'pick');
  const [selectedId, setSelectedId] = useState<string | null>(started?.id ?? null);
  const [advanced, setAdvanced] = useState(false);
  const [copied, setCopied] = useState(false);
  const [awaitingStatus, setAwaitingStatus] = useState(false);
  const [error, setError] = useState<McpStatusPayload | null>(null);
  // Each status envelope is acted on once. Without this, dismissing an error
  // and starting another attempt would re-read the same payload and put the
  // banner straight back. The status this screen opens on belongs to an
  // earlier attempt (Preferences opens it afresh for each tool), so it counts
  // as handled already.
  const handled = useRef<McpStatusPayload | null>(status);
  // The screen a connect was dispatched from. A failing connect reports
  // 'writing' before it reports the error, so by then the user is on the
  // progress screen -- which has no error slot and no way out.
  const origin = useRef<Step>('pick');

  useEffect(() => {
    onRefresh();
    if (started && !started.cli) onPreview(started.id);
    // The catalog is requested once when this surface opens.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (handled.current === status) return;
    handled.current = status;
    if (status.phase === 'error') {
      // An error does not navigate the user onward; it puts them back where
      // they were, with the command or the diff still in front of them.
      setAwaitingStatus(false);
      setError(status);
      setStep((current) => (current === 'saving' ? origin.current : current));
      return;
    }
    setError(null);
    if (!awaitingStatus) return;
    if (status.phase === 'writing') setStep('saving');
    if (status.phase === 'saved') {
      setAwaitingStatus(false);
      setStep('saved');
    }
  }, [status, awaitingStatus]);

  const tool = catalog?.tools.find((item) => item.id === selectedId);
  const toList = () => (startTool ? onBack() : setStep('pick'));

  const choose = (item: McpToolInfo) => {
    setSelectedId(item.id);
    setCopied(false);
    setAdvanced(false);
    setError(null);
    if (item.cli) setStep('prepare');
    else {
      onPreview(item.id);
      setStep('review');
    }
  };

  const connect = (toolId: string) => {
    origin.current = step;
    setError(null);
    setAwaitingStatus(true);
    onConnect(toolId);
  };

  const copyCommand = async (text: string) => {
    await copyText(text);
    setCopied(true);
  };

  const reviewAgain = (toolId: string) => {
    setError(null);
    onPreview(toolId);
  };

  const errorSlot = error ? (
    <SetupInlineError
      status={error}
      tool={tool}
      path={preview?.path ?? tool?.configPath}
      onReveal={onReveal}
      onReviewAgain={reviewAgain}
    />
  ) : null;

  if (step === 'saving') {
    return (
      <div className="pane-state setup" data-testid="setup-local-saving">
        <h1>{tool?.name ?? t`AI tool`}</h1>
        <p>{t`Writing settings…`}</p>
        <div className="setup-progress" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={40}>
          <span style={{ width: '40%' }} />
        </div>
        <p className="footnote">{t`Can’t be stopped once started; it takes a moment.`}</p>
      </div>
    );
  }

  if (step === 'saved') {
    const name = tool?.name;
    const backup = status.backup;
    return (
      <div className="pane-state setup" data-testid="setup-local-saved">
        <p className="setup-kicker">
          <span className="jp-icon jp-icon-circle-check" aria-hidden="true" />
          {t`Connected`}
        </p>
        <h1>{name ? t`${name} can see this project` : t`AI tool can see this project`}</h1>
        {backup && (
          <p className="setup-path">
            <Trans>Backup: <span>{backup}</span></Trans>
          </p>
        )}
        <p className="setup-eyebrow">{t`What to expect`}</p>
        <ul className="setup-expect">
          <li>{name
            ? t`Keep JusPrin open while ${name} uses it — the helper doesn’t launch it.`
            : t`Keep JusPrin open while the AI tool uses it — the helper doesn’t launch it.`}</li>
          <li>{t`You won’t need to set this up again after a normal JusPrin restart.`}</li>
        </ul>
        <p className="setup-eyebrow">{t`What to do next`}</p>
        <ol className="setup-expect">
          <li>{name ? t`Restart ${name} if it was already running.` : t`Restart the AI tool if it was already running.`}</li>
          <li>{t`Ask it about the open project.`}</li>
          <li>{t`Review its progress and results in this panel.`}</li>
        </ol>
        <button className="primary" onClick={onDone}>
          {t`Done`}
        </button>
      </div>
    );
  }

  if (step === 'prepare' && tool) {
    const name = tool.name;
    return (
      <div className="pane-state setup" data-testid="setup-local-prepare">
        <SetupScreenTitle label={tool.name} onBack={toList} />
        <p>{t`JusPrin can run this for you, or you can copy it and run it yourself.`}</p>
        <div className="setup-command">
          <pre>{tool.text}</pre>
          <button type="button" className="link" onClick={() => copyCommand(tool.text)}>
            {copied ? t`Copied` : t`Copy`}
          </button>
        </div>
        <p className="setup-manual">{t`Running it yourself? Paste it in a terminal, then restart ${name}.`}</p>
        <p className="footnote">
          {t`Keep JusPrin open while you use ${name}; the helper doesn’t launch it. Restarting JusPrin later won’t need this again.`}
        </p>
        {errorSlot}
        <div className="setup-actions">
          <button className="primary" onClick={() => connect(tool.id)} disabled={!catalog?.helperPresent}>
            {t`Connect…`}
          </button>
          <button onClick={toList}>{t`Close`}</button>
        </div>
        <div className="setup-advanced">
          <button type="button" onClick={() => setAdvanced(!advanced)}>
            <span className={`jp-icon jp-icon-chevron-${advanced ? 'down' : 'right'}`} aria-hidden="true" /> {t`Advanced / developer details`}
          </button>
          {advanced && (
            <>
              <p>{t`Live URL (developer only — this can change after restart)`}</p>
              <pre>{catalog?.liveUrl || t`Not listening yet`}</pre>
              {catalog?.startupError && <p className="setup-error-inline">{catalog.startupError}</p>}
            </>
          )}
        </div>
      </div>
    );
  }

  if (step === 'review') {
    return (
      <div className="pane-state setup" data-testid="setup-local-review">
        <SetupScreenTitle label={tool?.name ?? t`Review`} onBack={toList} />
        <p className="setup-consent">
          {t`Allow this AI tool to read and change the open project?`}
        </p>
        <p className="setup-path">{preview?.path ?? tool?.configPath}</p>
        <p className="setup-eyebrow">{t`JusPrin will edit`}</p>
        <div className="setup-diff">
          {preview?.previous === undefined ? (
            // Nothing to compare against on a first connection. Say so, rather
            // than printing an empty object the reader has to interpret.
            <p className="setup-diff-absent">{t`No JusPrin entry yet — this adds one.`}</p>
          ) : (
            <pre className="setup-diff-before">{preview.previous}</pre>
          )}
          <pre className="setup-diff-after">{preview?.next || ''}</pre>
        </div>
        <p className="footnote">{t`Your other settings stay as they are. The existing file is backed up first.`}</p>
        {errorSlot}
        <div className="setup-actions">
          <button
            className="primary"
            onClick={() => selectedId && connect(selectedId)}
            disabled={!preview || !catalog?.helperPresent}
          >
            {t`Connect`}
          </button>
          <button onClick={toList}>{t`Cancel`}</button>
        </div>
      </div>
    );
  }

  return (
    <div className="pane-state setup" data-testid="setup-local-tools">
      <SetupScreenTitle label={t`Connect an AI tool you already use`} onBack={onBack} />
      <ToolGroup tools={catalog?.tools ?? []} onConnect={choose} onLookAgain={onRefresh} />
    </div>
  );
}
