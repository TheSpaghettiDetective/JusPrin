import { useEffect, useState } from 'react';
import { McpCatalogPayload, McpPreviewPayload, McpStatusPayload, McpToolInfo } from '../bridge/protocol';
import { SetupScreenTitle } from './Setup';

type Step = 'pick' | 'prepare' | 'review' | 'saving' | 'saved' | 'error';

export interface SetupLocalToolProps {
  catalog: McpCatalogPayload | null;
  preview: McpPreviewPayload | null;
  status: McpStatusPayload;
  onRefresh: () => void;
  onPreview: (toolId: string) => void;
  onConnect: (toolId: string) => void;
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
  onBack,
  onDone,
}: SetupLocalToolProps) {
  const [step, setStep] = useState<Step>('pick');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [advanced, setAdvanced] = useState(false);
  const [copied, setCopied] = useState(false);
  const [awaitingStatus, setAwaitingStatus] = useState(false);

  useEffect(() => {
    onRefresh();
    // The catalog is requested once when this surface opens.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (!awaitingStatus) return;
    if (status.phase === 'writing') setStep('saving');
    if (status.phase === 'saved') {
      setAwaitingStatus(false);
      setStep('saved');
    }
    if (status.phase === 'error') {
      setAwaitingStatus(false);
      setStep('error');
    }
  }, [status, awaitingStatus]);

  const tool = catalog?.tools.find((item) => item.id === selectedId);

  const choose = (item: McpToolInfo) => {
    setSelectedId(item.id);
    setCopied(false);
    setAdvanced(false);
    if (item.cli) setStep('prepare');
    else {
      onPreview(item.id);
      setStep('review');
    }
  };

  const copyCommand = async (text: string) => {
    await copyText(text);
    setCopied(true);
  };

  if (step === 'saving') {
    return (
      <div className="pane-state setup" data-testid="setup-local-saving">
        <h1>Writing settings…</h1>
        <p>JusPrin is saving the connection. This usually takes a second.</p>
        <div className="setup-progress" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={40}>
          <span style={{ width: '40%' }} />
        </div>
      </div>
    );
  }

  if (step === 'saved') {
    return (
      <div className="pane-state setup" data-testid="setup-local-saved">
        <p className="setup-kicker">
          <span className="setup-check" aria-hidden="true" />
          Connected
        </p>
        <h1>{tool?.name ?? 'AI tool'} can see this project</h1>
        <p>Open a chat there and ask it to work with the model in JusPrin. Changes still need your approval here.</p>
        {status.backup && (
          <p className="setup-path">
            Backup: <span>{status.backup}</span>
          </p>
        )}
        <p className="setup-eyebrow">What to expect</p>
        <ol className="setup-expect">
          <li>Restart the AI tool if it was already running.</li>
          <li>Ask it about the open project.</li>
          <li>Approve or reject proposals in this panel.</li>
        </ol>
        <button className="primary" onClick={onDone}>
          Done
        </button>
      </div>
    );
  }

  if (step === 'error') {
    const copy = errorCopy(status, tool, preview?.path ?? tool?.configPath);
    return (
      <div className="pane-state setup" data-testid="setup-local-error">
        <SetupScreenTitle label="Can’t connect" onBack={() => setStep('pick')} />
        <div className="setup-error" role="alert">
          <strong>{copy.title}</strong>
          <p>{copy.body}</p>
        </div>
        <div className="setup-actions">
          {tool?.cli && (
            <button className="link" onClick={() => tool && copyText(tool.text)}>
              Copy
            </button>
          )}
          <button onClick={() => setStep(tool?.cli ? 'prepare' : 'review')}>Review again</button>
        </div>
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
        <p className="footnote">
          Keep JusPrin open while you use {tool.name}; the helper doesn’t launch it. Restarting JusPrin later won’t need
          this again.
        </p>
        <div className="setup-actions">
          <button
            className="primary"
            onClick={() => {
              setAwaitingStatus(true);
              onConnect(tool.id);
            }}
            disabled={!catalog?.helperPresent}
          >
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
        <p>JusPrin will write only the JusPrin entry. Everything else in the file stays as it is.</p>
        <p className="setup-path">{preview?.path ?? tool?.configPath}</p>
        <p className="setup-eyebrow">JusPrin will edit</p>
        <div className="setup-diff">
          <pre className="setup-diff-before">{preview?.previous || '{}'}</pre>
          <pre className="setup-diff-after">{preview?.next || ''}</pre>
        </div>
        <p className="footnote">Other servers in this file stay unchanged.</p>
        <div className="setup-actions">
          <button
            className="primary"
            onClick={() => {
              if (selectedId) {
                setAwaitingStatus(true);
                onConnect(selectedId);
              }
            }}
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
