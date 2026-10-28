import { useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import type { HistoryDestination, HistoryEntry, HistoryPrint, PrinterInfo } from '../bridge/protocol';
import type { Screen } from '../state/store';
import {
  countLine,
  dayGroup,
  duration,
  entryTime,
  fileName,
  folderOf,
  matches,
  printSpan,
  rowTime,
  shortHash,
  statsLine,
} from '../history';
import {
  AlertGlyph,
  CheckGlyph,
  ChevronLeftGlyph,
  CloseGlyph,
  DownloadGlyph,
  PlusGlyph,
  PrinterGlyph,
  SendGlyph,
  SearchGlyph,
} from './Glyphs';

export interface PrintHistoryActions {
  onBack: () => void;
  onOpenProject: (projectId: string) => void;
  onOpenInChat: (entry: HistoryEntry) => void;
  // `beside` writes next to that earlier export; without it the person picks.
  onExport: (entry: HistoryEntry, beside?: string) => void;
  // OrcaSlicer's send dialog for the kept file; the person presses Send there.
  onPrintAgain: (entry: HistoryEntry) => void;
}

type HistoryScreen = Extract<Screen, { kind: 'history' }>;

type Row = { at: string; destination: HistoryDestination; print?: undefined } | { at: string; print: HistoryPrint; destination?: undefined };

// An entry's rows in the order they happened: where it went, and what the
// printer reported.
function rowsOf(entry: HistoryEntry): Row[] {
  const rows: Row[] = [
    ...entry.destinations.map((destination) => ({ at: destination.at, destination })),
    ...entry.prints.map((print) => ({ at: print.startedAt, print })),
  ];
  return rows.sort((a, b) => (a.at < b.at ? -1 : a.at > b.at ? 1 : 0));
}

function DestinationRow({ entry, row, onExport }: { entry: HistoryEntry; row: HistoryDestination; onExport: PrintHistoryActions['onExport'] }) {
  if (row.kind === 'send') {
    const delivery = row.delivered
      ? `on the printer’s SD · ${row.startRequested ? 'started' : 'not started'}`
      : 'sent, delivery not confirmed';
    return (
      <HistoryRow icon={<span className="history-icon-send"><PrinterGlyph /></span>} label={`Sent to ${row.printerName}`}
        detail={`${rowTime(row.at, entry.createdAt)} · ${delivery}`} />
    );
  }
  const where = folderOf(row.path);
  const state = row.check === 'matches' ? 'matches, checked just now'
    : row.check === 'changed' ? 'changed outside JusPrin, checked just now' : 'not found';
  const icon = row.check === 'matches'
    ? <span className="history-icon-matches"><CheckGlyph /></span>
    : <span className={row.check === 'changed' ? 'history-icon-changed' : 'history-icon-missing'}><AlertGlyph /></span>;
  // A changed file is the person's: Export again writes a new one beside it.
  // A missing one has nowhere to sit beside, so the person picks the place.
  const again = row.check !== 'matches' && entry.kept && (
    <button type="button" className="history-link" onClick={() => onExport(entry, row.check === 'changed' ? row.path : undefined)}>
      Export again
    </button>
  );
  return (
    <HistoryRow greyed={row.check !== 'matches'} icon={icon} label={`Exported ${fileName(row.path)}`}
      detail={where ? `${where} · ${state}` : state} action={again || undefined} />
  );
}

// Only what the printer reported: printing, finished. A print that stopped
// early is neither, and says only when it started.
function PrintRow({ entry, print }: { entry: HistoryEntry; print: HistoryPrint }) {
  const state = print.finishedAt ? 'Finished' : print.live ? 'Printing' : 'Started';
  const live = print.live && [
    print.live.percent !== undefined ? `${print.live.percent}%` : '',
    print.live.remainingSeconds ? `${duration(print.live.remainingSeconds)} left` : '',
  ].filter(Boolean).join(' · ');
  const span = printSpan(print.startedAt, print.finishedAt, entry.createdAt);
  return (
    <HistoryRow
      icon={<span className={state === 'Started' ? 'status-dot' : 'status-dot printing'} aria-hidden="true" />}
      label={<><span className={state === 'Started' ? 'history-print-state started' : 'history-print-state'}>{state}</span> · {print.printerName}</>}
      detail={live ? `${span} · ${live}` : span}
    />
  );
}

function HistoryRow({ icon, label, detail, action, greyed }: { icon: ReactNode; label: ReactNode; detail: string; action?: ReactNode; greyed?: boolean }) {
  return (
    <li className={greyed ? 'history-row greyed' : 'history-row'}>
      <span className="history-row-icon">{icon}</span>
      <span className="history-row-text">
        <span className="history-row-label">{label}</span>
        <span className="history-row-detail">{detail}</span>
      </span>
      {action}
    </li>
  );
}

// Why Print again is not offered, when it is not: a re-slice in every case,
// since a G-code is printed again only from what was kept of it.
function printAgainNote(entry: HistoryEntry) {
  switch (entry.printAgain) {
    case 'not_kept':
      return <span className="history-note">File no longer kept · slice again to print it</span>;
    case 'not_for_this_printer':
      return <span className="history-note">Not kept for this printer · slice again to print it</span>;
    case 'changed':
      return <span className="history-note history-note-changed" role="alert">Kept file changed on disk · slice again to print it</span>;
    default:
      return null;
  }
}

function Entry({ entry, showProject, focused, actions }: { entry: HistoryEntry; showProject: boolean; focused: boolean; actions: PrintHistoryActions }) {
  const stats = statsLine(entry);
  const made = [entry.printer, entry.material, entry.process].filter(Boolean).join(' · ');
  return (
    <article id={`history-${entry.hash}`} className={focused ? 'history-entry focused' : 'history-entry'} aria-label={entry.name}>
      <div className="history-entry-head">
        <span className="history-entry-title">
          {showProject && (
            <>
              <button type="button" className="history-link" onClick={() => actions.onOpenProject(entry.projectId)}>
                {entry.projectName}
              </button>
              <span className="history-sep">·</span>
            </>
          )}
          <span className="history-entry-name">{entry.name}</span>
          <span className="history-hash">{shortHash(entry.hash)}</span>
          {stats && <span className="history-stats">{stats}</span>}
        </span>
        <span className="history-entry-time">{entryTime(entry.createdAt)}</span>
      </div>
      {(made || entry.markerId) && (
        <div className="history-made">
          {made && `Made with ${made}`}
          {made && entry.markerId && ' · '}
          {entry.markerId && (
            <button type="button" className="history-link" onClick={() => actions.onOpenInChat(entry)}>
              Open in chat
            </button>
          )}
        </div>
      )}
      <div className="history-rule" />
      <ul className="history-rows">
        {rowsOf(entry).map((row) =>
          row.destination ? (
            <DestinationRow key={row.destination.id} entry={entry} row={row.destination} onExport={actions.onExport} />
          ) : (
            <PrintRow key={row.print.id} entry={entry} print={row.print} />
          ),
        )}
      </ul>
      <div className="history-actions">
        {entry.printAgain === 'ready' && (
          <button type="button" className="button-primary button-primary-small" onClick={() => actions.onPrintAgain(entry)}>
            <SendGlyph />
            Print again
          </button>
        )}
        {entry.kept && (
          <button type="button" className="button-secondary button-secondary-small" onClick={() => actions.onExport(entry)}>
            <DownloadGlyph />
            Export…
          </button>
        )}
        {printAgainNote(entry)}
      </div>
    </article>
  );
}

// The Print history screen (boards history-01...04): every G-code that left a
// project, one entry each, grouped by day, newest first. The project and the
// printer are filters over the one list, shown as chips the person can
// remove; where the screen was opened from sets the first.
export function PrintHistory({
  screen,
  entries,
  printers,
  actions,
}: {
  screen: HistoryScreen;
  entries?: HistoryEntry[];
  printers: PrinterInfo[];
  actions: PrintHistoryActions;
}) {
  const [projectId, setProjectId] = useState(screen.projectId);
  const [printerId, setPrinterId] = useState(screen.printerId);
  const [query, setQuery] = useState('');
  const [adding, setAdding] = useState(false);

  // The entry the chat's marker named comes into view once its entry is there.
  const focusShown = Boolean(screen.focus && entries?.some((entry) => entry.hash === screen.focus));
  useEffect(() => {
    if (focusShown) document.getElementById(`history-${screen.focus}`)?.scrollIntoView({ block: 'center' });
  }, [focusShown, screen.focus]);

  const projectName = (id: string) => entries?.find((entry) => entry.projectId === id)?.projectName ?? '';
  const printerName = (id: string) => printers.find((printer) => printer.id === id)?.name ?? id;

  const shown = useMemo(() => {
    const needle = query.trim();
    return (entries ?? []).filter((entry) =>
      (!projectId || entry.projectId === projectId) &&
      (!printerId || entry.printerIds.includes(printerId)) &&
      matches(entry, needle));
  }, [entries, projectId, printerId, query]);

  // Each day once, in the order the host sends, which is newest first.
  const groups: { label: string; entries: HistoryEntry[] }[] = [];
  for (const entry of shown) {
    const label = dayGroup(entry.createdAt);
    if (groups.length === 0 || groups[groups.length - 1].label !== label) groups.push({ label, entries: [] });
    groups[groups.length - 1].entries.push(entry);
  }

  const projectOptions = projectId ? [] :
    [...new Map((entries ?? []).map((entry) => [entry.projectId, entry.projectName])).entries()];
  const printerOptions = printerId ? [] :
    printers.filter((printer) => (entries ?? []).some((entry) => entry.printerIds.includes(printer.id)));
  const canAdd = projectOptions.length + printerOptions.length > 0;

  const chip = (label: string, remove: () => void) => (
    <span className="history-chip" key={label}>
      <span>{label}</span>
      <button type="button" className="history-chip-remove" aria-label={`Remove filter ${label}`} onClick={remove}>
        <CloseGlyph />
      </button>
    </span>
  );

  // A leaf's bar: Back and the title. Preferences is reached from the title
  // bar's menu and Ctrl+,, not from here.
  return (
    <div className="app">
      <header className="home-header">
        <span className="history-header-start">
          <button type="button" className="preferences-button" aria-label="Back" title="Back" onClick={actions.onBack}>
            <ChevronLeftGlyph />
          </button>
          <span className="history-title">Print history</span>
        </span>
      </header>
      <div className="history-body">
        <div className="history-list">
          <div className="history-filters">
            <div className="history-chips">
              {projectId && chip(`Project: ${projectName(projectId)}`, () => setProjectId(undefined))}
              {printerId && chip(`Printer: ${printerName(printerId)}`, () => setPrinterId(undefined))}
              {canAdd && (
                <span className="history-add">
                  <button type="button" className="history-add-button" aria-expanded={adding} onClick={() => setAdding(!adding)}>
                    <PlusGlyph />
                    Filter
                  </button>
                  {adding && (
                    <div className="printer-menu history-add-menu" role="menu">
                      {projectOptions.map(([id, name]) => (
                        <button key={id} type="button" role="menuitem" onClick={() => { setProjectId(id); setAdding(false); }}>
                          Project: {name}
                        </button>
                      ))}
                      {printerOptions.map((printer) => (
                        <button key={printer.id} type="button" role="menuitem" onClick={() => { setPrinterId(printer.id); setAdding(false); }}>
                          Printer: {printer.name}
                        </button>
                      ))}
                    </div>
                  )}
                </span>
              )}
            </div>
            <div className="history-tools">
              {entries && <span className="history-count">{countLine(shown, Boolean(projectId || printerId || query.trim()))}</span>}
              <label className="gallery-search">
                <SearchGlyph />
                <input type="search" placeholder="Search prints" aria-label="Search prints" value={query}
                  onChange={(event) => setQuery(event.target.value)} />
              </label>
            </div>
          </div>
          {!entries ? (
            <p className="empty">Loading print history…</p>
          ) : entries.length === 0 ? (
            <p className="empty">Nothing printed or exported yet.</p>
          ) : shown.length === 0 ? (
            <p className="empty">No G-codes match.</p>
          ) : (
            groups.map((group) => (
              <section key={group.label} className="history-day" aria-label={group.label}>
                <h2 className="history-day-label">{group.label}</h2>
                {group.entries.map((entry) => (
                  <Entry key={`${entry.projectId}-${entry.hash}`} entry={entry} showProject={!projectId}
                    focused={entry.hash === screen.focus} actions={actions} />
                ))}
              </section>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
