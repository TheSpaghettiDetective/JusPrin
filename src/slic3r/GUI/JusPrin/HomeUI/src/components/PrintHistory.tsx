import { useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
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
  REPORT_OFFER,
  reportSentDetail,
  rowTime,
  statsLine,
} from '../history';
import {
  AlertGlyph,
  ArrowLeftGlyph,
  CheckGlyph,
  CloseGlyph,
  DownloadGlyph,
  MessageGlyph,
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
  onReport: (entry: HistoryEntry) => void;
}

type HistoryScreen = Extract<Screen, { kind: 'history' }>;

type Row = { at: string; destination: HistoryDestination; print?: undefined } | { at: string; print: HistoryPrint; destination?: undefined };

// An entry's rows in the order they happened: where it went, and what the
// printer reported. A print that is neither printing nor reported finished
// gets no row: nobody knows how it ended, so the page does not say.
function rowsOf(entry: HistoryEntry): Row[] {
  const rows: Row[] = [
    ...entry.destinations.map((destination) => ({ at: destination.at, destination })),
    ...entry.prints.filter((print) => print.finishedAt || print.live).map((print) => ({ at: print.startedAt, print })),
  ];
  return rows.sort((a, b) => (a.at < b.at ? -1 : a.at > b.at ? 1 : 0));
}

function DestinationRow({ entry, row, onExport }: { entry: HistoryEntry; row: HistoryDestination; onExport: PrintHistoryActions['onExport'] }) {
  if (row.kind === 'send') {
    // Where the file went is said only when the print did not start from it.
    const time = rowTime(row.at, entry.createdAt);
    const printer = row.printerName;
    const note = !row.delivered ? t`The printer hasn’t confirmed it got the file.`
      : row.startRequested ? undefined : t`The file is on the printer but was not started.`;
    return (
      <HistoryRow icon={<span className="history-icon-send"><PrinterGlyph /></span>}
        label={time ? t({ message: `Sent to ${printer} at ${time}`, comment: '{time} is a clock time, or a day and a time' }) : t`Sent to ${printer}`}
        detail={note} />
    );
  }
  const where = folderOf(row.path);
  const state = row.check === 'matches' ? t`matches, checked just now`
    : row.check === 'changed' ? t`changed outside JusPrin, checked just now` : t`not found`;
  const file = fileName(row.path);
  const icon = row.check === 'matches'
    ? <span className="history-icon-matches"><CheckGlyph /></span>
    : <span className={row.check === 'changed' ? 'history-icon-changed' : 'history-icon-missing'}><AlertGlyph /></span>;
  // A changed file is the person's: Export again writes a new one beside it.
  // A missing one has nowhere to sit beside, so the person picks the place.
  const again = row.check !== 'matches' && entry.kept && (
    <button type="button" className="history-link" onClick={() => onExport(entry, row.check === 'changed' ? row.path : undefined)}>
      <Trans>Export again</Trans>
    </button>
  );
  return (
    <HistoryRow greyed={row.check !== 'matches'} icon={icon} label={t`Exported ${file}`}
      detail={where ? `${where} · ${state}` : state} action={again || undefined} />
  );
}

// Only what the printer reported: printing, finished.
function PrintRow({ entry, print }: { entry: HistoryEntry; print: HistoryPrint }) {
  const state = print.finishedAt ? t`Finished` : t`Printing`;
  const remaining = print.live?.remainingSeconds ? duration(print.live.remainingSeconds) : '';
  const live = print.live && [
    print.live.percent !== undefined ? `${print.live.percent}%` : '',
    remaining ? t({ message: `${remaining} left`, comment: '{remaining} is a duration, 2h 34m' }) : '',
  ].filter(Boolean).join(' · ');
  const span = printSpan(print.startedAt, print.finishedAt, entry.createdAt);
  return (
    <HistoryRow
      icon={<span className="status-dot printing" aria-hidden="true" />}
      label={<><span className="history-print-state">{state}</span> · {print.printerName}</>}
      detail={live ? `${span} · ${live}` : span}
    />
  );
}

function HistoryRow({ icon, label, detail, action, greyed }: { icon: ReactNode; label: ReactNode; detail?: string; action?: ReactNode; greyed?: boolean }) {
  return (
    <li className={greyed ? 'history-row greyed' : 'history-row'}>
      <span className="history-row-icon">{icon}</span>
      <span className="history-row-text">
        <span className="history-row-label">{label}</span>
        {detail && <span className="history-row-detail">{detail}</span>}
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
      return <span className="history-note"><Trans>This G-code is no longer kept. Open the project and slice it to print it again.</Trans></span>;
    case 'not_for_this_printer':
      return <span className="history-note"><Trans>Not kept for this printer · slice again to print it</Trans></span>;
    case 'changed':
      return <span className="history-note history-note-changed" role="alert"><Trans>Kept file changed on disk · slice again to print it</Trans></span>;
    default:
      return null;
  }
}

