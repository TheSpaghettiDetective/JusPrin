// The runtime matrix (Figma 1619:2111) and the chat header matrix
// (1540:3341): connecting, the bridge error with its Diagnostics closed and
// open, the transient command error, and every state of the header, its menu
// and its two dialogs. Each is the real App against a scripted host.

import { act, fireEvent, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { mountedApp, ScriptedHost, VisualCase } from '../test/visual';
import { chatState } from './states';

const pane = (node: string, matrix: string, id: string, name: string, expects: string[], build: VisualCase['build']): VisualCase =>
  ({ node, matrix, id, name, frame: 'pane', expects, build });

const runtime = (id: string, name: string, expects: string[], build: VisualCase['build']) =>
  pane('1619:2111', 'Runtime states', `runtime-${id}`, name, expects, build);
const header = (id: string, name: string, expects: string[], build: VisualCase['build']) =>
  pane('1540:3341', 'Chat header', `header-${id}`, name, expects, build);

// A handshake nobody answers: the pane's own timeout is what puts it in the
// bridge-error state.
const timedOut = (open: boolean) => mountedApp(null, {
  props: { handshakeTimeoutMs: 1, transportRetryLimit: 0 },
  after: async ({ root }) => {
    await act(async () => { await new Promise((resolve) => setTimeout(resolve, 30)); });
    if (open) root.querySelector('details')!.open = true;
  },
});

const longTitle = 'Backpack wall bracket with accurate screw holes and reinforced hook';
const openMenu = async () => { await userEvent.click(screen.getByRole('button', { name: 'Chat actions' })); };
const pick = async (item: string) => { await openMenu(); await userEvent.click(screen.getByRole('menuitem', { name: item })); };
const busyState = () => chatState({ conversationBusy: true });

export const runtimeCases: VisualCase[] = [
  runtime('connecting', 'Mode=Connecting', ['Connecting…', 'Starting the Agent panel.'], () => mountedApp(null)),
  runtime('bridge-error', 'Mode=Bridge error · Diagnostics=Collapsed', ['data-testid="bridge-error"', 'Retry', 'Diagnostics'],
    () => timedOut(false)),
  runtime('bridge-error-diagnostics', 'Mode=Bridge error · Diagnostics=Expanded', ['<details class="diagnostics" open', 'diagnostics-code'],
    () => timedOut(true)),
  runtime('command-error', 'Transient command error', ['Resolve pending actions before deleting this chat.', 'Dismiss error'],
    () => mountedApp(chatState(), { after: ({ deliver }: ScriptedHost) =>
      deliver('bridge_error', { code: 'pending_actions', message: 'Resolve pending actions before deleting this chat.' }) })),

  header('default', 'State=Default', ['Strong print', 'Hide the Agent panel'], () => mountedApp(chatState())),
  header('long-title', 'State=Long title', [longTitle], () => mountedApp(chatState({
    conversations: [{ id: 'conv-1', title: longTitle, createdAt: '2026-10-05T10:42:00' }] }))),
  header('busy', 'State=Busy', ['aria-label="New chat" title="New chat" disabled'], () => mountedApp(busyState())),
  header('actions-open', 'State=Actions open', ['role="menu"', 'Rename', 'Delete'], () => mountedApp(chatState(), { after: openMenu })),
  header('rename', 'Rename valid', ['Rename chat', 'value="Strong print"', 'Save'], () => mountedApp(chatState(), { after: () => pick('Rename') })),
  header('rename-invalid', 'Rename invalid', ['Enter a title of 1–120 characters.', 'aria-invalid="true"'],
    () => mountedApp(chatState(), { after: async () => {
      await pick('Rename');
      fireEvent.change(screen.getByLabelText('Chat title'), { target: { value: '' } });
    } })),
  header('delete', 'Delete ready', ['Delete chat?', 'Delete ‘Strong print’ and its messages?'],
    () => mountedApp(chatState(), { after: () => pick('Delete') })),
  header('delete-busy', 'Delete busy', ['Wait for the current action to finish before deleting.'],
    () => mountedApp(chatState(), { after: async ({ deliver }: ScriptedHost) => {
      await pick('Delete');
      deliver('state', busyState());
      expect(within(screen.getByRole('dialog')).getByRole('button', { name: 'Delete' })).toBeDisabled();
    } })),
];
