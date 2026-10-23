// Agent setup as the "JusPrin v2" canvas draws it: a provider key row in each
// of its states (board "API key · one row per provider · every state") and
// the AI tool rows. Since 2026-10-23 these rows are set up in one place,
// Preferences › Agent (board "Agent setup · one door"), so each case is the
// real Preferences page on its Agent section against a scripted host, which
// sends a hint of a stored key and never the key.

import { fireEvent, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PreferencesPayload, StatePayload } from '../bridge/protocol';
import { mountedPreferences, ScriptedHost, VisualCase } from '../test/visual';

const BOARD = 'canvas board API key · one row per provider · every state';
const saved = { provider: 'anthropic', hint: 'sk-ant-…a3F9', rejected: false };

const agentSection: PreferencesPayload = {
  kind: 'preferences',
  section: 'agent',
  appearance: { language: 'en', languages: [{ code: 'en', name: 'English' }], theme: 'light', units: 'mm', restartFor: null },
  view3d: {
    navigation: 'mouse', zoomToPointer: true, reverseZoom: false, freeCamera: false,
    leftDrag: 'rotate', middleDrag: 'pan', rightDrag: 'pan',
  },
  projects: {
    versionsFolder: '/Users/alex/JusPrin/projects', downloadPath: '/Users/alex/Downloads',
    associations: false, open3mf: false, openModels: false, openLinks: false,
  },
  updates: { version: '2.0.0', status: 'notChecked', checked: null, available: null, checkUpdates: true },
  agent: { newChatProvider: 'openai', newChatChoices: [] },
};

const unconfigured = () => ({
  agent: { status: 'unavailable' },
  appearance: 'light',
  conversations: [],
  activeConversationId: '',
  conversation: [],
  streamingMessageId: null,
  context: null,
  session: agentSection,
}) as unknown as StatePayload;

const apiKey = (id: string, name: string, expects: string[], keys: typeof saved[], after?: (host: ScriptedHost) => Promise<void> | void): VisualCase => ({
  node: BOARD, matrix: 'API key rows', id: `api-key-${id}`, name, frame: 'pane', expects,
  build: async () => {
    // The app's web view can read the clipboard, so the field offers Paste;
    // jsdom has no clipboard of its own.
    Object.defineProperty(navigator, 'clipboard', { value: { readText: async () => '' }, configurable: true });
    try {
      return await mountedPreferences(unconfigured(), async (host) => {
        host.deliver('setup_keys', { keys });
        if (after) await after(host);
      });
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
  {
    node: 'canvas board Agent setup · connect an AI tool', matrix: 'Agent setup', id: 'setup-tools', name: 'Connect an AI tool',
    frame: 'pane', expects: ['Found on this computer', 'Not found on this computer', 'Connect…', 'local sessions', 'Look again'],
    build: () => mountedPreferences(unconfigured(), (host) => {
      host.deliver('mcp_catalog', { helperPresent: true, tools });
    }),
  },
];
