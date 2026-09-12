// Bridge client for the Home page: the shared transport and handshake bound to
// this page's protocol identity.

import { SharedBridgeClient } from '@shared/bridgeClient';
import type { BridgeClientOptions } from '@shared/bridgeClient';
import { PAGE_CAPABILITIES, PROTOCOL_NAME, PROTOCOL_VERSION } from './protocol';

export type { ConnectionState, Transport, BridgeClientOptions } from '@shared/bridgeClient';
export { nativeTransport } from '@shared/bridgeClient';

export class BridgeClient extends SharedBridgeClient {
  constructor(options: BridgeClientOptions) {
    super({ name: PROTOCOL_NAME, version: PROTOCOL_VERSION, capabilities: PAGE_CAPABILITIES }, options);
  }
}
