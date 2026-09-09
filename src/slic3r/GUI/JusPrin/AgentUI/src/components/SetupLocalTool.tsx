import { useEffect, useRef, useState } from 'react';
import { McpCatalogPayload, McpPreviewPayload, McpStatusPayload, McpToolInfo } from '../bridge/protocol';
import { SetupScreenTitle } from './Setup';

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
}

function errorCopy(status: McpStatusPayload, tool?: McpToolInfo, path?: string): { title: string; body: string } {
  const code = status.error?.code ?? '';
  const name = tool?.name ?? 'This tool';
  if (code === 'helper_missing')
    return { title: "JusPrin's connection helper is missing.", body: 'Reinstall or rebuild JusPrin, then try again.' };
  if (code === 'cli_missing')
    return {
      title: `${name} isn’t installed.`,
      body: 'Install it, or copy the command and run it later.',
    };
  if (code === 'stale_preview')
    return { title: 'The file changed since you reviewed it.', body: 'Look at the change again before saving.' };
  if (code === 'timeout')
    return { title: 'Timed out.', body: 'Settings might have changed; inspect them before retrying.' };
  if (code === 'write_failed')
    return {
      title: `Can’t write ${path ?? 'the settings file'}.`,
      body: 'The file is invalid or not writable. Fix it, then try again.',
    };
  return { title: 'The setup command failed.', body: status.error?.message ?? status.diagnostic ?? 'Try again.' };
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
            Show file
          </button>
        )}
        {tool && code === 'cli_missing' && (
          <button type="button" className="link" onClick={() => copyText(tool.text)}>
            Copy
          </button>
        )}
        {tool && code === 'stale_preview' && (
          <button type="button" className="link" onClick={() => onReviewAgain(tool.id)}>
            Review again
          </button>
        )}
      </div>
      {diagnostic && (
        <div className="setup-error-details">
          <button type="button" onClick={() => setDetails(!details)}>
            {details ? '▾' : '▸'} Technical output
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
}: SetupLocalToolProps) {
  const [step, setStep] = useState<Step>('pick');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [advanced, setAdvanced] = useState(false);
  const [copied, setCopied] = useState(false);
  const [awaitingStatus, setAwaitingStatus] = useState(false);
  const [error, setError] = useState<McpStatusPayload | null>(null);
  // Each status envelope is acted on once. Without this, dismissing an error
  // and starting another attempt would re-read the same payload and put the
  // banner straight back.
  const handled = useRef<McpStatusPayload | null>(null);
  // The screen a connect was dispatched from. A failing connect reports
  // 'writing' before it reports the error, so by then the user is on the
  // progress screen -- which has no error slot and no way out.
  const origin = useRef<Step>('pick');

  useEffect(() => {
    onRefresh();
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
        <h1>{tool?.name ?? 'AI tool'}</h1>
        <p>Writing settings…</p>
        <div className="setup-progress" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={40}>
          <span style={{ width: '40%' }} />
        </div>
        <p className="footnote">Can’t be stopped once started; it takes a moment.</p>
      </div>
    );
  }

  if (step === 'saved') {
    const name = tool?.name ?? 'the AI tool';
    return (
      <div className="pane-state setup" data-testid="setup-local-saved">
        <p className="setup-kicker">
          <span className="setup-check" aria-hidden="true" />
          Connected
        </p>
        <h1>{tool?.name ?? 'AI tool'} can see this project</h1>
        {status.backup && (
          <p className="setup-path">
            Backup: <span>{status.backup}</span>
          </p>
        )}
        <p className="setup-eyebrow">What to expect</p>
        <ul className="setup-expect">
          <li>Keep JusPrin open while {name} uses it — the helper doesn’t launch it.</li>
          <li>You won’t need to set this up again after a normal JusPrin restart.</li>
        </ul>
        <p className="setup-eyebrow">What to do next</p>
        <ol className="setup-expect">
          <li>Restart {name} if it was already running.</li>
          <li>Ask it about the open project.</li>
          <li>Approve or reject its proposals in this panel.</li>
        </ol>
        <button className="primary" onClick={onDone}>
          Done
        </button>
      </div>
    );
  }

  if (step === 'prepare' && tool) {
    return (
      <div className="pane-state setup" data-testid="setup-local-prepare">
        <SetupScreenTitle label={tool.name} onBack={() => setStep('pick')} />
        <p>JusPrin can run this for you, or you can copy it and run it yourself.</p>
        <div className="setup-command">
          <pre>{tool.text}</pre>
          <button type="button" className="link" onClick={() => copyCommand(tool.text)}>
            {copied ? 'Copied' : 'Copy'}
          </button>
        </div>
        <p className="setup-manual">Running it yourself? Paste it in a terminal, then restart {tool.name}.</p>
        <p className="footnote">
          Keep JusPrin open while you use {tool.name}; the helper doesn’t launch it. Restarting JusPrin later won’t need
          this again.
        </p>
        {errorSlot}
        <div className="setup-actions">
          <button className="primary" onClick={() => connect(tool.id)} disabled={!catalog?.helperPresent}>
            Connect…
          </button>
          <button onClick={() => setStep('pick')}>Close</button>
        </div>
        <div className="setup-advanced">
          <button type="button" onClick={() => setAdvanced(!advanced)}>
            {advanced ? '▾' : '▸'} Advanced / developer details
          </button>
          {advanced && (
            <>
              <p>Live URL (developer only — this can change after restart)</p>
              <pre>{catalog?.liveUrl || 'Not listening yet'}</pre>
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
        <SetupScreenTitle label={tool?.name ?? 'Review'} onBack={() => setStep('pick')} />
        <p className="setup-consent">
          Allow this AI tool to read the open project and propose changes? Changes still require approval inside
          JusPrin.
        </p>
        <p className="setup-path">{preview?.path ?? tool?.configPath}</p>
        <p className="setup-eyebrow">JusPrin will edit</p>
        <div className="setup-diff">
          {preview?.previous === undefined ? (
            // Nothing to compare against on a first connection. Say so, rather
            // than printing an empty object the reader has to interpret.
            <p className="setup-diff-absent">No JusPrin entry yet — this adds one.</p>
          ) : (
            <pre className="setup-diff-before">{preview.previous}</pre>
          )}
          <pre className="setup-diff-after">{preview?.next || ''}</pre>
        </div>
        <p className="footnote">Your other settings stay as they are. The existing file is backed up first.</p>
        {errorSlot}
        <div className="setup-actions">
          <button
            className="primary"
            onClick={() => selectedId && connect(selectedId)}
            disabled={!preview || !catalog?.helperPresent}
          >
            Connect
          </button>
          <button onClick={() => setStep('pick')}>Cancel</button>
        </div>
      </div>
    );
  }

  return (
    <div className="pane-state setup" data-testid="setup-local-tools">
      <SetupScreenTitle label="Connect an AI tool" onBack={onBack} />
      <p>Let an AI app on this computer work with the open project. JusPrin writes the connection settings; you don’t need to know what MCP is.</p>
      <div className="setup-tool-list" role="radiogroup" aria-label="AI tools">
        {(catalog?.tools ?? []).map((item) => (
          <label key={item.id} className="setup-tool-row">
            <input type="radio" name="mcp-tool" checked={selectedId === item.id} onChange={() => choose(item)} />
            <span className="setup-tool-copy">
              <span className="setup-tool-name">
                {item.name}
                {item.subtitle ? <span className="setup-tool-sub">{item.subtitle}</span> : null}
              </span>
              <span className={item.detected ? 'setup-detected' : 'setup-missing'}>
                {item.detected ? 'detected' : 'not found'}
              </span>
            </span>
          </label>
        ))}
      </div>
      <p className="footnote">
        “Detected” means JusPrin found the app or its settings file here. It doesn’t mean the app is signed in. You can
        pick one that wasn’t found.
      </p>
      <button type="button" onClick={onRefresh}>
        Refresh scan
      </button>
    </div>
  );
}
