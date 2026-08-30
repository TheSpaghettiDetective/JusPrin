import { beforeEach, describe, expect, it } from 'vitest';
import { BridgeClient, ConnectionState, Transport } from './client';
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
});
