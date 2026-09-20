import { useEffect, useMemo, useReducer } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import type { AddedPrinterInfo } from './bridge/protocol';
import { ProjectCard } from './components/ProjectCard';
import { PrinterCard } from './components/PrinterCard';
import type { PrinterActions } from './components/PrinterMenu';
import { PlusGlyph, UploadGlyph } from './components/Glyphs';
import { initialState, reduce } from './state/store';
import { applyAppearance } from './tokens';

// "0.4 mm, Textured PEI Plate · assumed, Bambu PLA Basic · assumed": each
// fact the card assumed says so, in the order the pinned card itself states
// them.
function addedPrinterFacts(added: AddedPrinterInfo): string {
  const facts: [string, boolean][] = [
    [added.nozzleText, added.nozzleAssumed],
    [added.plateText, added.plateAssumed],
    [added.filamentText, added.filamentAssumed],
  ];
  return facts
    .filter(([text]) => text)
    .map(([text, assumed]) => (assumed ? `${text} · assumed` : text))
    .join(', ');
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
    const send = (type: 'open_printer_settings' | 'rename_printer' | 'remove_printer', payload: object) => {
      dispatch({ kind: 'printer_action' });
      client.send(type, payload);
    };
    return {
      onOpenSettings: (id) => send('open_printer_settings', { id }),
      onRename: (id, name) => send('rename_printer', name === undefined ? { id } : { id, name }),
      onRemove: (id) => send('remove_printer', { id }),
    };
  }, [client]);

  useEffect(() => {
    applyAppearance(state.appearance);
  }, [state.appearance]);

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

  return (
    <div className="app">
      <main className="gallery">
        <header className="gallery-header">
          <span className="section-label">Projects</span>
          <div className="gallery-actions">
            <button type="button" className="button-secondary" onClick={() => client.send('import_project', {})}>
              <UploadGlyph />
              Import
            </button>
            <button type="button" className="button-primary" onClick={() => client.send('new_project', {})}>
              <PlusGlyph />
              New
            </button>
          </div>
        </header>
        {state.projects.length === 0 ? (
          <p className="empty">No projects yet.</p>
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
      {/* The conversation panel takes this column while it is open; the
          shell draws it beside the page, in the same place. */}
      {!state.printerPanelOpen && (
      <aside className="printer-column">
        <span className="section-label">Printers</span>
        {state.printerError && (
          <p className="printer-error" role="alert">
            {state.printerError.message}
          </p>
        )}
        {state.addedPrinter && (
          <div className="printer-added-strip">
            <span>
              <b>{state.addedPrinter.name}</b> added · {addedPrinterFacts(state.addedPrinter)}
            </span>
            <button
              type="button"
              className="printer-added-change"
              onClick={() => {
                const id = state.printers.find((printer) => printer.name === state.addedPrinter?.name)?.id;
                if (id) printerActions.onOpenSettings(id);
              }}
            >
              Change
            </button>
            <button
              type="button"
              className="printer-added-dismiss"
              aria-label="Dismiss"
              onClick={() => dispatch({ kind: 'dismiss_added_printer' })}
            >
              ×
            </button>
          </div>
        )}
        {state.printers.map((printer) => (
          <PrinterCard
            key={printer.id}
            printer={printer}
            otherNames={state.printers.filter((other) => other.id !== printer.id).map((other) => other.name)}
            actions={printerActions}
            onLaunchMonitor={(id) => client.send('launch_monitor', { id })}
            highlighted={state.addedPrinter?.name === printer.name}
          />
        ))}
        <button type="button" className="add-printer" onClick={() => client.send('add_printer', {})}>
          + Add printer
        </button>
      </aside>
      )}
    </div>
  );
}
