// JusPrin's Preferences window (canvas "JusPrin v2", Preferences, decided
// 2026-10-21): a list of sections on the left and the chosen one beside it.
// The app reads and writes every setting, under the keys OrcaSlicer's own
// dialog uses, and sends what is stored after each change; the page only
// says what to change. Agent and Account draw the same connection rows Agent
// setup draws (AgentConnection.tsx), so the two never differ.

import { useEffect, useState } from 'react';
import {
  DragAction,
  McpCatalogPayload,
  McpPreviewPayload,
  McpStatusPayload,
  McpToolInfo,
  PreferencesPayload,
  PreferencesSection,
  SavedKeyInfo,
  SetupStatusPayload,
} from '../bridge/protocol';
import { AccountRow, ProviderKeys, ToolGroup } from './AgentConnection';
import { ActionMenu } from './ChatNavigation';
import { SetupLocalTool } from './SetupLocalTool';

const SECTIONS: { id: PreferencesSection; label: string; icon: string }[] = [
  { id: 'account', label: 'Account', icon: 'user' },
  { id: 'agent', label: 'Agent', icon: 'bot' },
  { id: 'appearance', label: 'Appearance', icon: 'palette' },
  { id: 'view3d', label: '3D view', icon: 'box' },
  { id: 'projects', label: 'Projects & files', icon: 'folder' },
  { id: 'updates', label: 'Updates', icon: 'refresh-cw' },
];

// What each mouse button's drag can do, in OrcaSlicer's order.
const DRAG_ACTIONS: { value: DragAction; label: string }[] = [
  { value: 'rotate', label: 'Turn the view' },
  { value: 'pan', label: 'Pan' },
  { value: 'none', label: 'Nothing' },
];

export interface PreferencesActions {
  // One setting, by the name the app's settings use.
  set: (name: string, value: string | boolean) => void;
  // A command: choose_downloads, show_versions, open_update, restart,
  // orca_preferences.
  run: (action: string) => void;
  checkKey: (provider: string, apiKey: string) => void;
  cancelCheck: () => void;
  removeKey: (provider: string) => void;
  refreshTools: () => void;
  previewTool: (toolId: string) => void;
  connectTool: (toolId: string) => void;
  revealTool: (toolId: string) => void;
}

export interface PreferencesProps {
  preferences: PreferencesPayload;
  setup: SetupStatusPayload;
  savedKeys: SavedKeyInfo[];
  mcpCatalog: McpCatalogPayload | null;
  mcpPreview: McpPreviewPayload | null;
  mcpStatus: McpStatusPayload;
  actions: PreferencesActions;
}

function Row({ title, detail, children }: { title: string; detail?: React.ReactNode; children?: React.ReactNode }) {
  return (
    <div className="prefs-row">
      <div className="prefs-row-copy">
        <span className="prefs-row-title">{title}</span>
        {detail && <span className="prefs-row-detail">{detail}</span>}
      </div>
      {children}
    </div>
  );
}

function Check({ title, detail, checked, onChange }: {
  title: string; detail: string; checked: boolean; onChange: (checked: boolean) => void;
}) {
  return (
    <div className="prefs-row">
      <button type="button" role="checkbox" aria-checked={checked} className="printer-form-check prefs-check"
        onClick={() => onChange(!checked)}>
        <span className="printer-form-check-box">{checked && <span className="jp-icon jp-icon-check" aria-hidden="true" />}</span>
        <span className="printer-form-check-copy">
          <span>{title}</span>
          <span className="printer-form-check-detail">{detail}</span>
        </span>
      </button>
    </div>
  );
}

function Segmented<T extends string>({ label, options, value, onChange }: {
  label: string; options: { value: T; label: string }[]; value: T; onChange: (value: T) => void;
}) {
  return (
    <div className="jp-segmented prefs-segmented" role="radiogroup" aria-label={label}>
      {options.map((option) => (
        <button key={option.value} type="button" role="radio" aria-checked={option.value === value}
          className={option.value === value ? 'jp-segment selected' : 'jp-segment'} onClick={() => onChange(option.value)}>
          {option.label}
        </button>
      ))}
    </div>
  );
}

