// The Preferences window's page against a scripted host: it draws what the
// app says is stored, sends one preferences_action per change, and draws the
// Agent's connection rows with the same components Agent setup uses.

import { beforeEach, describe, expect, it } from 'vitest';
import { act, render, screen, within } from '@testing-library/react';
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
    updates: { version: '2.0.0', status: 'notChecked', checked: null, available: null, checkUpdates: true, updateProfiles: true },
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

  it('shows the account sign-in disabled, as setup does, and points to Agent', async () => {
    render(<PreferencesApp getTransport={() => host.transport} />);
    connect(host);
    expect(screen.getByRole('button', { name: 'Continue with JusPrin' })).toBeDisabled();
    await userEvent.click(within(screen.getByRole('main')).getByRole('button', { name: 'Agent' }));
    expect(screen.getByRole('heading', { name: 'Agent', level: 2 })).toBeInTheDocument();
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
    await userEvent.click(screen.getByRole('checkbox', { name: /Update built-in printer/ }));

    expect(actionsSent(host)).toEqual([
      { action: 'set', name: 'theme', value: 'dark' },
      { action: 'set', name: 'units', value: 'in' },
      { action: 'set', name: 'language', value: 'de_DE' },
      { action: 'set', name: 'zoomToPointer', value: false },
      { action: 'set', name: 'leftDrag', value: 'pan' },
      { action: 'set', name: 'updateProfiles', value: false },
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
