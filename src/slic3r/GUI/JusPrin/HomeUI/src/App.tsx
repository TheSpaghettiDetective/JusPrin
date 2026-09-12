import { useEffect, useMemo, useReducer } from 'react';
import { BridgeClient, ConnectionState, Transport } from './bridge/client';
import { ProjectCard } from './components/ProjectCard';
import { PrinterCard } from './components/PrinterCard';
import { initialState, reduce } from './state/store';
import { applyAppearance } from './tokens';

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
              Import
            </button>
            <button type="button" className="button-primary" onClick={() => client.send('new_project', {})}>
              + New
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
      <aside className="printer-column">
        <span className="section-label">Printers</span>
        {state.printers.map((printer) => (
          <PrinterCard
            key={printer.id}
            printer={printer}
            onLaunchMonitor={(id) => client.send('launch_monitor', { id })}
          />
        ))}
        <button type="button" className="add-printer" onClick={() => client.send('add_printer', {})}>
          + Add printer
        </button>
      </aside>
    </div>
  );
}
