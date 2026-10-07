// Shared by the visual review pages (*.preview.test.tsx): the production
// stylesheet as the WebView gets it, the token variables of both appearances,
// and a way to mount the real components -- or the real App against a
// scripted host -- and keep the markup they end up with.
//
// Nothing here is in the production bundle: only test files import it.

import { ReactElement } from 'react';
import { act, render } from '@testing-library/react';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { App, AppProps } from '../App';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from '../bridge/protocol';
import { applyAppearance, applyStaticTokens } from '../tokens';

const resources = resolve(__dirname, '../../../../../../../resources');

export const escapeHtml = (value: string) => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;').replaceAll('"', '&quot;');

// Vite inlines the icon masks and the fonts into the single-file bundle, so a
// review page inlines them too.
export function productionCss(): string {
  return readFileSync(resolve(__dirname, '../styles.css'), 'utf8')
    .replace(/url\('@resources\/([^']+\.svg)'\)/g, (_, path) =>
      `url("data:image/svg+xml;base64,${readFileSync(resolve(resources, path)).toString('base64')}")`)
    .replace(/url\('(\.\.\/\.\.\/WebShared\/fonts\/[^']+\.woff)'\)/g, (_, path) =>
      `url("data:font/woff;base64,${readFileSync(resolve(__dirname, '..', path)).toString('base64')}")`);
}

// The variables exactly as the page writes them at startup and on an
// appearance change.
export function tokenVariables(): { light: string; dark: string } {
  applyStaticTokens();
  applyAppearance('dark');
  const dark = document.documentElement.style.cssText;
  applyAppearance('light');
  const light = document.documentElement.style.cssText;
  return { light, dark };
}

// innerHTML does not carry what was typed into a field or which element has
// the focus, so both are written into the markup before it is kept.
export function markup(root: Element): string {
  root.querySelectorAll('input').forEach((input) => {
    if (input.type !== 'file' && input.type !== 'password') input.setAttribute('value', input.value);
  });
  root.querySelectorAll('textarea').forEach((area) => { area.textContent = area.value; });
  if (document.activeElement && root.contains(document.activeElement))
    document.activeElement.setAttribute('data-visual-focus', '');
  return root.innerHTML;
}

// One component state, after whatever the case did to reach it.
export async function mounted(element: ReactElement, after?: (root: HTMLElement) => Promise<void> | void): Promise<string> {
  const { container, unmount } = render(element);
  if (after) await after(container);
  const html = markup(container);
  unmount();
  return html;
}

export interface ScriptedHost {
  root: HTMLElement;
  sent: Envelope[];
  deliver: (type: string, payload: unknown) => void;
}

// The real App against a host that plays back the given state. `state` null
// leaves the handshake unanswered, which is the connecting pane.
export async function mountedApp(
  state: StatePayload | null,
  options: { props?: Partial<AppProps>; after?: (host: ScriptedHost) => Promise<void> | void; transport?: boolean } = {},
): Promise<string> {
  const sent: Envelope[] = [];
  const transport = { post: (json: string) => { sent.push(JSON.parse(json)); } };
  const { container, unmount } = render(
    <App getTransport={() => (options.transport === false ? null : transport)} draftDebounceMs={0} {...options.props} />,
  );
  let counter = 0;
  const deliver = (type: string, payload: unknown) => {
    counter += 1;
    const envelope: Envelope = { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: `visual-${counter}-${type}`, type, payload };
    act(() => window.__jusprinBridge!.deliver(envelope));
  };
  if (state) {
    deliver('hello_ack', { version: PROTOCOL_VERSION, agent: state.agent, appearance: state.appearance });
    deliver('state', state);
  }
  if (options.after) await options.after({ root: container, sent, deliver });
  const html = markup(container);
  unmount();
  return html;
}

// One named state of one approved Figma matrix.
export interface VisualCase {
  // The matrix's Figma node, as in the design file's URL (for example 1483:344).
  node: string;
  matrix: string;
  id: string;
  name: string;
  // 'card' is one thread item at the thread's content width; 'block' is a
  // region that spans the dock, such as the composer; 'pane' is the whole
  // Agent pane at the dock's width.
  frame: 'card' | 'block' | 'pane';
  // The classes the card sits inside in the app, so its scoped rules apply.
  within?: string;
  // What must be in the markup for the state to count as rendered.
  expects: string[];
  build: () => Promise<string> | string;
}
