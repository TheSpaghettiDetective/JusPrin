// The host state the visual cases start from: one project chat with a current
// setup, the way the reviewed Figma frames show the pane.

import { StatePayload, WireMessage } from '../bridge/protocol';
import { setupVisualCases } from '../components/SetupCard.visual-cases';

export const workspace = setupVisualCases.find((item) => item.id === 'A1')!.context;

export const turn = (id: string, role: WireMessage['role'], text: string, extra: Partial<WireMessage> = {}): WireMessage =>
  ({ id, role, state: 'complete', text, attempt: 1, ...extra });

export const exchange: WireMessage[] = [
  turn('m-1', 'user', 'This will hold a backpack. Make it strong, but keep the screw holes accurate.'),
  turn('m-2', 'assistant', 'I laid it on its side so the layers run through the hook, and thickened the walls around both screw holes.'),
];

export function chatState(overrides: Partial<StatePayload> = {}): StatePayload {
  return {
    agent: { status: 'ready' },
    appearance: 'light',
    conversations: [{ id: 'conv-1', title: 'Strong print', createdAt: '2026-10-05T10:42:00', updatedAt: '2026-10-05T10:42:00',
      preview: 'I laid it on its side so the layers run through the hook.' }],
    activeConversationId: 'conv-1',
    conversation: exchange,
    streamingMessageId: null,
    toolActivities: [],
    builds: [],
    exportedCopies: [],
    physicalPrints: [],
    draft: '',
    context: workspace,
    ...overrides,
  };
}
