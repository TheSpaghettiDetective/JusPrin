import { useEffect, useMemo, useReducer } from 'react';
import type { DragEvent } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import { ProjectCard } from './components/ProjectCard';
import { PrinterCard } from './components/PrinterCard';
import { ConfirmDialog } from './components/ConfirmDialog';
import type { PrinterActions } from './components/PrinterMenu';
import { SettingsGlyph, UploadGlyph } from './components/Glyphs';
import { initialState, reduce } from './state/store';
import { applyAppearance } from './tokens';

function readAsDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : '');
    reader.onerror = () => reject(reader.error ?? new Error('read failed'));
    reader.readAsDataURL(file);
  });
}

// Home is the screen before a project: the gallery takes the width that is
// left, the printer column is a fixed rail. The page draws no top bar -- the
// native shell owns the header above this panel, and a second one here would
// duplicate it.
export function App({ getTransport }: { getTransport: () => Transport | null }) {
  const [state, dispatch] = useReducer(reduce, initialState);

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

  return (
    <div className="app" onDragOver={(event) => event.preventDefault()} onDrop={openDropped}>
      <main className="gallery">
        <header className="gallery-header">
          <span className="section-label">Projects</span>
          <div className="gallery-actions">
            {/* Every file opens as a new project; a blank one is File > New Project. */}
            <button type="button" className="button-primary" onClick={() => client.send('open_file', {})}>
              <UploadGlyph />
              Open…
            </button>
          </div>
        </header>
        {state.projectsProblem ? (
          <p className="empty" role="alert">{state.projectsProblem}</p>
        ) : state.projects.length === 0 ? (
          <p className="empty">Drop a model or project here, or Open…</p>
        ) : (
          <div className="project-grid">
            {state.projects.map((project) => (
              <ProjectCard
                key={project.id}
                project={project}
                onOpen={(id) => client.send('open_project', { id })}
              />
            ))}
          </div>
        )}
      </main>
      <aside className="printer-column">
        <div className="printer-column-head">
          <span className="section-label">Printers</span>
          <button type="button" className="preferences-button" aria-label="Preferences" title="Preferences (Ctrl+,)"
            onClick={() => client.send('open_preferences', {})}>
            <SettingsGlyph />
          </button>
        </div>
        {state.printerError && (
          <p className="printer-error" role="alert">
            {state.printerError.message}
          </p>
        )}
        {state.printers.map((printer) => (
          <PrinterCard
            key={printer.id}
            printer={printer}
            otherNames={state.printers.filter((other) => other.id !== printer.id).map((other) => other.name)}
            actions={printerActions}
            onLaunchMonitor={(id) => client.send('launch_monitor', { id })}
            highlighted={state.highlightPrinter === printer.name}
          />
        ))}
        <button type="button" className="add-printer" onClick={() => client.send('add_printer', {})}>
          + Add printer
        </button>
      </aside>
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
