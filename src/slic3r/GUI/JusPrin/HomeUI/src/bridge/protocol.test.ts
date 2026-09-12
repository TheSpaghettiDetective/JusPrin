// The page and the native host agree through resources/jusprin/home/protocol.json.
// These cases fail if the typed view drifts from the shared file.

import { describe, expect, it } from 'vitest';
import protocolJson from '@resources/jusprin/home/protocol.json';
import { isEnvelope, PAGE_CAPABILITIES, PROTOCOL_NAME, PROTOCOL_VERSION } from './protocol';
import type { HostMessageType, PageMessageType } from './protocol';

describe('protocol constants come from the shared file', () => {
  it('names this protocol and its version', () => {
    expect(PROTOCOL_NAME).toBe('jusprin-home-bridge');
    expect(PROTOCOL_VERSION).toBe(protocolJson.version);
    expect(PAGE_CAPABILITIES).toEqual(protocolJson.capabilities);
  });

  // A type the page can name but the file does not list would be sent and
  // never answered.
  it('declares exactly the message types the file lists', () => {
    const page: PageMessageType[] = [
      'hello',
      'state_request',
      'open_project',
      'new_project',
      'import_project',
      'launch_monitor',
      'add_printer',
    ];
    const host: HostMessageType[] = [
      'hello_ack',
      'hello_reject',
      'state',
      'projects',
      'printers',
      'appearance',
      'bridge_error',
    ];
    expect([...page].sort()).toEqual([...protocolJson.pageMessageTypes].sort());
    expect([...host].sort()).toEqual([...protocolJson.hostMessageTypes].sort());
  });
});

describe('isEnvelope', () => {
  it('accepts a well-formed envelope of this protocol', () => {
    expect(isEnvelope({ protocol: PROTOCOL_NAME, version: 1, id: 'h-1', type: 'state', payload: {} })).toBe(true);
  });

  it('rejects anything else', () => {
    expect(isEnvelope(null)).toBe(false);
    expect(isEnvelope('text')).toBe(false);
    // The Agent page's traffic must not be read as Home's.
    expect(isEnvelope({ protocol: 'jusprin-agent-bridge', version: 1, id: 'x', type: 'state' })).toBe(false);
    expect(isEnvelope({ protocol: PROTOCOL_NAME, id: 'x', type: 'state' })).toBe(false);
  });
});
