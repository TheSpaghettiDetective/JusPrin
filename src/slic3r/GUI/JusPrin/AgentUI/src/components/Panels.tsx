// Full-pane and banner states. The Agent-unavailable state and bridge errors
// are deliberately different surfaces: the first is a clean product state for
// an unconfigured service, the second an internal connection failure with
// Retry and diagnostics.

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

export function BridgeErrorPane({ title, detail, diagnostics, onRetry }: BridgeErrorProps) {
  return (
    <div className="pane-state" data-testid="bridge-error" role="alert">
      <h1>{title}</h1>
      <p>This is an internal connection inside JusPrin, not a network service. The 3D canvas and all other controls keep working.</p>
      {detail && <p>{detail}</p>}
      <button className="primary" onClick={onRetry}>
        Retry
      </button>
      <details>
        <summary>Diagnostics</summary>
        <pre>{diagnostics.length > 0 ? diagnostics.join('\n') : 'No bridge messages recorded.'}</pre>
      </details>
    </div>
  );
}

export function AgentUnavailableNotice() {
  return (
    <div className="notice" data-testid="agent-unavailable">
      <h2>The Agent isn’t set up yet</h2>
      <p>
        This build does not include a configured Agent service. Your conversation and project stay saved, and you can
        keep preparing, slicing, and checking prints with the canvas and the controls above.
      </p>
      <p>
        Enabling a cloud Agent requires your consent to send your message, the current project summary—including the
        printer, plates, objects, and selection—and only the attachments you include. The API key stays in your
        system credential store.
      </p>
    </div>
  );
}

// The dock before any Agent service is configured. Connecting an Agent
// changes nothing about the print, so this state offers exactly one thing and
// leaves the ask box where it always is, inert — the dock keeps its shape
// whether or not an Agent is ever set up. It replaces the conversation
// chrome only while the conversation is empty; history carried in from a
// previously configured session stays visible behind the banner above.
export function AgentNotConfiguredHeader() {
  return (
    <div className="agent-header" data-testid="agent-not-configured-header">
      <span className="agent-title">Agent</span>
      <span className="agent-badge">NOT SET UP</span>
    </div>
  );
}

export function AgentNotConfiguredPane({ onSetUp }: { onSetUp: () => void }) {
  return (
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
}
