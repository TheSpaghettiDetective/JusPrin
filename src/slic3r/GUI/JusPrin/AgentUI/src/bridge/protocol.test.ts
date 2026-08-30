// The protocol constants must agree with the shared versioned source that the
// native host is also tested against.

import { describe, expect, it } from 'vitest';
import protocolJson from '@resources/jusprin/agent/protocol.json';
import { isEnvelope, PAGE_CAPABILITIES, PROTOCOL_NAME, PROTOCOL_VERSION } from './protocol';

describe('protocol constants', () => {
  it('derive from the shared protocol.json', () => {
    expect(PROTOCOL_NAME).toBe('jusprin-agent-bridge');
    expect(PROTOCOL_VERSION).toBe(2);
    expect(PROTOCOL_NAME).toBe(protocolJson.name);
    expect(PROTOCOL_VERSION).toBe(protocolJson.version);
    expect(PAGE_CAPABILITIES).toEqual(protocolJson.capabilities);
    expect(PAGE_CAPABILITIES).toContain('tools');
  });

  it('page and host message type unions cover the shared lists', () => {
    expect(protocolJson.pageMessageTypes).toEqual([
      'hello',
      'state_request',
      'user_message',
      'stop_generation',
      'retry_message',
      'tool_decision',
      'tool_cancel',
    ]);
    expect(protocolJson.hostMessageTypes).toContain('hello_ack');
    expect(protocolJson.hostMessageTypes).toContain('assistant_delta');
    expect(protocolJson.hostMessageTypes).toContain('tool_activity');
    expect(protocolJson.hostMessageTypes).toContain('bridge_error');
  });
});

describe('isEnvelope', () => {
  it('accepts a well-formed envelope', () => {
    expect(
      isEnvelope({ protocol: PROTOCOL_NAME, version: 1, id: 'h-1', type: 'state', payload: {} }),
    ).toBe(true);
  });

  it('rejects foreign or malformed messages', () => {
    expect(isEnvelope(null)).toBe(false);
    expect(isEnvelope('text')).toBe(false);
    expect(isEnvelope({ protocol: 'other', version: 1, id: 'x', type: 'state' })).toBe(false);
    expect(isEnvelope({ protocol: PROTOCOL_NAME, id: 'x', type: 'state' })).toBe(false);
  });
});
