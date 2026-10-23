// Full-pane states. An unconfigured Agent offers setup in the conversation
// body; a bridge error is an internal connection failure with diagnostics.

import { ReactNode, Ref, useLayoutEffect, useRef } from 'react';
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

export function AgentNotConfiguredPane({ onSetUp, fileReports, offerRef }: {
  onSetUp: () => void;
  fileReports: { id: string; report: FileReport }[];
  offerRef?: Ref<HTMLDivElement>;
}) {
  const offer = (
    <div className="pane-state" data-testid="agent-not-configured" ref={offerRef}>
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

// The offer, once the conversation has something to show and no agent is set
// up: in the composer's place under the thread, since there is nobody to type
// to. Arriving straight from the full offer (`from`, where that offer was),
// the offer shrinks into the button.
export function AgentSetupBar({ onSetUp, from, notice }: {
  onSetUp: () => void;
  from?: DOMRect | null;
  notice?: ReactNode;
}) {
  const button = useRef<HTMLButtonElement>(null);
  useLayoutEffect(() => {
    if (from && button.current) return shrinkInto(from, button.current);
    // Only the arrival animates.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  return (
    <div className="agent-setup-bar" data-testid="agent-setup-bar">
      {notice}
      <button ref={button} type="button" className="primary" onClick={onSetUp}>
        Set up the agent
      </button>
      <p>Chatting here needs an agent.</p>
    </div>
  );
}

// A box the offer's size and colour that moves onto the button, turning the
// button's colour on the way, and hands over to the real button at the end.
// Colours and time come from the token variables; reduced motion skips it.
function shrinkInto(from: DOMRect, target: HTMLElement): (() => void) | undefined {
  if (from.width === 0 || from.height === 0 || typeof target.animate !== 'function' ||
      window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) return undefined;
  const tokens = getComputedStyle(document.documentElement);
  const duration = parseFloat(tokens.getPropertyValue('--setup-offer-shrink-duration'));
  if (!(duration > 0)) return undefined;
  const to = target.getBoundingClientRect();
  const box = (rect: DOMRect) => ({ top: `${rect.top}px`, left: `${rect.left}px`, width: `${rect.width}px`, height: `${rect.height}px` });
  const ghost = document.createElement('div');
  ghost.className = 'agent-offer-ghost';
  document.body.appendChild(ghost);
  const moving = ghost.animate([
    { ...box(from), backgroundColor: tokens.getPropertyValue('--surface-subtle'), opacity: 1 },
    { ...box(to), backgroundColor: tokens.getPropertyValue('--action-primary'), opacity: 1, offset: 0.8 },
    { ...box(to), backgroundColor: tokens.getPropertyValue('--action-primary'), opacity: 0 },
  ], { duration, easing: 'ease-in-out', fill: 'forwards' });
  const arriving = target.animate([{ opacity: 0 }, { opacity: 0, offset: 0.8 }, { opacity: 1 }], { duration });
  moving.onfinish = () => ghost.remove();
  return () => { moving.cancel(); arriving.cancel(); ghost.remove(); };
}
