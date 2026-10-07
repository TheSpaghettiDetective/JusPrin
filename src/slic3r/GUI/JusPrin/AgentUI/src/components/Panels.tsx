// Full-pane states. An unconfigured Agent offers setup in the conversation
// body; a bridge error is an internal connection failure with diagnostics.

import type { FileReport } from '../bridge/protocol';
import { AgentPaneToggle } from './ChatNavigation';
import { FileNotesCard } from './FileNotesCard';

interface BridgeErrorProps {
  title: string;
  detail?: string;
  diagnostics: string[];
  onRetry: () => void;
}

export function ConnectingPane() {
  return (
    <div className="pane-state" data-testid="connecting">
      <h1>Connecting…</h1>
      <p>Starting the Agent panel.</p>
    </div>
  );
}

// The Agent pane's technical details: one disclosure, closed until asked for,
// whose content is the bridge's own log lines in the code face. `open` is the
// state it starts in.
export function Diagnostics({ lines, open = false }: { lines: string[]; open?: boolean }) {
  return (
    <details className="diagnostics" open={open}>
      <summary className="diagnostics-header">
        <span className="diagnostics-chevron" aria-hidden="true" />
        Diagnostics
      </summary>
      <pre className="diagnostics-code">{lines.length > 0 ? lines.join('\n') : 'No bridge messages recorded.'}</pre>
    </details>
  );
}

export function BridgeErrorPane({ title, detail, diagnostics, onRetry }: BridgeErrorProps) {
  return (
    <div className="pane-state" data-testid="bridge-error" role="alert">
      <h1>{title}</h1>
      <p>This is an internal connection inside JusPrin, not a network service. The 3D canvas and all other controls keep working.</p>
      {detail && <p className="pane-state-detail">{detail}</p>}
      <button className="primary" onClick={onRetry}>
        Retry
      </button>
      <Diagnostics lines={diagnostics} />
    </div>
  );
}

// The dock before any Agent service is configured. Connecting an Agent
// changes nothing about the print, so this state offers exactly one thing and
// leaves the ask box where it always is, inert — the dock keeps its shape
// whether or not an Agent is ever set up. It replaces the conversation
// chrome only while the conversation is empty; history carried in from a
// previously configured session stays visible in the ordinary chat.
export function AgentNotConfiguredHeader({ onCollapse }: { onCollapse: () => void }) {
  return (
    <div className="agent-header" data-testid="agent-not-configured-header">
      <span className="agent-title">Agent</span>
      <span className="agent-badge">NOT SET UP</span>
      <AgentPaneToggle onCollapse={onCollapse} />
    </div>
  );
}

export function AgentNotConfiguredPane({ onSetUp, fileReports }: {
  onSetUp: () => void;
  fileReports: { id: string; report: FileReport }[];
}) {
  const offer = (
    <div className="pane-state" data-testid="agent-not-configured">
      <h1>No agent connected</h1>
      <p>
        An agent turns what you want — <em>“make it strong, it’ll bear weight”</em> — into the hundreds of
        settings underneath, and shows you what it changed.
      </p>
      <p>Until then JusPrin slices from the preset, exactly as it always has.</p>
      <button className="primary" onClick={onSetUp}>
        Set up the agent
      </button>
      <p className="footnote">Registered JusPrin account, your own key, or an AI tool you already use.</p>
    </div>
  );
  if (fileReports.length === 0) return offer;
  return (
    <div className="agent-offer-with-notes">
      {fileReports.map(({ id, report }) => <FileNotesCard key={id} report={report} />)}
      {offer}
    </div>
  );
}
