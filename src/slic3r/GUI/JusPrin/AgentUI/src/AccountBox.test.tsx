// The server's box on Preferences › Account against a scripted host: when it
// is asked for, that nothing shows without an answer or before the frame is
// measured, and that the frame runs no scripts. jsdom loads no frame content,
// so whether a script inside really stays dead is checked in WebKit by the
// shell harness (preferences_account_box_runs_no_script).

import { describe, expect, it } from 'vitest';
import { act, fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PreferencesApp } from './PreferencesApp';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION } from './bridge/protocol';

const BOX = '<!doctype html><div>Double credits on everything</div>';

class MockHost {
  received: Envelope[] = [];
  transport = { post: (json: string) => { this.received.push(JSON.parse(json)); } };

  deliver(type: string, payload: unknown): void {
    const envelope: Envelope = { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: `h-${this.received.length}`, type, payload };
    act(() => { window.__jusprinBridge!.deliver(envelope); });
  }

  asked(): number {
    return this.received.filter((envelope) => envelope.type === 'refresh_account_box').length;
  }
}

function open(keys: { provider: string }[] = []) {
  const host = new MockHost();
  render(<PreferencesApp getTransport={() => host.transport} />);
  host.deliver('hello_ack', { version: PROTOCOL_VERSION, agent: { status: 'unavailable' }, appearance: 'light' });
  host.deliver('setup_keys', { keys: keys.map((key) => ({ hint: '…wxyz', rejected: false, ...key })) });
  host.deliver('state', {
    agent: { status: 'unavailable' }, appearance: 'light', conversations: [], activeConversationId: '', conversation: [],
    streamingMessageId: null, context: null,
    session: {
      kind: 'preferences', section: 'account',
      appearance: { language: 'en', languages: [{ code: 'en', name: 'English' }], theme: 'light', units: 'mm', restartFor: null },
      view3d: { navigation: 'mouse', zoomToPointer: true, reverseZoom: false, freeCamera: false, leftDrag: 'rotate', middleDrag: 'pan', rightDrag: 'pan' },
      projects: { versionsFolder: '', downloadPath: '', associations: true, open3mf: true, openModels: false, openLinks: false },
      updates: { version: '2.0.0', status: 'notChecked', checked: null, available: null, checkUpdates: true },
      agent: { newChatProvider: 'openai', newChatChoices: [] },
    },
  });
  return host;
}

const frame = () => document.querySelector<HTMLIFrameElement>('iframe.prefs-box');

// What the frame's content measures once WebKit has laid it out.
function load(height: number) {
  const box = frame()!;
  box.contentDocument!.documentElement.getBoundingClientRect = () => ({ height }) as DOMRect;
  fireEvent.load(box);
}

// jsdom loads a blank page into every frame a moment later, which this page
// takes for the content loading; let that pass inside act.
const settle = () => act(() => new Promise((resolve) => setTimeout(resolve, 0)));

describe('the account box', () => {
  it('runs no scripts: its frame allows same-origin reads and nothing else', async () => {
    const host = open();
    host.deliver('account_box', { html: BOX });
    // A frame that may run scripts gets the app's bridge (window.wx), so this
    // must stay exactly one permission, never with allow-scripts beside it.
    expect(frame()!.getAttribute('sandbox')).toBe('allow-same-origin');
    expect(frame()!.srcdoc).toBe(BOX);
    await settle();
  });

  it('is asked for when Account opens and shows only once it is measured', async () => {
    const host = open();
    expect(host.asked()).toBe(1);
    expect(frame()).toBeNull();

    host.deliver('account_box', { html: BOX });
    // Loading: out of the column and hidden, so it leaves no gap.
    expect(frame()).toHaveAttribute('data-measuring');
    load(86.4);
    expect(frame()).not.toHaveAttribute('data-measuring');
    expect(frame()!.style.height).toBe('87px');
    await settle();
  });

  it('shows nothing when the server sends nothing or cannot be asked', () => {
    const host = open();
    host.deliver('account_box', { html: BOX });
    load(86);
    host.deliver('account_box', { html: null });
    expect(frame()).toBeNull();
  });

  it('is never asked for or shown on the person\'s own API key', () => {
    const host = open([{ provider: 'openai' }]);
    expect(host.asked()).toBe(0);
    host.deliver('account_box', { html: BOX });
    expect(frame()).toBeNull();
  });

  it('is asked for again after a sign-in, on coming back, and each time Account opens', async () => {
    const host = open();
    expect(host.asked()).toBe(1);
    host.deliver('setup_keys', { keys: [{ provider: 'jusprin', hint: '…wxyz', rejected: false, account: 'alex@example.com' }] });
    expect(host.asked()).toBe(2);
    act(() => { window.dispatchEvent(new Event('focus')); });
    expect(host.asked()).toBe(3);
    await userEvent.click(screen.getByRole('button', { name: 'AI' }));
    await userEvent.click(screen.getByRole('button', { name: 'Account' }));
    expect(host.asked()).toBe(4);
  });
});