function Select<T extends string>({ label, options, value, onChange, searchable = false }: {
  label: string; options: { value: T; label: string }[]; value: T; onChange: (value: T) => void; searchable?: boolean;
}) {
  return (
    <div className="prefs-select">
      <ActionMenu
        appearance="select"
        label={label}
        text={options.find((option) => option.value === value)?.label ?? value}
        items={options.map((option) => ({
          key: option.value,
          label: option.label,
          current: option.value === value,
          onSelect: () => { if (option.value !== value) onChange(option.value); },
        }))}
        search={searchable ? { label: `Find a ${label.toLowerCase()}`, noMatch: (query) => `No ${label.toLowerCase()} matches “${query}”` } : undefined}
      />
    </div>
  );
}

function Group({ title, tag, children }: { title: string; tag?: string; children: React.ReactNode }) {
  return (
    <div className="prefs-group">
      <div className="prefs-group-head">
        <h3 className="prefs-group-title">{title}</h3>
        {tag && <span className="prefs-group-tag">{tag}</span>}
      </div>
      {children}
    </div>
  );
}

function AccountSection({ onAgent }: { onAgent: () => void }) {
  return (
    <section className="prefs-section" aria-label="Account">
      <h2 className="prefs-title">Account</h2>
      <AccountRow />
      <p className="prefs-note">
        Local work never needs an account. Your own API key or AI tool works without one; set those up in{' '}
        <button type="button" className="link prefs-inline-link" onClick={onAgent}>Agent</button>.
      </p>
    </section>
  );
}

