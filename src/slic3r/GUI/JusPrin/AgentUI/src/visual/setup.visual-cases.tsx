// The API key field's five states (canvas "JusPrin v2", page "Agent Setup ·
// Empty State", board "api-key-field · saved vs empty · proposal"): no key,
// a key saved, replacing it, checking the replacement, and a key the provider
// refused. Each is the real App against a scripted host, which sends a hint
// of the stored key and never the key.

import { fireEvent, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { mountedApp, ScriptedHost, VisualCase } from '../test/visual';
import { chatState } from './states';

const BOARD = 'canvas board api-key-field · saved vs empty';
const saved = { provider: 'anthropic', hint: 'sk-ant-…a3F9', rejected: false };

const apiKey = (id: string, name: string, expects: string[], keys: typeof saved[], after?: (host: ScriptedHost) => Promise<void> | void): VisualCase => ({
  node: BOARD, matrix: 'API key field', id: `api-key-${id}`, name, frame: 'pane', expects,
  build: async () => {
    // The app's web view can read the clipboard, so the field offers Paste;
    // jsdom has no clipboard of its own.
    Object.defineProperty(navigator, 'clipboard', { value: { readText: async () => '' }, configurable: true });
    try {
      return await mountedApp(chatState({ agent: { status: 'unavailable' }, conversation: [] }), { after: async (host) => {
      host.deliver('setup_keys', { keys });
      fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
      fireEvent.click(screen.getByTestId('setup-row-api-key'));
      fireEvent.click(screen.getByRole('tab', { name: 'Anthropic' }));
      if (after) await after(host);
      } });
    } finally {
      delete (navigator as { clipboard?: unknown }).clipboard;
    }
  },
});

const replace = () => { fireEvent.click(screen.getByRole('button', { name: 'Replace' })); };

export const setupCases: VisualCase[] = [
  apiKey('none', '1. No key saved', ['Paste your Anthropic key (sk-ant-…)', '>Paste<', 'Check key'], []),
  apiKey('saved', '2. Key saved', ['Key saved on this computer', 'sk-ant-…a3F9', 'Replace', 'Remove'], [saved]),
  apiKey('replacing', '3. Replacing', ['Paste the new key', 'Keeps sk-ant-…a3F9', 'data-visual-focus'], [saved], replace),
  apiKey('checking', '4. Checking', ['Key saved on this computer', 'sk-ant-…a3F9', 'Checking…', 'Cancel'], [saved],
    async ({ deliver }) => {
      replace();
      await userEvent.type(screen.getByLabelText('Anthropic API key'), 'sk-ant-new');
      fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
      deliver('setup_status', { phase: 'checking', provider: 'anthropic' });
    }),
  apiKey('rejected', '5. Key rejected', ['The provider rejected this key', 'setup-key--rejected', 'Replace it with a working key, or remove it.'],
    [{ ...saved, rejected: true }]),
];
