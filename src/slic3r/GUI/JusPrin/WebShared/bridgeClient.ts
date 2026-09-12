// The transport, handshake, and message identity shared by every JusPrin web
// surface. It renders nothing and knows no page's message types: a page names
// its protocol once, and consumes the connection state and the validated host
// envelopes. Reload safety comes from the protocol design -- every hello is
// answered with complete native state.

export type ConnectionState =
  | 'connecting'
  | 'connected'
  | 'no-transport'
  | 'incompatible'
  | 'timeout';

export interface Envelope<T = unknown> {
  protocol: string;
  version: number;
  id: string;
  type: string;
  correlationId?: string;
  sessionId?: string;
  revision?: number;
  payload: T;
}

// Which protocol this page speaks, from its own protocol.json.
export interface ProtocolIdentity {
  name: string;
  version: number;
  capabilities: string[];
}

export interface Transport {
  post(json: string): void;
}

export interface BridgeClientOptions {
  // The native transport can appear slightly after the page starts running:
  // wx injects window.wx into an already-loading page from native code. The
  // client therefore re-polls this factory before declaring no-transport.
  getTransport: () => Transport | null;
  onEnvelope: (envelope: Envelope) => void;
  onConnectionChange: (state: ConnectionState, detail?: string) => void;
  handshakeTimeoutMs?: number;
  transportRetryMs?: number;
  transportRetryLimit?: number;
  // Injectable for deterministic tests.
  scheduleTimeout?: (fn: () => void, ms: number) => unknown;
  cancelTimeout?: (handle: unknown) => void;
}

declare global {
  interface Window {
    wx?: { postMessage(json: string): void };
    __jusprinBridge?: { deliver(envelope: unknown): void };
  }
}

export function nativeTransport(): Transport | null {
  if (typeof window !== 'undefined' && window.wx && typeof window.wx.postMessage === 'function') {
    return { post: (json) => window.wx!.postMessage(json) };
  }
  return null;
}

export function envelopeMatches(value: unknown, protocolName: string): value is Envelope {
  if (typeof value !== 'object' || value === null) return false;
  const env = value as Record<string, unknown>;
  return (
    env.protocol === protocolName &&
    typeof env.version === 'number' &&
    typeof env.id === 'string' &&
    typeof env.type === 'string'
  );
}

export class SharedBridgeClient {
  private readonly protocol: ProtocolIdentity;
  private readonly options: BridgeClientOptions;
  private nextId = 1;
  private handshakeHandle: unknown = null;
  private state: ConnectionState = 'connecting';
  private transport: Transport | null = null;
  private transportAttempts = 0;

  constructor(protocol: ProtocolIdentity, options: BridgeClientOptions) {
    this.protocol = protocol;
    this.options = options;
  }

  connectionState(): ConnectionState {
    return this.state;
  }

  start(): void {
    if (typeof window !== 'undefined') {
      window.__jusprinBridge = { deliver: (envelope: unknown) => this.deliver(envelope) };
    }
    this.beginHandshake();
  }

  retry(): void {
    this.transportAttempts = 0;
    this.beginHandshake();
  }

  private schedule(fn: () => void, ms: number): unknown {
    const schedule = this.options.scheduleTimeout ?? ((f, m) => setTimeout(f, m));
    return schedule(fn, ms);
  }

  private cancel(handle: unknown): void {
    const cancel = this.options.cancelTimeout ?? ((h) => clearTimeout(h as number));
    cancel(handle);
  }

  private beginHandshake(): void {
    this.transport = this.options.getTransport();
    if (!this.transport) {
      const limit = this.options.transportRetryLimit ?? 40;
      if (this.transportAttempts < limit) {
        this.transportAttempts += 1;
        this.setState('connecting');
        this.schedule(() => {
          if (this.state === 'connecting' && !this.transport) this.beginHandshake();
        }, this.options.transportRetryMs ?? 250);
      } else {
        this.setState('no-transport', 'The native message handler (window.wx) is not present.');
      }
      return;
    }
    this.setState('connecting');
    this.post('hello', {
      protocolVersions: [this.protocol.version],
      capabilities: this.protocol.capabilities,
    });
    const timeoutMs = this.options.handshakeTimeoutMs ?? 10000;
    if (this.handshakeHandle !== null) this.cancel(this.handshakeHandle);
    this.handshakeHandle = this.schedule(() => {
      if (this.state === 'connecting') {
        this.setState('timeout', 'The native host did not answer the hello handshake.');
      }
    }, timeoutMs);
  }

  send(type: string, payload: unknown): void {
    this.post(type, payload);
  }

  private post(type: string, payload: unknown): void {
    if (!this.transport) return;
    const envelope: Envelope = {
      protocol: this.protocol.name,
      version: this.protocol.version,
      id: `w-${this.nextId++}`,
      type,
      payload,
    };
    this.transport.post(JSON.stringify(envelope));
  }

  deliver(raw: unknown): void {
    if (!envelopeMatches(raw, this.protocol.name)) return; // not this protocol; ignore
    const envelope = raw;
    if (envelope.type === 'hello_ack') {
      this.cancelHandshakeTimer();
      this.setState('connected');
    } else if (envelope.type === 'hello_reject') {
      this.cancelHandshakeTimer();
      const payload = envelope.payload as { message?: string } | undefined;
      this.setState('incompatible', payload?.message ?? 'The host rejected this protocol version.');
    }
    this.options.onEnvelope(envelope);
  }

  private cancelHandshakeTimer(): void {
    if (this.handshakeHandle !== null) {
      this.cancel(this.handshakeHandle);
      this.handshakeHandle = null;
    }
  }

  private setState(state: ConnectionState, detail?: string): void {
    this.state = state;
    this.options.onConnectionChange(state, detail);
  }
}
