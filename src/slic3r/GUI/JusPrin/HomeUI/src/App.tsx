import { useEffect, useMemo, useReducer, useState } from 'react';
import type { DragEvent } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import type { HistoryEntry, PrinterInfo } from './bridge/protocol';
import { ProjectCard } from './components/ProjectCard';
import { PrinterCard, PrinterChip } from './components/PrinterCard';
import { ConfirmDialog } from './components/ConfirmDialog';
import { PrintHistory } from './components/PrintHistory';
import type { PrintHistoryActions } from './components/PrintHistory';
import type { PrinterActions } from './components/PrinterMenu';
import { ChevronDownGlyph, ChevronRightGlyph, HistoryGlyph, PlusGlyph, SearchGlyph, SettingsGlyph, UploadGlyph } from './components/Glyphs';
import { initialState, reduce } from './state/store';
import type { Screen } from './state/store';
import { applyAppearance } from './tokens';

// What Open… and a drop open, as OrcaSlicer's own file dialog filters them.
const FORMATS = ['STL', '3MF', 'STEP', 'OBJ', 'AMF', 'SVG', 'ZIP'];

// Search and sort earn their place once the gallery is too long to scan.
const SEARCH_FROM = 8;

type SortOrder = 'recent' | 'name';

// A printer that is printing or needs reconnecting opens as its full card.
// Every other one is a chip until the person opens it.
function startsCollapsed(printer: PrinterInfo): boolean {
  return printer.state !== 'printing' && printer.connectionAction !== 'reconnect';
}

// A new screen of the history starts from its own filters, not the last one's.
function screenKey(screen: Screen): string {
  return JSON.stringify(screen);
}

function readAsDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : '');
    reader.onerror = () => reject(reader.error ?? new Error('read failed'));
    reader.readAsDataURL(file);
  });
}

