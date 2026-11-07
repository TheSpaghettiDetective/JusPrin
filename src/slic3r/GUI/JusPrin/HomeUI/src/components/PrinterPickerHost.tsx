// The Agent page's printer fields (Brand, Model, Nozzle, the printers found on
// the network), drawn on Home's first-launch printer screen. The fields and
// their stylesheet belong to the Agent page, whose class names overlap
// Home's (.app, the buttons), so they render in a shadow root that holds the
// Agent stylesheet alone. The design tokens are custom properties on the
// document, which a shadow root inherits.

import { useEffect, useRef } from 'react';
import type { ReactNode } from 'react';
import { createRoot } from 'react-dom/client';
import type { Root } from 'react-dom/client';
import agentStyles from '../../../AgentUI/src/styles.css?inline';
import { PrinterPicker } from '../../../AgentUI/src/components/PrinterPicker';
import type { PrinterPick } from '../../../AgentUI/src/components/PrinterPicker';
import type { PrinterFormInfo } from '../../../AgentUI/src/bridge/protocol';
import { applyStaticTokens as applyAgentTokens } from '../../../AgentUI/src/tokens';

// The fields sit in the add-printer dialog's box, for its type and spacing;
// here the box is the screen's column, with no card of its own.
// The note under the nozzles turns into the spinner while the app reads the
// chosen printer's settings, which can take a while.
const IN_FLOW = `.chat-dialog.onboarding-picker { max-width: none; padding: 0; border-radius: 0; background: none; box-shadow: none; }
.chat-dialog p.printer-form-note.onboarding-reading { display: flex; align-items: center; gap: 8px; }`;

interface Props {
  form: PrinterFormInfo;
  pick: PrinterPick;
  onPick: (pick: PrinterPick) => void;
  // "Can't find your printer? Ask the agent", under the menus.
  onAskAgent?: () => void;
  // The app is reading the chosen printer's settings.
  reading?: boolean;
}

export function PrinterPickerHost({ form, pick, onPick, onAskAgent, reading }: Props) {
  const host = useRef<HTMLDivElement>(null);
  const root = useRef<Root>();

  useEffect(() => {
    // The fields' own variables, such as their menus' sizes.
    applyAgentTokens();
    const shadow = host.current!.shadowRoot ?? host.current!.attachShadow({ mode: 'open' });
    const style = document.createElement('style');
    style.textContent = `${agentStyles}\n${IN_FLOW}`;
    const mount = document.createElement('div');
    shadow.replaceChildren(style, mount);
    root.current = createRoot(mount);
    return () => {
      const unmounting = root.current;
      root.current = undefined;
      // React refuses to unmount a root while it is rendering this one.
      queueMicrotask(() => unmounting?.unmount());
    };
  }, []);

  useEffect(() => {
    const askAgent: ReactNode = onAskAgent && <p className="printer-form-note">
      Can’t find your printer?{' '}
      <button type="button" className="panel-link-button" onClick={onAskAgent}>Ask AI</button>
    </p>;
    root.current?.render(<div className="chat-dialog printer-form onboarding-picker">
      <PrinterPicker form={form} pick={pick} onPick={onPick} underMenus={askAgent} nozzleUnit
        nozzleNote={reading ? (
          <p className="printer-form-note onboarding-reading" role="status">
            <span className="jp-icon jp-icon-rotate-cw turn-activity-spinner" aria-hidden="true" />
            Reading this printer’s settings…
          </p>
        ) : <p className="printer-form-note">Most printers ship with 0.4 mm.</p>} />
    </div>);
  });

  return <div ref={host} className="onboarding-printer-picker" />;
}
