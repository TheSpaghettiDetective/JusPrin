// The Agent page's add-printer form, drawn on Home's Printers step. The form
// and its stylesheet belong to the Agent page, whose class names overlap
// Home's (.app, the buttons), so it renders in a shadow root that holds the
// Agent stylesheet alone. The design tokens are custom properties on the
// document, which a shadow root inherits.

import { useEffect, useRef } from 'react';
import { createRoot } from 'react-dom/client';
import type { Root } from 'react-dom/client';
import agentStyles from '../../../AgentUI/src/styles.css?inline';
import { AddPrinterForm } from '../../../AgentUI/src/components/AddPrinterForm';
import type { PrinterFormChoice } from '../../../AgentUI/src/components/AddPrinterForm';
import type { PrinterFormInfo } from '../../../AgentUI/src/bridge/protocol';
import { applyStaticTokens as applyAgentTokens } from '../../../AgentUI/src/tokens';

// In the printer panel the form is a dialog over the conversation; here it is
// the step's own card, in the page's flow, with Back and Skip under it.
const IN_FLOW = `.chat-dialog-shade { position: static; padding: 0; background: none; overflow: visible; }`;

interface Props {
  form: PrinterFormInfo;
  onAsk: (catalogId: string, nozzle: number) => void;
  onFinish: (choice: PrinterFormChoice) => void;
  onClose: () => void;
}

export function PrinterFormHost(props: Props) {
  const host = useRef<HTMLDivElement>(null);
  const root = useRef<Root>();

  useEffect(() => {
    // The form's own variables, such as its menus' and fields' sizes.
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
    root.current?.render(<AddPrinterForm {...props} />);
  });

  return <div ref={host} className="onboarding-printer-form" />;
}