// Home is the screen before a project: a "JusPrin › Home" bar, then the
// gallery in the width that is left and the printer column as a fixed rail.
// The page draws that bar itself because the shell hides its project header
// while Home is on screen: those controls act on an open project.
export function App({ getTransport }: { getTransport: () => Transport | null }) {
  const [state, dispatch] = useReducer(reduce, initialState);
  const [query, setQuery] = useState('');
  const [sort, setSort] = useState<SortOrder>('recent');
  // Printers the person opened from a chip, by id. They stay open through
  // the host's pushes, so a card does not fold away under an open menu.
  const [opened, setOpened] = useState<ReadonlySet<string>>(new Set());

  const client = useMemo(
    () =>
      new BridgeClient({
        getTransport,
        onEnvelope: (envelope) => dispatch({ kind: 'envelope', envelope }),
        onConnectionChange: (connection: ConnectionState, detail?: string) =>
          dispatch({ kind: 'connection', state: connection, detail }),
      }),
    [getTransport],
  );

  useEffect(() => {
    client.start();
  }, [client]);

  // Each action clears the last refusal first: its message was about the
  // previous request, not this one.
  const printerActions = useMemo<PrinterActions>(() => {
    const send = (
      type: 'open_printer_settings' | 'rename_printer' | 'remove_printer' | 'connect_printer' | 'calibrate_printer',
      payload: object,
    ) => {
      dispatch({ kind: 'printer_action' });
      client.send(type, payload);
    };
    return {
      onConnect: (id) => send('connect_printer', { id }),
      onOpenSettings: (id) => send('open_printer_settings', { id }),
      onCalibrate: (id) => send('calibrate_printer', { id }),
      onRename: (id, name) => send('rename_printer', name === undefined ? { id } : { id, name }),
      onRemove: (id) => send('remove_printer', { id }),
    };
  }, [client]);

  useEffect(() => {
    applyAppearance(state.appearance);
  }, [state.appearance]);

  // A printer the conversation just added opens as its full card and stays
  // open after the push that named it.
  useEffect(() => {
    const added = state.printers.find((printer) => printer.name === state.highlightPrinter);
    if (added) setOpened((ids) => new Set(ids).add(added.id));
  }, [state.highlightPrinter, state.printers]);

  // The host lists projects newest first, which is the Recent order.
  const searchable = state.projects.length >= SEARCH_FROM;
  const shown = useMemo(() => {
    const needle = searchable ? query.trim().toLocaleLowerCase() : '';
    const matching = needle
      ? state.projects.filter((project) => project.name.toLocaleLowerCase().includes(needle))
      : state.projects;
    return searchable && sort === 'name' ? [...matching].sort((a, b) => a.name.localeCompare(b.name)) : matching;
  }, [state.projects, searchable, query, sort]);

  // The Print history asks for its entries each time it opens, and again
  // whenever the host's state moves under it: a print started or finished.
  const onHistory = state.screen.kind === 'history';
  useEffect(() => {
    if (onHistory) client.send('history_request', {});
  }, [client, onHistory, state.stateCount]);

  // The shell's own Ctrl+, cannot see keys while this page has the focus.
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key === ',') {
        event.preventDefault();
        client.send('open_preferences', {});
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [client]);

  if (state.connection !== 'connected' && !state.loaded) {
    return (
      <div className="app connecting">
        <h1>Connecting…</h1>
        <p>Starting Home.</p>
        {state.connectionDetail && (
          <details>
            <summary>Diagnostics</summary>
            <p className="footnote">{state.connectionDetail}</p>
          </details>
        )}
      </div>
    );
  }

  const openHistory = (filter: { projectId?: string; printerId?: string } = {}) =>
    dispatch({ kind: 'show', screen: { kind: 'history', back: 'home', ...filter } });

  if (state.screen.kind === 'history') {
    const screen = state.screen;
    const actions: PrintHistoryActions = {
      // A leaf: back to the gallery, or to the project that opened it.
      onBack: () => (screen.back === 'project' ? client.send('history_back', {}) : dispatch({ kind: 'show', screen: { kind: 'home' } })),
      onOpenProject: (id) => client.send('open_project', { id }),
      onOpenInChat: (entry: HistoryEntry) =>
        client.send('open_in_chat', { projectId: entry.projectId, conversationId: entry.conversationId, markerId: entry.markerId }),
      onPrintAgain: (entry: HistoryEntry) => client.send('print_again', { projectId: entry.projectId, hash: entry.hash }),
      onExport: (entry: HistoryEntry, beside?: string) =>
        client.send('export_gcode', { projectId: entry.projectId, hash: entry.hash, ...(beside ? { beside } : {}) }),
    };
    return (
      <PrintHistory key={screenKey(screen)} screen={screen} entries={state.history} printers={state.printers} actions={actions} />
    );
  }

  // The whole page takes a dropped file, which opens as a new project. The
  // page sees a file's name and bytes, not where it came from.
  const openDropped = (event: DragEvent) => {
    const files = Array.from(event.dataTransfer.files ?? []);
    event.preventDefault();
    if (files.length === 0) return;
    Promise.all(files.map((file) => readAsDataUrl(file).then((dataBase64) => ({ name: file.name, dataBase64 }))))
      .then((read) => client.send('open_dropped_files', { files: read }))
      .catch(() => {
        // A file the browser could not read opens nothing.
      });
  };

  // Every file opens as a new project, so Open… is the primary action; a
  // blank project, built up part by part, is the quiet choice beside it.
  const newProject = (
    <button type="button" className="button-secondary button-secondary-small" onClick={() => client.send('new_project', {})}>
      <PlusGlyph />
      New
    </button>
  );
  const openFile = (
    <button type="button" className="button-primary" onClick={() => client.send('open_file', {})}>
      <UploadGlyph />
      Open…
    </button>
  );

  // Nothing saved yet: the gallery is one drop target that carries the two
  // actions itself, so the header has none and Open… stays the only primary.
  const firstRun = !state.projectsProblem && state.projects.length === 0;
  const firstPrinter = state.printers[0]?.name;

  return (
    <div className="app" onDragOver={(event) => event.preventDefault()} onDrop={openDropped}>
      <header className="home-header">
        <span className="home-header-path">
          <span className="home-header-brand">JusPrin</span>
          <ChevronRightGlyph />
          <span className="home-header-page">Home</span>
        </span>
        <button type="button" className="preferences-button" aria-label="Preferences" title="Preferences (Ctrl+,)"
          onClick={() => client.send('open_preferences', {})}>
          <SettingsGlyph />
        </button>
      </header>
      <div className="home-body">
        <main className="gallery">
          <header className="gallery-header">
            <span className="gallery-title">
              <span className="section-label">Projects</span>
              {!state.projectsProblem && <span className="section-count">{state.projects.length}</span>}
            </span>
            {!firstRun && (
              <div className="gallery-actions">
                <button type="button" className="history-header-link" onClick={() => openHistory()}>
                  <HistoryGlyph />
                  Print history
                </button>
                {searchable && (
                  <>
                    <label className="gallery-search">
                      <SearchGlyph />
                      <input
                        type="search"
                        placeholder="Search projects"
                        aria-label="Search projects"
                        value={query}
                        onChange={(event) => setQuery(event.target.value)}
                      />
                    </label>
                    <span className="gallery-sort">
                      <select aria-label="Sort projects" value={sort} onChange={(event) => setSort(event.target.value as SortOrder)}>
                        <option value="recent">Recent</option>
                        <option value="name">Name</option>
                      </select>
                      <ChevronDownGlyph />
                    </span>
                    <span className="gallery-divider" aria-hidden="true" />
                  </>
                )}
                {newProject}
                {openFile}
              </div>
            )}
          </header>
          {state.projectsProblem ? (
            <p className="empty" role="alert">{state.projectsProblem}</p>
          ) : firstRun ? (
            <section className="first-run">
              <UploadGlyph large />
              <div className="first-run-text">
                <h2>Drop a model or project to start</h2>
                <p>
                  {FORMATS.slice(0, -1).join(', ')} or {FORMATS[FORMATS.length - 1]}. JusPrin asks what the part is for, then
                  plans the print{firstPrinter ? ` for your ${firstPrinter}` : ''}.
                </p>
              </div>
              <div className="first-run-actions">
                {newProject}
                {openFile}
              </div>
              <p className="first-run-hint">Your projects will appear here, most recent first.</p>
            </section>
          ) : (
            <>
              <div className="project-grid">
                {/* The whole page takes a drop; this tile is where it says so. */}
                <button type="button" className="drop-tile" onClick={() => client.send('open_file', {})}>
                  <UploadGlyph large />
                  <span className="drop-tile-title">Drop a model or project here</span>
                  <span className="drop-tile-formats">{FORMATS.join(' · ')}</span>
                </button>
                {shown.map((project) => (
                  <ProjectCard
                    key={project.id}
                    project={project}
                    onOpen={(id) => client.send('open_project', { id })}
                    onHistory={(projectId) => openHistory({ projectId })}
                  />
                ))}
              </div>
              {shown.length === 0 && <p className="empty">No projects match “{query.trim()}”.</p>}
            </>
          )}
        </main>
        <aside className="printer-column">
          <span className="section-label">Printers</span>
          {state.printerError && (
            <p className="printer-error" role="alert">
              {state.printerError.message}
            </p>
          )}
          {state.printers.map((printer) =>
            startsCollapsed(printer) && !opened.has(printer.id) && state.highlightPrinter !== printer.name ? (
              <PrinterChip key={printer.id} printer={printer} onExpand={(id) => setOpened((ids) => new Set(ids).add(id))} />
            ) : (
              <PrinterCard
                key={printer.id}
                printer={printer}
                otherNames={state.printers.filter((other) => other.id !== printer.id).map((other) => other.name)}
                actions={printerActions}
                onLaunchMonitor={(id) => client.send('launch_monitor', { id })}
                onHistory={(printerId) => openHistory({ printerId })}
                highlighted={state.highlightPrinter === printer.name}
              />
            ),
          )}
          <button type="button" className="add-printer" onClick={() => client.send('add_printer', {})}>
            + Add printer
          </button>
        </aside>
      </div>
      {state.confirmation && (
        <ConfirmDialog
          question={state.confirmation}
          onAnswer={(confirmed) => {
            client.send('confirm_answer', { requestId: state.confirmation!.requestId, confirmed });
            dispatch({ kind: 'confirmation_answered' });
          }}
        />
      )}
    </div>
  );
}