function AgentSection({ agent, setup, savedKeys, mcpCatalog, actions, onConnectTool }: {
  agent: PreferencesPayload['agent']; setup: SetupStatusPayload; savedKeys: SavedKeyInfo[];
  mcpCatalog: McpCatalogPayload | null; actions: PreferencesActions; onConnectTool: (tool: McpToolInfo) => void;
}) {
  // The tools are looked for once each time the section opens.
  useEffect(() => {
    actions.refreshTools();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  return (
    <section className="prefs-section" aria-label="Agent">
      <div className="prefs-head">
        <h2 className="prefs-title">Agent</h2>
        <p className="prefs-intro">The ways the Agent can think. Set up as many as you like; each chat picks a model from them.</p>
      </div>
      <AccountRow />
      <ProviderKeys heading setup={setup} savedKeys={savedKeys} onCheck={actions.checkKey} onCancel={actions.cancelCheck}
        onRemove={actions.removeKey} />
      <ToolGroup heading tools={mcpCatalog?.tools ?? []} onConnect={onConnectTool} onLookAgain={actions.refreshTools} />
      <Group title="New chats start with">
        {agent.newChatChoices.length > 0 ? (
          <Row title="Model" detail="Any chat can switch from its message box.">
            <Select label="Model" value={agent.newChatProvider} onChange={(provider) => actions.set('newChatProvider', provider)}
              options={agent.newChatChoices.map((choice) => ({ value: choice.provider, label: `${choice.model} · ${choice.label}` }))} />
          </Row>
        ) : (
          <Row title="Model" detail="Add a key above, and new chats start with that provider’s model." />
        )}
      </Group>
      <div className="prefs-callout">
        <span className="jp-icon jp-icon-lock" aria-hidden="true" />
        <p>
          The provider a chat uses receives your messages, the open project’s settings and anything you attach. JusPrin
          doesn’t send your keys anywhere else.
        </p>
      </div>
    </section>
  );
}

function AppearanceSection({ appearance, set, run }: {
  appearance: PreferencesPayload['appearance']; set: PreferencesActions['set']; run: PreferencesActions['run'];
}) {
  // "Later" puts the offer away for as long as the same language waits.
  const [later, setLater] = useState<string | null>(null);
  return (
    <section className="prefs-section" aria-label="Appearance">
      <h2 className="prefs-title">Appearance</h2>
      <div>
        <Row title="Language" detail="Takes effect when JusPrin restarts. Your project is saved first.">
          <Select label="Language" value={appearance.language} searchable
            options={appearance.languages.map((language) => ({ value: language.code, label: language.name }))}
            onChange={(code) => set('language', code)} />
        </Row>
        {appearance.restartFor && later !== appearance.restartFor && (
          <div className="prefs-callout prefs-restart" role="status">
            <span className="jp-icon jp-icon-refresh-cw" aria-hidden="true" />
            <p>JusPrin will use {appearance.restartFor} after it restarts.</p>
            <button type="button" className="primary" onClick={() => run('restart')}>Restart now</button>
            <button type="button" className="link" onClick={() => setLater(appearance.restartFor)}>Later</button>
          </div>
        )}
        <Row title="Theme" detail="Changes every window, including the 3D view.">
          <Segmented label="Theme" value={appearance.theme} onChange={(theme) => set('theme', theme)}
            options={[{ value: 'light', label: 'Light' }, { value: 'dark', label: 'Dark' }]} />
        </Row>
        <Row title="Units" detail="For sizes, positions and distances.">
          <Segmented label="Units" value={appearance.units} onChange={(units) => set('units', units)}
            options={[{ value: 'mm', label: 'Millimetres' }, { value: 'in', label: 'Inches' }]} />
        </Row>
      </div>
    </section>
  );
}

function View3dSection({ view, set }: { view: PreferencesPayload['view3d']; set: PreferencesActions['set'] }) {
  return (
    <section className="prefs-section" aria-label="3D view">
      <h2 className="prefs-title">3D view</h2>
      <Group title="Moving the view">
        <Row title="Controls" detail="Mouse: drag to turn, right-drag to pan. Touchpad: Alt-drag to turn, Shift-drag to pan.">
          <Select label="Controls" value={view.navigation} onChange={(style) => set('navigation', style)}
            options={[{ value: 'mouse', label: 'Mouse' }, { value: 'touchpad', label: 'Touchpad' }]} />
        </Row>
        <Check title="Zoom toward the pointer" detail="Instead of toward the middle of the view." checked={view.zoomToPointer}
          onChange={(on) => set('zoomToPointer', on)} />
        <Check title="Reverse scroll-wheel zoom" detail="Scroll up to zoom out." checked={view.reverseZoom}
          onChange={(on) => set('reverseZoom', on)} />
        <Check title="Turn freely" detail="Turn the view in any direction. Off keeps the bed level." checked={view.freeCamera}
          onChange={(on) => set('freeCamera', on)} />
      </Group>
      <Group title="Mouse buttons">
        <Row title="Left-drag">
          <Select label="Left-drag" value={view.leftDrag} options={DRAG_ACTIONS} onChange={(action) => set('leftDrag', action)} />
        </Row>
        <Row title="Middle-drag">
          <Select label="Middle-drag" value={view.middleDrag} options={DRAG_ACTIONS} onChange={(action) => set('middleDrag', action)} />
        </Row>
        <Row title="Right-drag">
          <Select label="Right-drag" value={view.rightDrag} options={DRAG_ACTIONS} onChange={(action) => set('rightDrag', action)} />
        </Row>
      </Group>
    </section>
  );
}

function ProjectsSection({ projects, actions }: { projects: PreferencesPayload['projects']; actions: PreferencesActions }) {
  return (
    <section className="prefs-section" aria-label="Projects and files">
      <h2 className="prefs-title">Projects &amp; files</h2>
      <Group title="Version history">
        <Row title="Every project saves itself as you work"
          detail="JusPrin keeps up to 200 versions of each project, about 4 GB at most. Pinned versions are always kept.">
          <button type="button" className="prefs-button" onClick={() => actions.run('show_versions')}>Show folder</button>
        </Row>
      </Group>
      <Group title="Downloads">
        <Row title="Save downloaded models to"
          detail={<code className="prefs-path" title={projects.downloadPath}>{projects.downloadPath || 'Not chosen yet'}</code>}>
          <button type="button" className="prefs-button" onClick={() => actions.run('choose_downloads')}>Change…</button>
        </Row>
      </Group>
      {projects.associations && (
        <Group title="Open with JusPrin" tag="Windows">
          <Check title="3MF projects" detail="Double-clicking a .3mf file opens it here." checked={projects.open3mf}
            onChange={(on) => actions.set('open3mf', on)} />
          <Check title="STL and STEP models" detail="Also .stp and .drc files." checked={projects.openModels}
            onChange={(on) => actions.set('openModels', on)} />
          <Check title="“Open in slicer” buttons on model sites" detail="Printables and Thingiverse send the model straight here."
            checked={projects.openLinks} onChange={(on) => actions.set('openLinks', on)} />
        </Group>
      )}
    </section>
  );
}

function UpdatesSection({ updates, actions }: { updates: PreferencesPayload['updates']; actions: PreferencesActions }) {
  return (
    <section className="prefs-section" aria-label="Updates">
      <h2 className="prefs-title">Updates</h2>
      {updates.available ? (
        <div className="prefs-card prefs-card--update">
          <div className="prefs-row-copy">
            <span className="prefs-card-title">
              <span className="prefs-dot" aria-hidden="true" />
              JusPrin {updates.available.version} is available
            </span>
            <span className="prefs-row-detail">You have {updates.version}. Your projects and settings carry over.</span>
          </div>
          <div className="prefs-card-actions">
            <button type="button" className="primary" onClick={() => actions.run('open_update')}>
              Download
              <span className="jp-icon jp-icon-arrow-up-right" aria-hidden="true" />
            </button>
            <button type="button" className="link" onClick={() => actions.run('open_update')}>What’s new</button>
          </div>
        </div>
      ) : (
        <div className="prefs-card">
          <div className="prefs-row-copy">
            <span className="prefs-card-title">JusPrin {updates.version}</span>
            <span className="prefs-row-detail">JusPrin will look for newer versions itself in a later release.</span>
          </div>
        </div>
      )}
      <Group title="What to check for">
        <Check title="Update built-in printer and filament profiles"
          detail="Only the profiles JusPrin ships. Your own presets never change." checked={updates.updateProfiles}
          onChange={(on) => actions.set('updateProfiles', on)} />
      </Group>
    </section>
  );
}

// The section on screen, kept for this window only: switching the theme
// reloads the page (OrcaSlicer's dark-mode pass walks every child window), and
// the person should stay where they were. A new window starts a new session.
const SECTION_KEY = 'jusprin.preferences.section';

function rememberedSection(): PreferencesSection | null {
  try {
    const stored = window.sessionStorage.getItem(SECTION_KEY);
    return SECTIONS.some((entry) => entry.id === stored) ? (stored as PreferencesSection) : null;
  } catch {
    return null;
  }
}

export function Preferences({ preferences, setup, savedKeys, mcpCatalog, mcpPreview, mcpStatus, actions }: PreferencesProps) {
  // Opened where the app was asked to open it; the list moves on from there.
  const [section, setSection] = useState<PreferencesSection>(() => rememberedSection() ?? preferences.section);
  // The AI tool being connected, which takes the Agent section's place.
  const [tool, setTool] = useState<string | null>(null);
  const go = (next: PreferencesSection) => {
    setSection(next);
    setTool(null);
    try {
      window.sessionStorage.setItem(SECTION_KEY, next);
    } catch {
      // Without storage a reload opens where the window was opened.
    }
  };

  return (
    <div className="prefs" data-testid="preferences">
      <nav className="prefs-nav" aria-label="Preference sections">
        {SECTIONS.map((entry) => (
          <button key={entry.id} type="button" className="prefs-nav-item" aria-current={section === entry.id ? 'page' : undefined}
            onClick={() => go(entry.id)}>
            <span className={`jp-icon jp-icon-${entry.icon}`} aria-hidden="true" />
            <span>{entry.label}</span>
            {entry.id === 'updates' && preferences.updates.available && (
              <span className="prefs-dot prefs-nav-dot" role="img" aria-label="Update available" />
            )}
          </button>
        ))}
        <div className="prefs-orca">
          <button type="button" className="link prefs-orca-link" onClick={() => actions.run('orca_preferences')}>
            OrcaSlicer preferences…
            <span className="jp-icon jp-icon-arrow-up-right" aria-hidden="true" />
          </button>
          <p className="prefs-orca-note">Every OrcaSlicer setting, as OrcaSlicer shows it.</p>
        </div>
      </nav>
      <main className="prefs-content">
        {section === 'account' && <AccountSection onAgent={() => go('agent')} />}
        {section === 'agent' && (tool ? (
          <SetupLocalTool
            startTool={tool}
            catalog={mcpCatalog}
            preview={mcpPreview}
            status={mcpStatus}
            onRefresh={actions.refreshTools}
            onPreview={actions.previewTool}
            onConnect={actions.connectTool}
            onReveal={actions.revealTool}
            onBack={() => setTool(null)}
            onDone={() => setTool(null)}
          />
        ) : (
          <AgentSection agent={preferences.agent} setup={setup} savedKeys={savedKeys} mcpCatalog={mcpCatalog} actions={actions}
            onConnectTool={(chosen) => setTool(chosen.id)} />
        ))}
        {section === 'appearance' && <AppearanceSection appearance={preferences.appearance} set={actions.set} run={actions.run} />}
        {section === 'view3d' && <View3dSection view={preferences.view3d} set={actions.set} />}
        {section === 'projects' && <ProjectsSection projects={preferences.projects} actions={actions} />}
        {section === 'updates' && <UpdatesSection updates={preferences.updates} actions={actions} />}
      </main>
    </div>
  );
}