function Entry({ entry, showProject, focused, reportsOffered, actions }: { entry: HistoryEntry; showProject: boolean; focused: boolean; reportsOffered: boolean; actions: PrintHistoryActions }) {
  // One report per G-code, and none while it is still printing. An export-only
  // entry is offered too: a print from USB never reports back.
  const canReport = reportsOffered && !entry.report && !entry.prints.some((print) => print.live);
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
          {stats && <span className="history-stats">{stats}</span>}
        </span>
        <span className="history-entry-time">{entryTime(entry.createdAt)}</span>
      </div>
      {(made || entry.markerId) && (
        <div className="history-made">
          {made && t({ message: `Made with ${made}`, comment: '{made} is the printer, filament and print settings, joined by ·' })}
          {made && entry.markerId && ' · '}
          {entry.markerId && (
            <button type="button" className="history-link" onClick={() => actions.onOpenInChat(entry)}>
              <Trans>Open in chat</Trans>
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
        {entry.report && (
          <HistoryRow icon={<MessageGlyph />} label={t`Report sent`} detail={reportSentDetail(entry.report, entry.createdAt)} />
        )}
      </ul>
      <div className="history-actions">
        {entry.printAgain === 'ready' && (
          <button type="button" className="button-primary button-primary-small" onClick={() => actions.onPrintAgain(entry)}>
            <SendGlyph />
            <Trans>Print again</Trans>
          </button>
        )}
        {entry.kept && (
          <button type="button" className="button-secondary button-secondary-small" onClick={() => actions.onExport(entry)}>
            <DownloadGlyph />
            <Trans>Export…</Trans>
          </button>
        )}
        {canReport && (
          <button type="button" className="button-secondary button-secondary-small" onClick={() => actions.onReport(entry)}>
            {REPORT_OFFER}
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
  problem,
  printers,
  reportsOffered,
  actions,
}: {
  screen: HistoryScreen;
  entries?: HistoryEntry[];
  // Set while the projects folder cannot be read: shown instead of the list.
  problem?: string;
  printers: PrinterInfo[];
  reportsOffered: boolean;
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
  // A filter as its chip and its menu row name it.
  const projectFilter = (name: string) => t`Project: ${name}`;
  const printerFilter = (name: string) => t`Printer: ${name}`;

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
      <button type="button" className="history-chip-remove" aria-label={t`Remove filter ${label}`} onClick={remove}>
        <CloseGlyph />
      </button>
    </span>
  );

  // A leaf's bar: the window's back arrow and the title. The arrow's tooltip
  // names where it goes. Preferences is reached from the title bar's menu and
  // Ctrl+,, not from here.
  return (
    <div className="app">
      <header className="home-header">
        <span className="history-header-start">
          <button type="button" className="window-back-button" aria-label={t({ message: 'Back', context: 'navigation' })}
            title={screen.back === 'project' ? t`Back to the project` : t`Back to Home`} onClick={actions.onBack}>
            <ArrowLeftGlyph />
          </button>
          <span className="history-title"><Trans>Print history</Trans></span>
        </span>
      </header>
      <div className="history-body">
        <div className="history-list">
          <div className="history-filters">
            <div className="history-chips">
              {projectId && chip(projectFilter(projectName(projectId)), () => setProjectId(undefined))}
              {printerId && chip(printerFilter(printerName(printerId)), () => setPrinterId(undefined))}
              {canAdd && (
                <span className="history-add">
                  <button type="button" className="history-add-button" aria-expanded={adding} onClick={() => setAdding(!adding)}>
                    <PlusGlyph />
                    {t({ message: 'Filter', comment: 'Button: add a filter to the list' })}
                  </button>
                  {adding && (
                    <div className="printer-menu history-add-menu" role="menu">
                      {projectOptions.map(([id, name]) => (
                        <button key={id} type="button" role="menuitem" onClick={() => { setProjectId(id); setAdding(false); }}>
                          {projectFilter(name)}
                        </button>
                      ))}
                      {printerOptions.map((printer) => (
                        <button key={printer.id} type="button" role="menuitem" onClick={() => { setPrinterId(printer.id); setAdding(false); }}>
                          {printerFilter(printer.name)}
                        </button>
                      ))}
                    </div>
                  )}
                </span>
              )}
            </div>
            <div className="history-tools">
              {entries && !problem && <span className="history-count">{countLine(shown, Boolean(projectId || printerId || query.trim()))}</span>}
              <label className="gallery-search">
                <SearchGlyph />
                <input type="search" placeholder={t`Search prints`} aria-label={t`Search prints`} value={query}
                  onChange={(event) => setQuery(event.target.value)} />
              </label>
            </div>
          </div>
          {problem ? (
            <p className="empty history-problem" role="alert">{problem}</p>
          ) : !entries ? (
            <p className="empty"><Trans>Loading print history…</Trans></p>
          ) : entries.length === 0 ? (
            <p className="empty"><Trans>Nothing printed or exported yet.</Trans></p>
          ) : shown.length === 0 ? (
            <p className="empty"><Trans>No G-codes match.</Trans></p>
          ) : (
            groups.map((group) => (
              <section key={group.label} className="history-day" aria-label={group.label}>
                <h2 className="history-day-label">{group.label}</h2>
                {group.entries.map((entry) => (
                  <Entry key={`${entry.projectId}-${entry.hash}`} entry={entry} showProject={!projectId}
                    focused={entry.hash === screen.focus} reportsOffered={reportsOffered} actions={actions} />
                ))}
              </section>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
