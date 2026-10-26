// The Preferences window's page against a scripted host: it draws what the
// app says is stored, sends one preferences_action per change, and draws the
// Agent's connection rows with the same components Agent setup uses.

import { beforeEach, describe, expect, it, vi } from 'vitest';
import { act, fireEvent, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PreferencesApp } from './PreferencesApp';
import { checkedAgo } from './components/Preferences';
import { Envelope, PreferencesPayload, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from './bridge/protocol';

class MockHost {
  received: Envelope[] = [];
  transport = { post: (json: string) => { this.received.push(JSON.parse(json)); } };

  deliver(type: string, payload: unknown): void {
    const envelope: Envelope = { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: `h-${this.received.length}`, type, payload };
    act(() => { window.__jusprinBridge!.deliver(envelope); });
  }

  ofType(type: string): Envelope[] {
    return this.received.filter((envelope) => envelope.type === type);
  }

  lastOfType(type: string): Envelope | undefined {
    return [...this.received].reverse().find((envelope) => envelope.type === type);
  }
}

function preferences(overrides: Partial<PreferencesPayload> = {}): PreferencesPayload {
  return {
    kind: 'preferences',
    section: 'account',
    appearance: {
      language: 'en',
      languages: [{ code: 'en', name: 'English' }, { code: 'de_DE', name: 'Deutsch' }],
      theme: 'light',
      units: 'mm',
      restartFor: null,
    },
    view3d: {
      navigation: 'mouse', zoomToPointer: true, reverseZoom: false, freeCamera: false,
      leftDrag: 'rotate', middleDrag: 'pan', rightDrag: 'pan',
    },
    projects: {
      versionsFolder: 'C:/data/jusprin/projects', downloadPath: 'C:/Users/alex/Downloads',
      associations: true, open3mf: true, openModels: false, openLinks: false,
    },
    updates: { version: '2.0.0', status: 'notChecked', checked: null, available: null, checkUpdates: true },
    agent: {
      newChatProvider: 'openai',
      newChatChoices: [
        { provider: 'openai', model: 'gpt-5.4-mini', label: 'OpenAI' },
        { provider: 'anthropic', model: 'claude-opus-5-5', label: 'Anthropic' },
      ],
    },
    ...overrides,
  };
}

function state(session: PreferencesPayload): StatePayload {
  return {
    agent: { status: 'unavailable' },
    appearance: 'light',
    conversations: [],
    activeConversationId: '',
    conversation: [],
    streamingMessageId: null,
    context: null,
    session,
  } as unknown as StatePayload;
}

function connect(host: MockHost, session = preferences()) {
  expect(host.lastOfType('hello')).toBeTruthy();
  host.deliver('hello_ack', { version: PROTOCOL_VERSION, agent: { status: 'unavailable' }, appearance: 'light' });
  host.deliver('state', state(session));
}

const actionsSent = (host: MockHost) => host.ofType('preferences_action').map((envelope) => envelope.payload);

describe('Preferences', () => {
  let host: MockHost;
  beforeEach(() => { host = new MockHost(); window.sessionStorage.clear(); });

  it('opens on the section it was asked for, with the six sections and the OrcaSlicer link', () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'appearance' }));
    const nav = screen.getByRole('navigation', { name: 'Preference sections' });
    expect(within(nav).getAllByRole('button').map((button) => button.textContent)).toEqual([
      'Account', 'Agent', 'Appearance', '3D view', 'Projects & files', 'Updates', 'OrcaSlicer preferences…',
    ]);
    expect(within(nav).getByRole('button', { name: 'Appearance' })).toHaveAttribute('aria-current', 'page');
    expect(screen.getByRole('heading', { name: 'Appearance', level: 2 })).toBeInTheDocument();
  });

  it('stays on its section when the page reloads, as a theme switch makes it', async () => {
    const first = render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host);
    await userEvent.click(screen.getByRole('button', { name: '3D view' }));
    first.unmount();
    host = new MockHost();
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host);
    expect(screen.getByRole('heading', { name: '3D view', level: 2 })).toBeInTheDocument();
  });

  it('opens OrcaSlicer’s own dialog from the link', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host);
    await userEvent.click(screen.getByRole('button', { name: /OrcaSlicer preferences/ }));
    expect(actionsSent(host)).toEqual([{ action: 'orca_preferences' }]);
  });

  it('signs in to a JusPrin account through the browser and points to Agent', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host);
    expect(screen.getByTestId('agent-account-status')).toHaveTextContent('Free · sign in with your browser');
    await userEvent.click(screen.getByRole('button', { name: 'Continue with JusPrin' }));
    expect(host.lastOfType('account_sign_in')?.payload).toEqual({});

    // Waiting on the browser: what to do there, a way to copy the page, and Cancel.
    host.deliver('account_status', { phase: 'browser', url: 'https://app.obico.io/o/authorize/?client_id=JusPrin2' });
    expect(screen.getByTestId('agent-account-status')).toHaveTextContent('Finish signing in in your browser');
    expect(screen.getByText(/A JusPrin tab just opened/)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Copy link' })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('account_cancel')).toBeTruthy();

    // Back from the browser, the account is checked, then shown by its email.
    host.deliver('account_status', { phase: 'checking' });
    expect(screen.getByTestId('agent-account-status')).toHaveTextContent('Checking your account');
    host.deliver('account_status', { phase: 'idle' });
    host.deliver('setup_keys', { keys: [{ provider: 'jusprin', hint: '…wxyz', rejected: false, account: 'alex@example.com' }] });
    expect(screen.getByTestId('agent-account-status')).toHaveTextContent('Signed in as alex@example.com');
    await userEvent.click(screen.getByRole('button', { name: 'Sign out' }));
    expect(host.lastOfType('setup_remove_key')?.payload).toEqual({ provider: 'jusprin' });

    await userEvent.click(within(screen.getByRole('main')).getByRole('button', { name: 'Agent' }));
    expect(screen.getByRole('heading', { name: 'Agent', level: 2 })).toBeInTheDocument();
  });

  it('asks to sign in again when the account’s sign-in has ended, and says why one failed', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host);
    host.deliver('setup_keys', { keys: [{ provider: 'jusprin', hint: '…wxyz', rejected: true, account: 'alex@example.com' }] });
    expect(screen.getByTestId('agent-account-status')).toHaveTextContent('Your sign-in for alex@example.com has ended');
    expect(screen.queryByRole('button', { name: 'Sign out' })).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Sign in again' }));
    expect(host.lastOfType('account_sign_in')).toBeTruthy();

    host.deliver('account_status', {
      phase: 'idle', error: { code: 'sign_in_failed', message: 'Signing in didn’t finish. Nothing was saved. Try again.', retryable: true },
    });
    expect(screen.getByTestId('account-error')).toHaveTextContent('Signing in didn’t finish');
  });

  it('does not list the JusPrin account among the API keys', () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'agent' }));
    host.deliver('setup_keys', { keys: [{ provider: 'jusprin', hint: '…wxyz', rejected: false, account: 'alex@example.com' }] });
    expect(screen.queryByTestId('agent-key-jusprin')).toBeNull();
    expect(screen.getByTestId('agent-account-status')).toHaveTextContent('Signed in as alex@example.com');
  });

  it('draws the Agent section with setup’s rows and their messages', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'agent' }));
    expect(host.lastOfType('mcp_catalog')).toBeTruthy();
    host.deliver('setup_keys', { keys: [{ provider: 'anthropic', hint: 'sk-ant-…a1b2', rejected: false }] });
    host.deliver('mcp_catalog', { tools: [{ id: 'claude-code', name: 'Claude Code', detected: true, cli: true, text: 'claude mcp add jusprin' }], helperPresent: true });

    expect(screen.getByTestId('agent-account')).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Your own API key' })).toBeInTheDocument();
    expect(screen.getByTestId('agent-key-anthropic-standing')).toHaveTextContent('Key saved on this computer');
    expect(screen.getByRole('heading', { name: 'Connect an AI tool you already use' })).toBeInTheDocument();

    await userEvent.click(screen.getByRole('button', { name: 'Add OpenAI key' }));
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-test');
    await userEvent.click(screen.getByRole('button', { name: 'Check key' }));
    expect(host.lastOfType('setup_check_key')?.payload).toEqual({ provider: 'openai', apiKey: 'sk-test' });

    await userEvent.click(screen.getByRole('button', { name: 'Remove Anthropic key' }));
    expect(host.lastOfType('setup_remove_key')?.payload).toEqual({ provider: 'anthropic' });

    // A tool's Connect… opens that tool's own screen, and its way back is the section.
    await userEvent.click(screen.getByRole('button', { name: 'Connect Claude Code' }));
    expect(screen.getByTestId('setup-local-prepare')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Close' }));
    expect(screen.getByRole('heading', { name: 'Agent', level: 2 })).toBeInTheDocument();
  });

  it('sends one change per control, by the name the app writes it under', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'appearance' }));
    await userEvent.click(screen.getByRole('radio', { name: 'Dark' }));
    await userEvent.click(screen.getByRole('radio', { name: 'Inches' }));
    await userEvent.click(screen.getByRole('button', { name: 'Language: English' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Deutsch' }));

    await userEvent.click(screen.getByRole('button', { name: '3D view' }));
    await userEvent.click(screen.getByRole('checkbox', { name: /Zoom toward the pointer/ }));
    await userEvent.click(screen.getByRole('button', { name: 'Left-drag: Turn the view' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Pan' }));

    await userEvent.click(screen.getByRole('button', { name: 'Updates' }));
    await userEvent.click(screen.getByRole('checkbox', { name: /New JusPrin versions/ }));

    expect(actionsSent(host)).toEqual([
      { action: 'set', name: 'theme', value: 'dark' },
      { action: 'set', name: 'units', value: 'in' },
      { action: 'set', name: 'language', value: 'de_DE' },
      { action: 'set', name: 'zoomToPointer', value: false },
      { action: 'set', name: 'leftDrag', value: 'pan' },
      { action: 'set', name: 'checkUpdates', value: false },
    ]);
  });

  it('draws what the app sent back, not what was clicked', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'appearance' }));
    await userEvent.click(screen.getByRole('radio', { name: 'Dark' }));
    expect(screen.getByRole('radio', { name: 'Light' })).toHaveAttribute('aria-checked', 'true');
    host.deliver('preferences', preferences({ section: 'appearance', appearance: { ...preferences().appearance, theme: 'dark' } }));
    expect(screen.getByRole('radio', { name: 'Dark' })).toHaveAttribute('aria-checked', 'true');
  });

  it('offers file associations only where the app has them', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'projects' }));
    expect(screen.getByRole('heading', { name: 'Open with JusPrin' })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('checkbox', { name: /STL and STEP models/ }));
    await userEvent.click(screen.getByRole('button', { name: 'Show folder' }));
    await userEvent.click(screen.getByRole('button', { name: 'Change…' }));
    expect(actionsSent(host)).toEqual([
      { action: 'set', name: 'openModels', value: true },
      { action: 'show_versions' },
      { action: 'choose_downloads' },
    ]);
    host.deliver('preferences', preferences({ section: 'projects', projects: { ...preferences().projects, associations: false } }));
    expect(screen.queryByRole('heading', { name: 'Open with JusPrin' })).not.toBeInTheDocument();
  });

  it('marks Updates when a newer version was found and offers it', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    const available = { version: '2.1.0', url: 'https://example.invalid', published: '2026-11-02T10:00:00Z', notes: '## Fixes\n\n- Slices faster' };
    connect(host, preferences({ section: 'updates', updates: { ...preferences().updates, status: 'available', checked: 1_800_000_000, available } }));
    expect(screen.getByRole('img', { name: 'Update available' })).toBeInTheDocument();
    expect(screen.getByText('JusPrin 2.1.0 is available')).toBeInTheDocument();
    expect(screen.getByText(/you have 2\.0\.0/)).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Release notes' })).toBeInTheDocument();
    expect(screen.getByText('Slices faster')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Download' }));
    await userEvent.click(screen.getByRole('button', { name: 'Skip this version' }));
    expect(actionsSent(host)).toEqual([{ action: 'open_update' }, { action: 'skip_update', version: '2.1.0' }]);
  });

  it('checks for a new version on Check now and shows the check under way', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'updates' }));
    expect(screen.getByText('JusPrin 2.0.0')).toBeInTheDocument();
    expect(screen.getByText('Not checked yet')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Check now' }));
    expect(actionsSent(host)).toEqual([{ action: 'check_updates' }]);

    host.deliver('preferences', preferences({ section: 'updates', updates: { ...preferences().updates, status: 'checking' } }));
    expect(screen.getByRole('button', { name: 'Checking…' })).toBeDisabled();
    expect(screen.getByRole('progressbar', { name: 'Checking for a newer version' })).toBeInTheDocument();

    const now = Date.now() / 1000;
    host.deliver('preferences', preferences({ section: 'updates', updates: { ...preferences().updates, status: 'upToDate', checked: now - 2 * 3600 } }));
    expect(screen.getByText('Up to date · checked 2 hours ago')).toBeInTheDocument();
    expect(screen.queryByRole('img', { name: 'Update available' })).not.toBeInTheDocument();
  });

  it('says quietly when the check could not reach GitHub, and offers another try', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    const checked = Date.now() / 1000 - 3 * 86400;
    connect(host, preferences({ section: 'updates', updates: { ...preferences().updates, status: 'failed', checked } }));
    expect(screen.getByText('Couldn’t reach GitHub · last checked 3 days ago')).toBeInTheDocument();
    expect(screen.getByText('JusPrin tries again tomorrow, or the next time it starts.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Try again' }));
    expect(actionsSent(host)).toEqual([{ action: 'check_updates' }]);
  });

  it('turns the daily check off, and says what it sends', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'updates' }));
    expect(screen.queryByRole('checkbox', { name: /Stable releases only/ })).not.toBeInTheDocument();
    const daily = screen.getByRole('checkbox', { name: /New JusPrin versions/ });
    expect(daily).toHaveAccessibleName(/Nothing about you or your projects is sent/);
    await userEvent.click(daily);
    expect(actionsSent(host)).toEqual([{ action: 'set', name: 'checkUpdates', value: false }]);
  });

  it('says built-in profiles come with each release, with nothing to switch', () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'updates' }));
    expect(screen.getByText('Built-in printer and filament profiles')).toBeInTheDocument();
    expect(screen.getByText('Update with each JusPrin release. Your own presets never change.')).toBeInTheDocument();
    expect(screen.queryByRole('checkbox', { name: /profiles/ })).not.toBeInTheDocument();
  });

  it('leaves updates to the Microsoft Store for a Store install', () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'updates', updates: { ...preferences().updates, status: 'store' } }));
    expect(screen.getByText('Updates come from the Microsoft Store.')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Check now' })).not.toBeInTheDocument();
    expect(screen.queryByRole('checkbox', { name: /New JusPrin versions/ })).not.toBeInTheDocument();
  });

  it('says how long ago the last check was', () => {
    const now = 1_800_000_000 * 1000;
    expect(checkedAgo(1_800_000_000 - 20, now)).toBe('just now');
    expect(checkedAgo(1_800_000_000 - 60, now)).toBe('1 minute ago');
    expect(checkedAgo(1_800_000_000 - 45 * 60, now)).toBe('45 minutes ago');
    expect(checkedAgo(1_800_000_000 - 3600, now)).toBe('1 hour ago');
    expect(checkedAgo(1_800_000_000 - 30 * 3600, now)).toBe('yesterday');
    expect(checkedAgo(1_800_000_000 - 5 * 86400, now)).toBe('5 days ago');
  });

  it('offers a restart for a new language, and puts the offer away on Later', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'appearance' }));
    expect(screen.queryByText(/after it restarts/)).not.toBeInTheDocument();
    const waiting = preferences({ section: 'appearance', appearance: { ...preferences().appearance, language: 'de_DE', restartFor: 'Deutsch' } });
    host.deliver('preferences', waiting);
    expect(screen.getByRole('status')).toHaveTextContent('JusPrin will use Deutsch after it restarts.');
    await userEvent.click(screen.getByRole('button', { name: 'Restart now' }));
    expect(actionsSent(host)).toEqual([{ action: 'restart' }]);
    await userEvent.click(screen.getByRole('button', { name: 'Later' }));
    expect(screen.queryByText(/after it restarts/)).not.toBeInTheDocument();
  });

  it('chooses what new chats start with among the providers that have a key', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'agent' }));
    await userEvent.click(screen.getByRole('button', { name: 'Model: gpt-5.4-mini · OpenAI' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'claude-opus-5-5 · Anthropic' }));
    expect(actionsSent(host)).toEqual([{ action: 'set', name: 'newChatProvider', value: 'anthropic' }]);
  });
});

