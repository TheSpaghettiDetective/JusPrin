import { beforeEach, describe, expect, it, vi } from 'vitest';
import { BridgeClient, ConnectionState, Transport } from './client';
import type { PageErrorReport } from '@shared/errorReports';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION } from './protocol';

interface Recorded {
  envelopes: Envelope[];
  transport: Transport;
}

function recordingTransport(): Recorded {
  const envelopes: Envelope[] = [];
  return {
    envelopes,
    transport: {
      post(json: string) {
        envelopes.push(JSON.parse(json));
      },
    },
  };
}

function hostEnvelope(type: string, payload: unknown = {}): Envelope {
  return { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: 'h-test', type, payload };
}

describe('BridgeClient', () => {
  let states: { state: ConnectionState; detail?: string }[];
  let received: Envelope[];

  beforeEach(() => {
    states = [];
    received = [];
  });

  function makeClient(transport: Transport | null, timeouts?: { fire: () => void }) {
    let pending: (() => void) | null = null;
    const client = new BridgeClient({
      getTransport: () => transport,
      onEnvelope: (envelope) => received.push(envelope),
      onConnectionChange: (state, detail) => states.push({ state, detail }),
      handshakeTimeoutMs: 10,
      transportRetryLimit: 0,
      scheduleTimeout: (fn) => {
        pending = fn;
        return 1;
      },
      cancelTimeout: () => {
        pending = null;
      },
    });
    if (timeouts) timeouts.fire = () => pending?.();
    return client;
  }

  it('sends a versioned hello on start and connects on hello_ack', () => {
    const { envelopes, transport } = recordingTransport();
    const client = makeClient(transport);
    client.start();

    expect(envelopes).toHaveLength(1);
    expect(envelopes[0].protocol).toBe(PROTOCOL_NAME);
    expect(envelopes[0].version).toBe(PROTOCOL_VERSION);
    expect(envelopes[0].type).toBe('hello');
    expect((envelopes[0].payload as { protocolVersions: number[] }).protocolVersions).toEqual([PROTOCOL_VERSION]);
    expect(states.at(-1)?.state).toBe('connecting');

    client.deliver(hostEnvelope('hello_ack', { version: PROTOCOL_VERSION }));
    expect(states.at(-1)?.state).toBe('connected');
    expect(received.at(-1)?.type).toBe('hello_ack');
  });

  it('reports the incompatible state on hello_reject', () => {
    const { transport } = recordingTransport();
    const client = makeClient(transport);
    client.start();
    client.deliver(hostEnvelope('hello_reject', { supportedVersions: [2], message: 'version 2 only' }));
    expect(states.at(-1)?.state).toBe('incompatible');
    expect(states.at(-1)?.detail).toBe('version 2 only');
  });

  it('reports no-transport when the native handler is absent', () => {
    const client = makeClient(null);
    client.start();
    expect(states.at(-1)?.state).toBe('no-transport');
  });

  it('finds a transport that appears after startup (late window.wx injection)', () => {
    const { envelopes, transport } = recordingTransport();
    let available: Transport | null = null;
    let pending: (() => void) | null = null;
    const client = new BridgeClient({
      getTransport: () => available,
      onEnvelope: (envelope) => received.push(envelope),
      onConnectionChange: (state, detail) => states.push({ state, detail }),
      transportRetryLimit: 5,
      scheduleTimeout: (fn) => {
        pending = fn;
        return 1;
      },
      cancelTimeout: () => {
        pending = null;
      },
    });
    client.start();
    expect(states.at(-1)?.state).toBe('connecting');
    expect(envelopes).toHaveLength(0);

    available = transport; // the native side injects window.wx late
    pending!();
    expect(envelopes.filter((e) => e.type === 'hello')).toHaveLength(1);
  });

  it('reports a timeout when the host never answers, and retry reconnects', () => {
    const { envelopes, transport } = recordingTransport();
    const timeouts = { fire: () => {} };
    const client = makeClient(transport, timeouts);
    client.start();
    timeouts.fire();
    expect(states.at(-1)?.state).toBe('timeout');

    client.retry();
    expect(envelopes.filter((e) => e.type === 'hello')).toHaveLength(2);
    client.deliver(hostEnvelope('hello_ack', {}));
    expect(states.at(-1)?.state).toBe('connected');
  });

  it('assigns unique page envelope IDs', () => {
    const { envelopes, transport } = recordingTransport();
    const client = makeClient(transport);
    client.start();
    client.send('user_message', { clientMessageId: 'c-1', text: 'hi' });
    client.send('user_message', { clientMessageId: 'c-2', text: 'again' });
    const ids = envelopes.map((e) => e.id);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it('ignores messages that are not envelopes of this protocol', () => {
    const { transport } = recordingTransport();
    const client = makeClient(transport);
    client.start();
    client.deliver({ protocol: 'someone-else', version: 9, id: 'x', type: 'state', payload: {} });
    client.deliver('garbage');
    expect(received).toHaveLength(0);
  });

  // What the browser calls for an error or a rejection nothing caught; the
  // page's Sentry SDK installs both handlers. (jsdom calls neither for an
  // event dispatched by hand.)
  function uncaught(error: Error): void {
    window.onerror?.(error.message, 'app:///index.html', 1, 1, error);
  }
  function unhandled(reason: Error): void {
    window.onunhandledrejection?.(Object.assign(new Event('unhandledrejection'), { reason }) as PromiseRejectionEvent);
  }

  it('sends an error the page did not catch to the app with its Sentry event, holding one from before the handshake', async () => {
    const { envelopes, transport } = recordingTransport();
    const client = makeClient(transport);
    client.start();
    // What the browser does when nothing caught an error.
    uncaught(new TypeError('x is undefined'));
    await vi.waitFor(() => expect(client['unsentErrors']).toHaveLength(1));
    expect(envelopes.map((envelope) => envelope.type)).toEqual(['hello']);

    client.deliver(hostEnvelope('hello_ack'));
    const sent = envelopes.filter((envelope) => envelope.type === 'report_error');
    expect(sent).toHaveLength(1);
    const report = sent[0].payload as PageErrorReport;
    expect(report.error).toBe('TypeError: x is undefined');
    expect(report.where).toContain('client.test.ts');
    const exception = report.event.exception?.values?.at(-1);
    expect(exception?.type).toBe('TypeError');
    expect(exception?.stacktrace?.frames?.length).toBeGreaterThan(0);
    expect(report.event.sdk?.name).toBe('sentry.javascript.browser');
    expect(report.event).not.toHaveProperty('sdkProcessingMetadata');
    // The build its source map is filed under in Sentry: none outside the release workflow.
    expect(report.event.dist).toBe('local');

    // A rejected promise nothing handled goes the same way.
    unhandled(new RangeError('out of range'));
    await vi.waitFor(() => expect(envelopes.filter((envelope) => envelope.type === 'report_error')).toHaveLength(2));
    expect((envelopes.at(-1)?.payload as PageErrorReport).error).toBe('RangeError: out of range');
  });

  it('sends the steps the SDK recorded before an error with its report', async () => {
    const { envelopes, transport } = recordingTransport();
    const client = makeClient(transport);
    client.start();
    client.deliver(hostEnvelope('hello_ack'));
    const button = document.createElement('button');
    button.className = 'project-card';
    document.body.append(button);
    button.click();
    console.log('opened the project list');
    uncaught(new Error('after a click'));
    await vi.waitFor(() => expect(envelopes.filter((envelope) => envelope.type === 'report_error')).not.toHaveLength(0));
    const report = envelopes.filter((envelope) => envelope.type === 'report_error').at(-1)?.payload as PageErrorReport;
    const steps = JSON.stringify(report.event.breadcrumbs);
    expect(steps).toContain('button.project-card');
    expect(steps).toContain('opened the project list');
    button.remove();
  });
});
