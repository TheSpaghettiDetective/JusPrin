// Agent setup as the "JusPrin v2" canvas draws it (2026-10-21): the chooser
// with the JusPrin account block, a provider key row in each of its states
// (board "API key · one row per provider · every state"), the AI tool rows,
// and the one-time Connected banner. Each is the real App against a scripted
// host, which sends a hint of a stored key and never the key.

import { fireEvent, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { mountedApp, ScriptedHost, VisualCase } from '../test/visual';
import { chatState } from './states';

const BOARD = 'canvas board API key · one row per provider · every state';
const saved = { provider: 'anthropic', hint: 'sk-ant-…a3F9', rejected: false };

const unconfigured = () => chatState({ agent: { status: 'unavailable' }, conversation: [] });

const apiKey = (id: string, name: string, expects: string[], keys: typeof saved[], after?: (host: ScriptedHost) => Promise<void> | void): VisualCase => ({
  node: BOARD, matrix: 'API key rows', id: `api-key-${id}`, name, frame: 'pane', expects,
  build: async () => {
    // The app's web view can read the clipboard, so the field offers Paste;
    // jsdom has no clipboard of its own.
    Object.defineProperty(navigator, 'clipboard', { value: { readText: async () => '' }, configurable: true });
    try {
      return await mountedApp(unconfigured(), { after: async (host) => {
      host.deliver('setup_keys', { keys });
      fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
      fireEvent.click(screen.getByTestId('setup-row-api-key'));
      if (after) await after(host);
      } });
    } finally {
      delete (navigator as { clipboard?: unknown }).clipboard;
    }
  },
});

const add = () => { fireEvent.click(screen.getByRole('button', { name: 'Add Anthropic key' })); };
const replace = () => { fireEvent.click(screen.getByRole('button', { name: 'Replace Anthropic key' })); };
const check = async (host: ScriptedHost, field: string) => {
  await userEvent.type(screen.getByLabelText(field), 'sk-ant-new');
  fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
  host.deliver('setup_status', { phase: 'checking', provider: 'anthropic' });
};

const tools = [
  { id: 'claude', name: 'Claude Code', detected: true, cli: true, text: 'claude mcp add jusprin' },
  { id: 'cowork', name: 'Cowork', subtitle: 'local sessions', detected: false, cli: false, text: '{}' },
  { id: 'vscode', name: 'VS Code', detected: false, cli: false, text: '{}' },
];

export const setupCases: VisualCase[] = [
  {
    node: 'canvas board Agent setup · choose a way', matrix: 'Agent setup', id: 'setup-chooser', name: 'Choose a way',
    frame: 'pane', expects: ['JusPrin account', 'Continue with JusPrin', 'Your own API key', 'Connect an AI tool you already use'],
    build: () => mountedApp(unconfigured(), { after: () => {
      fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
    } }),
  },
  apiKey('none', '1. No key', ['No key', 'Add key'], []),
  apiKey('entering', '2. Entering a key', ['Paste your Anthropic key (sk-ant-…)', '>Paste<', 'Check key', 'data-visual-focus'], [], add),
  apiKey('checking', '3. Checking', ['Checking with Anthropic…', 'sk-ant-••••', 'Cancel'], [],
    async (host) => { add(); await check(host, 'Anthropic API key'); }),
  apiKey('could-not-check', '4. Couldn’t check', ['Couldn’t reach Anthropic to check this key.', 'agent-key-field--locked'], [],
    async (host) => {
      add();
      await check(host, 'Anthropic API key');
      host.deliver('setup_status', { phase: 'error', provider: 'anthropic',
        error: { code: 'network_error', message: 'The Agent request could not reach the Anthropic service.', retryable: true } });
    }),
  apiKey('saved', '5. Key saved', ['Key saved on this computer', 'sk-ant-…a3F9', 'Replace', 'Remove'], [saved]),
  apiKey('replacing', '6. Replacing', ['Paste the new key', 'until the new key passes', 'data-visual-focus'], [saved], replace),
  apiKey('rejected', '7. Rejected', ['The provider rejected this key', 'agent-row-status--rejected',
    'Replace it with a working key, or remove it.'], [{ ...saved, rejected: true }]),
  apiKey('session-only', '8. Working until restart', ['Working, but only until JusPrin restarts',
    'enter it again after a restart'], [], (host) => {
    host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 500,
      warning: 'This key could not be saved, so it will have to be entered again next time JusPrin starts.' });
  }),
  ...[
    ['connected', 'Connected', undefined, ['Connected · your own OpenAI key', 'jp-icon-check']],
    ['connected-not-stored', 'Connected, key not stored',
      'This key could not be saved, so it will have to be entered again next time JusPrin starts.',
      ['could not be saved', 'jp-icon-triangle-alert']],
  ].map(([id, name, warning, expects]): VisualCase => ({
    node: 'canvas board Agent setup · connected', matrix: 'Agent setup', id: `setup-${id}`, name: name as string,
    frame: 'pane', expects: expects as string[],
    build: () => mountedApp(unconfigured(), { after: (host) => {
      fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
      fireEvent.click(screen.getByTestId('setup-row-api-key'));
      host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 500, warning });
      host.deliver('agent_status', { status: 'ready' });
    } }),
  })),
  {
    node: 'canvas board Agent setup · connect an AI tool', matrix: 'Agent setup', id: 'setup-tools', name: 'Connect an AI tool',
    frame: 'pane', expects: ['Found on this computer', 'Not found on this computer', 'Connect…', 'local sessions', 'Look again'],
    build: () => mountedApp(unconfigured(), { after: (host) => {
      fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
      fireEvent.click(screen.getByTestId('setup-row-connect-tool'));
      host.deliver('mcp_catalog', { helperPresent: true, tools });
    } }),
  },
];