// Agent setup's rows, which live in Preferences › Agent: the one place an
// agent is set up or changed, whichever surface sent the person there.
describe('Preferences › Agent', () => {
  let host: MockHost;
  beforeEach(() => { host = new MockHost(); window.sessionStorage.clear(); });

  function openAgentSection() {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host, preferences({ section: 'agent' }));
  }

  const openApiKeyScreen = openAgentSection;

  // Both branches of the flow start here: 'claude' runs a CLI command,
  // 'cursor' edits a settings file.
  function openToolCatalog() {
    openAgentSection();
    host.deliver('mcp_catalog', {
      helperPresent: true,
      liveUrl: 'http://127.0.0.1:9/mcp',
      tools: [
        {
          id: 'claude',
          name: 'Claude Code',
          detected: false,
          cli: true,
          text: "claude mcp add --scope user --transport stdio jusprin -- '/tmp/jusprin-mcp' --discovery '/tmp/mcp.json'",
          configPath: '/home/u/.claude.json',
        },
        {
          id: 'cursor',
          name: 'Cursor',
          detected: true,
          cli: false,
          text: '{"mcpServers":{"jusprin":{"command":"/tmp/jusprin-mcp","args":["--discovery","/tmp/mcp.json"]}}}',
          configPath: '/tmp/mcp.json',
        },
      ],
    });
  }

  function openCursorReview() {
    openToolCatalog();
    fireEvent.click(screen.getByRole('button', { name: 'Connect Cursor' }));
    host.deliver('mcp_preview', {
      toolId: 'cursor',
      path: '/tmp/mcp.json',
      previous: '{}',
      next: '{"command":"/tmp/jusprin-mcp","args":["--discovery","/tmp/mcp.json"]}',
      root: 'mcpServers',
    });
  }

  // Each provider has a row of its own (canvas "API key · one row per
  // provider · every state", 2026-10-21), so which ones have a key is visible
  // at a glance.
  const row = (provider: string) => within(screen.getByTestId(`agent-key-${provider}`));

  it('shows one row per provider this build can verify, each with its own Add key', () => {
    openApiKeyScreen();

    expect(screen.queryByRole('tablist')).not.toBeInTheDocument();
    for (const [id, name] of [['anthropic', 'Anthropic'], ['openai', 'OpenAI'], ['openrouter', 'OpenRouter']]) {
      expect(row(id).getByText(name)).toBeInTheDocument();
      expect(row(id).getByText('No key')).toBeInTheDocument();
      expect(row(id).getByRole('button', { name: `Add ${name} key` })).toBeEnabled();
    }
    expect(screen.queryByRole('textbox')).not.toBeInTheDocument();
    expect(screen.getByText('Billed by your provider, not JusPrin. Stored on this computer only.')).toBeInTheDocument();
    expect(screen.getByText('Every chat can pick a model from these providers in its message box.')).toBeInTheDocument();
  });

  it('sends an Anthropic key under its own id', async () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add Anthropic key' }));
    const field = screen.getByLabelText('Anthropic API key');
    expect(field).toHaveAttribute('placeholder', 'Paste your Anthropic key (sk-ant-…)');
    await userEvent.type(field, 'sk-ant-test');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    expect(host.lastOfType('setup_check_key')!.payload).toEqual({ provider: 'anthropic', apiKey: 'sk-ant-test' });
  });

  it('sends the key to the host, locks it while it is checked, and shows it saved once it passes', async () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add OpenAI key' }));

    // An empty field cannot be submitted.
    expect(screen.getByRole('button', { name: 'Check key' })).toBeDisabled();

    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-test-key');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));

    const sent = host.lastOfType('setup_check_key');
    expect(sent).toBeTruthy();
    expect(sent!.payload).toEqual({ provider: 'openai', apiKey: 'sk-test-key' });

    host.deliver('setup_status', { phase: 'checking', provider: 'openai' });
    expect(row('openai').getByText('Checking with OpenAI…')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Check key' })).not.toBeInTheDocument();
    // The key in question stays visible, locked, and only by its known start.
    expect(screen.getByLabelText('OpenAI API key')).toBeDisabled();
    expect(screen.getByLabelText('OpenAI API key')).toHaveValue(`sk-${'•'.repeat(16)}`);
    // Nothing else starts while a check runs.
    expect(screen.getByRole('button', { name: 'Add Anthropic key' })).toBeDisabled();

    host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 812 });
    host.deliver('setup_keys', { keys: [{ provider: 'openai', hint: 'sk-proj-…7Qm2', rejected: false }] });
    expect(screen.queryByLabelText('OpenAI API key')).not.toBeInTheDocument();
    expect(screen.getByTestId('agent-key-openai-standing')).toHaveTextContent('Key saved on this computersk-proj-…7Qm2');
  });

  it('sends an OpenRouter key under its own id', async () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add OpenRouter key' }));

    const field = screen.getByLabelText('OpenRouter API key');
    expect(field).toHaveAttribute('placeholder', 'Paste your OpenRouter key (sk-or-…)');
    await userEvent.type(field, 'sk-or-test');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    expect(host.lastOfType('setup_check_key')!.payload).toEqual({ provider: 'openrouter', apiKey: 'sk-or-test' });

    host.deliver('setup_status', { phase: 'verified', provider: 'openrouter', elapsedMs: 640 });
    host.deliver('setup_keys', { keys: [{ provider: 'openrouter', hint: 'sk-or-…t3st', rejected: false }] });
    expect(screen.getByTestId('agent-key-openrouter-standing')).toHaveTextContent('Key saved on this computer');
  });

  it('opens one key form at a time', () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add Anthropic key' }));
    fireEvent.click(screen.getByRole('button', { name: 'Add OpenAI key' }));
    expect(screen.queryByLabelText('Anthropic API key')).not.toBeInTheDocument();
    expect(screen.getByLabelText('OpenAI API key')).toHaveFocus();
  });

  it('says when a working key could not be stored', () => {
    openApiKeyScreen();
    host.deliver('setup_status', {
      phase: 'verified',
      provider: 'openai',
      elapsedMs: 500,
      warning: 'This key could not be saved, so it will have to be entered again next time JusPrin starts.',
    });

    expect(screen.getByTestId('agent-key-openai-standing')).toHaveTextContent('Working, but only until JusPrin restarts');
    expect(row('openai').getByText(/you’ll need to enter it again after a restart/)).toBeInTheDocument();
    expect(row('openai').getByRole('button', { name: 'Replace OpenAI key' })).toBeEnabled();
  });

  it('keeps a refused key open to correct, and clears the refusal once it changes', async () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add OpenAI key' }));
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-wrong');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));

    host.deliver('setup_status', {
      phase: 'error',
      provider: 'openai',
      error: { code: 'invalid_credentials', message: 'The OpenAI API key was rejected.', retryable: false },
    });

    expect(screen.getByTestId('setup-error')).toHaveTextContent('OpenAI rejected this key. Nothing was saved.');
    // The key is still there to correct, and can be resubmitted.
    expect(screen.getByLabelText('OpenAI API key')).toBeEnabled();
    expect(screen.getByLabelText('OpenAI API key')).toHaveValue('sk-wrong');
    expect(screen.getByRole('button', { name: 'Check key' })).toBeEnabled();

    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'x');
    expect(screen.queryByTestId('setup-error')).not.toBeInTheDocument();
  });

  it('keeps a key that could not be checked locked for another try', async () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add Anthropic key' }));
    await userEvent.type(screen.getByLabelText('Anthropic API key'), 'sk-ant-offline');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    host.deliver('setup_status', {
      phase: 'error',
      provider: 'anthropic',
      error: { code: 'network_error', message: 'The Agent request could not reach the Anthropic service.', retryable: true },
    });

    expect(screen.getByTestId('setup-error')).toHaveTextContent(
      'Couldn’t reach Anthropic to check this key. Nothing was saved. Try again.');
    expect(screen.getByLabelText('Anthropic API key')).toBeDisabled();
    expect(row('anthropic').getByText('No key')).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    expect(host.lastOfType('setup_check_key')!.payload).toEqual({ provider: 'anthropic', apiKey: 'sk-ant-offline' });
  });

  it('abandons a check when the user cancels', async () => {
    openApiKeyScreen();
    fireEvent.click(screen.getByRole('button', { name: 'Add OpenAI key' }));
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-slow');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    host.deliver('setup_status', { phase: 'checking', provider: 'openai' });

    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('setup_cancel')).toBeTruthy();
    expect(screen.queryByLabelText('OpenAI API key')).not.toBeInTheDocument();
  });

  // A stored key: the host sends a hint of it, never the key.
  const savedAnthropic = { provider: 'anthropic', hint: 'sk-ant-…a3F9', rejected: false };

  function openWithKeys(keys: Array<{ provider: string; hint: string; rejected: boolean }>) {
    openApiKeyScreen();
    host.deliver('setup_keys', { keys });
  }

  it('2. key saved: its hint at full contrast, read-only, with Replace and Remove; each provider shows its own', () => {
    openWithKeys([savedAnthropic]);

    expect(screen.getByTestId('agent-key-anthropic-standing')).toHaveTextContent('Key saved on this computer');
    expect(screen.getByTestId('agent-key-anthropic-standing')).toHaveTextContent('sk-ant-…a3F9');
    expect(screen.queryByLabelText('Anthropic API key')).not.toBeInTheDocument();
    expect(row('anthropic').getByRole('button', { name: 'Replace Anthropic key' })).toBeEnabled();
    expect(row('anthropic').getByRole('button', { name: 'Remove Anthropic key' })).toBeEnabled();
    expect(row('anthropic').queryByRole('button', { name: /Add/ })).not.toBeInTheDocument();

    // OpenAI has no key here, so its row offers only Add key.
    expect(row('openai').getByText('No key')).toBeInTheDocument();
    expect(row('openai').queryByRole('button', { name: /Replace|Remove/ })).not.toBeInTheDocument();
  });

  it('3. replacing: an empty focused field, the saved key kept until the new one passes', async () => {
    openWithKeys([savedAnthropic]);
    fireEvent.click(screen.getByRole('button', { name: 'Replace Anthropic key' }));

    const field = screen.getByLabelText('New Anthropic key');
    expect(field).toHaveValue('');
    expect(field).toHaveAttribute('placeholder', 'Paste the new key');
    expect(field).toHaveFocus();
    expect(screen.getByRole('button', { name: 'Check key' })).toBeDisabled();
    expect(row('anthropic').getByText(/until the new key passes/)).toHaveTextContent('Keeps sk-ant-…a3F9 until the new key passes');
    expect(screen.getByTestId('agent-key-anthropic-standing')).toHaveTextContent('sk-ant-…a3F9');

    // Cancel goes back to the saved key without telling the host anything.
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(screen.queryByLabelText('New Anthropic key')).not.toBeInTheDocument();
    expect(host.received.filter((e) => e.type.startsWith('setup_'))).toHaveLength(0);

    fireEvent.click(screen.getByRole('button', { name: 'Replace Anthropic key' }));
    await userEvent.type(screen.getByLabelText('New Anthropic key'), 'sk-ant-new');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    expect(host.lastOfType('setup_check_key')!.payload).toEqual({ provider: 'anthropic', apiKey: 'sk-ant-new' });
  });

  it('4. checking a replacement: the saved key stays on show, with Checking… and Cancel', async () => {
    openWithKeys([savedAnthropic]);
    fireEvent.click(screen.getByRole('button', { name: 'Replace Anthropic key' }));
    await userEvent.type(screen.getByLabelText('New Anthropic key'), 'sk-ant-new');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    host.deliver('setup_status', { phase: 'checking', provider: 'anthropic' });

    expect(screen.getByTestId('agent-key-anthropic-standing')).toHaveTextContent('Key saved on this computer');
    expect(screen.getByTestId('agent-key-anthropic-standing')).toHaveTextContent('sk-ant-…a3F9');
    expect(screen.getByText('Checking with Anthropic…')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /Replace|Remove/ })).not.toBeInTheDocument();

    // A new key the provider refuses leaves the saved one, and the new one to correct.
    host.deliver('setup_status', {
      phase: 'error',
      provider: 'anthropic',
      error: { code: 'invalid_credentials', message: 'The Anthropic API key was rejected.', retryable: false },
    });
    expect(screen.getByTestId('setup-error')).toHaveTextContent('Anthropic rejected this key. Nothing was saved.');
    expect(screen.getByLabelText('New Anthropic key')).toHaveValue('sk-ant-new');
    expect(row('anthropic').getByText(/until the new key passes/)).toBeInTheDocument();
  });

  it('5. key rejected: says so beside the refused key, with Replace and Remove', () => {
    openWithKeys([{ ...savedAnthropic, rejected: true }]);

    const standing = screen.getByTestId('agent-key-anthropic-standing');
    expect(standing).toHaveTextContent('The provider rejected this key');
    expect(standing).toHaveTextContent('sk-ant-…a3F9');
    expect(standing).toHaveClass('agent-row-status--rejected');
    expect(row('anthropic').getByRole('button', { name: 'Replace Anthropic key' })).toBeEnabled();
    expect(row('anthropic').getByRole('button', { name: 'Remove Anthropic key' })).toBeEnabled();
    expect(row('anthropic').getByText('Replace it with a working key, or remove it.')).toBeInTheDocument();
  });

  it('Remove asks the host to delete the key, and the row follows what the host says is left', () => {
    openWithKeys([savedAnthropic]);
    fireEvent.click(screen.getByRole('button', { name: 'Remove Anthropic key' }));
    expect(host.lastOfType('setup_remove_key')!.payload).toEqual({ provider: 'anthropic' });

    host.deliver('setup_keys', { keys: [] });
    expect(screen.queryByTestId('agent-key-anthropic-standing')).not.toBeInTheDocument();
    expect(row('anthropic').getByRole('button', { name: 'Add Anthropic key' })).toBeEnabled();
  });

  it('opens the local AI tool catalog and connects a file-based client', () => {
    openAgentSection();

    expect(host.lastOfType('mcp_catalog')).toBeTruthy();

    host.deliver('mcp_catalog', {
      helperPresent: true,
      liveUrl: 'http://127.0.0.1:9/mcp',
      tools: [
        { id: 'claude', name: 'Claude Code', detected: false, cli: true, text: 'claude mcp add -- discovery' },
        {
          id: 'cursor',
          name: 'Cursor',
          detected: true,
          cli: false,
          text: '{"mcpServers":{"jusprin":{"command":"/tmp/jusprin-mcp","args":["--discovery","/tmp/mcp.json"]}}}',
          configPath: '/tmp/mcp.json',
        },
      ],
    });
    fireEvent.click(screen.getByRole('button', { name: 'Connect Cursor' }));
    expect(host.lastOfType('mcp_preview')?.payload).toMatchObject({ toolId: 'cursor' });
    host.deliver('mcp_preview', {
      toolId: 'cursor',
      path: '/tmp/mcp.json',
      previous: '{}',
      next: '{"command":"/tmp/jusprin-mcp","args":["--discovery","/tmp/mcp.json"]}',
      root: 'mcpServers',
    });
    expect(screen.getByTestId('setup-local-review')).toBeInTheDocument();
    expect(screen.getByText('/tmp/mcp.json')).toBeInTheDocument();
    expect(screen.queryByText(/http:\/\/127.0.0.1/)).not.toBeInTheDocument();
    // The screen asks permission, and promises the backup before the user
    // commits rather than after.
    expect(screen.getByText(/Allow this AI tool to read and change the open project/)).toBeInTheDocument();
    expect(screen.getByText(/The existing file is backed up first/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(host.lastOfType('mcp_connect')?.payload).toMatchObject({ toolId: 'cursor' });
    host.deliver('mcp_status', { phase: 'writing', toolId: 'cursor' });
    expect(screen.getByTestId('setup-local-saving')).toBeInTheDocument();
    expect(screen.getByText(/Can’t be stopped once started/)).toBeInTheDocument();
    host.deliver('mcp_status', { phase: 'saved', toolId: 'cursor', backup: '/tmp/mcp.json.bak' });
    expect(screen.getByTestId('setup-local-saved')).toBeInTheDocument();
    expect(screen.getByText(/Backup:/)).toBeInTheDocument();
    // The two operational facts reach the config-file branch, which never
    // sees the prepare screen's footnote.
    expect(screen.getByText(/Keep JusPrin open while Cursor uses it/)).toBeInTheDocument();
    expect(screen.getByText(/won’t need to set this up again/)).toBeInTheDocument();
  });

  it('lists each AI tool as a row with its own Connect…, and looks again on request', () => {
    openToolCatalog();
    const claude = within(screen.getByTestId('agent-tool-claude'));
    expect(claude.getByText('Not found on this computer')).toBeInTheDocument();
    expect(within(screen.getByTestId('agent-tool-cursor')).getByText('Found on this computer')).toBeInTheDocument();
    expect(screen.queryByRole('radio')).not.toBeInTheDocument();
    // A tool that was not found can still be connected.
    expect(claude.getByRole('button', { name: 'Connect Claude Code' })).toBeEnabled();

    const scans = host.received.filter((e) => e.type === 'mcp_catalog').length;
    fireEvent.click(screen.getByRole('button', { name: 'Look again' }));
    expect(host.received.filter((e) => e.type === 'mcp_catalog')).toHaveLength(scans + 1);
  });

  it('copies a CLI command instead of embedding a loopback URL', async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.assign(navigator, { clipboard: { writeText } });

    openAgentSection();
    host.deliver('mcp_catalog', {
      helperPresent: true,
      tools: [
        {
          id: 'claude',
          name: 'Claude Code',
          detected: true,
          cli: true,
          text: "claude mcp add --scope user --transport stdio jusprin -- '/tmp/jusprin-mcp' --discovery '/tmp/mcp.json'",
        },
      ],
    });
    fireEvent.click(screen.getByRole('button', { name: 'Connect Claude Code' }));
    expect(screen.getByTestId('setup-local-prepare')).toBeInTheDocument();
    expect(screen.getByText(/--discovery/)).toBeInTheDocument();
    // A user running the command by hand is told what to do with it.
    expect(screen.getByText('Running it yourself? Paste it in a terminal, then restart Claude Code.')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Copy' }));
    expect(writeText).toHaveBeenCalled();
    expect(await screen.findByRole('button', { name: 'Copied' })).toBeInTheDocument();
    expect(screen.getByTestId('setup-local-prepare')).toBeInTheDocument();
  });

  it('says there is no JusPrin entry yet instead of rendering a serialised null', () => {
    openToolCatalog();
    fireEvent.click(screen.getByRole('button', { name: 'Connect Cursor' }));
    // The host omits `previous` when the file holds no JusPrin entry — which
    // is what every user sees the first time, on the screen where they grant
    // an external tool access to their project.
    host.deliver('mcp_preview', {
      toolId: 'cursor',
      path: '/tmp/mcp.json',
      next: '{"command":"/tmp/jusprin-mcp"}',
      root: 'mcpServers',
    });

    const review = screen.getByTestId('setup-local-review');
    expect(review).toBeInTheDocument();
    expect(review).not.toHaveTextContent('null');
    expect(screen.getByText('No JusPrin entry yet — this adds one.')).toBeInTheDocument();
    // The new entry is still shown.
    expect(review).toHaveTextContent('"command"');
  });

  it('reports a failed write on the review screen without discarding the diff', () => {
    openCursorReview();
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    // The host reports the write starting before it reports the failure.
    host.deliver('mcp_status', { phase: 'writing', toolId: 'cursor' });
    host.deliver('mcp_status', {
      phase: 'error',
      toolId: 'cursor',
      error: { code: 'write_failed', message: 'Permission denied', retryable: true },
    });

    // The user stays where they were, still looking at the change.
    expect(screen.getByTestId('setup-local-review')).toBeInTheDocument();
    expect(screen.getByTestId('setup-local-review')).toHaveTextContent('"command"');
    expect(screen.getByTestId('setup-local-error')).toHaveTextContent('Can’t write /tmp/mcp.json.');
    expect(screen.getByRole('button', { name: 'Connect' })).toBeInTheDocument();

    // The file the user has to go and look at can be revealed, by tool id.
    fireEvent.click(screen.getByRole('button', { name: 'Show file' }));
    expect(host.lastOfType('reveal_path')?.payload).toMatchObject({ toolId: 'cursor' });
  });

  it('reports a missing client on the prepare screen without discarding the command', () => {
    openToolCatalog();
    fireEvent.click(screen.getByRole('button', { name: 'Connect Claude Code' }));
    fireEvent.click(screen.getByRole('button', { name: 'Connect…' }));
    // The host reports the write starting before it reports the failure.
    host.deliver('mcp_status', { phase: 'writing', toolId: 'claude' });
    host.deliver('mcp_status', {
      phase: 'error',
      toolId: 'claude',
      error: { code: 'cli_missing', message: 'not installed', retryable: true },
    });

    expect(screen.getByTestId('setup-local-prepare')).toBeInTheDocument();
    expect(screen.getByText(/--discovery/)).toBeInTheDocument();
    const banner = screen.getByTestId('setup-local-error');
    expect(banner).toHaveTextContent('Claude Code isn’t installed.');
    // The way out for this one is to take the command away and run it later.
    expect(within(banner).getByRole('button', { name: 'Copy' })).toBeInTheDocument();
    // Nothing to reveal here, and no raw output to unfold.
    expect(screen.queryByRole('button', { name: 'Show file' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /Technical output/ })).not.toBeInTheDocument();
  });

  it('offers a fresh look at a file that changed under the review', () => {
    openCursorReview();
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    // The host reports the write starting before it reports the failure.
    host.deliver('mcp_status', { phase: 'writing', toolId: 'cursor' });
    host.deliver('mcp_status', {
      phase: 'error',
      toolId: 'cursor',
      error: { code: 'stale_preview', message: 'changed after the preview', retryable: true },
    });
    expect(screen.getByTestId('setup-local-error')).toHaveTextContent('The file changed since you reviewed it.');

    fireEvent.click(screen.getByRole('button', { name: 'Review again' }));
    expect(host.lastOfType('mcp_preview')?.payload).toMatchObject({ toolId: 'cursor' });
    expect(screen.queryByTestId('setup-local-error')).not.toBeInTheDocument();
  });

  it('keeps the raw output of a timed-out command behind a disclosure', () => {
    openToolCatalog();
    fireEvent.click(screen.getByRole('button', { name: 'Connect Claude Code' }));
    fireEvent.click(screen.getByRole('button', { name: 'Connect…' }));
    // The host reports the write starting before it reports the failure.
    host.deliver('mcp_status', { phase: 'writing', toolId: 'claude' });
    host.deliver('mcp_status', {
      phase: 'error',
      toolId: 'claude',
      diagnostic: 'claude: the command timed out after 30s',
      error: { code: 'timeout', message: 'the command timed out after 30s', retryable: true },
    });

    expect(screen.getByTestId('setup-local-error')).toHaveTextContent('Timed out.');
    expect(screen.queryByText(/timed out after 30s/)).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: /Technical output/ }));
    expect(screen.getByText(/timed out after 30s/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Show file' }));
    expect(host.lastOfType('reveal_path')?.payload).toMatchObject({ toolId: 'claude' });
  });

  it('shows raw command output once, behind the disclosure rather than as the body', () => {
    openToolCatalog();
    fireEvent.click(screen.getByRole('button', { name: 'Connect Claude Code' }));
    fireEvent.click(screen.getByRole('button', { name: 'Connect…' }));
    // A failed CLI reports the same text through both fields. The banner is
    // the title alone; the raw text belongs in the disclosure.
    host.deliver('mcp_status', {
      phase: 'error',
      toolId: 'claude',
      diagnostic: 'exit 1: no such command',
      error: { code: 'command_failed', message: 'exit 1: no such command', retryable: true },
    });

    expect(screen.getByTestId('setup-local-error')).toHaveTextContent('The setup command failed.');
    expect(screen.getByTestId('setup-local-error')).not.toHaveTextContent('no such command');
    fireEvent.click(screen.getByRole('button', { name: /Technical output/ }));
    expect(screen.getByText('exit 1: no such command')).toBeInTheDocument();
  });

  it('drops a stale error when the user picks a different tool', () => {
    openToolCatalog();
    fireEvent.click(screen.getByRole('button', { name: 'Connect Claude Code' }));
    fireEvent.click(screen.getByRole('button', { name: 'Connect…' }));
    // The host reports the write starting before it reports the failure.
    host.deliver('mcp_status', { phase: 'writing', toolId: 'claude' });
    host.deliver('mcp_status', {
      phase: 'error',
      toolId: 'claude',
      error: { code: 'cli_missing', message: 'not installed', retryable: true },
    });
    expect(screen.getByTestId('setup-local-error')).toBeInTheDocument();

    // The banner must not follow the user onto a screen it no longer applies to.
    fireEvent.click(screen.getByRole('button', { name: 'Back to setup options' }));
    fireEvent.click(screen.getByRole('button', { name: 'Connect Cursor' }));
    host.deliver('mcp_preview', {
      toolId: 'cursor',
      path: '/tmp/mcp.json',
      previous: '{}',
      next: '{"command":"/tmp/jusprin-mcp"}',
      root: 'mcpServers',
    });
    expect(screen.getByTestId('setup-local-review')).toBeInTheDocument();
    expect(screen.queryByTestId('setup-local-error')).not.toBeInTheDocument();
  });
});
