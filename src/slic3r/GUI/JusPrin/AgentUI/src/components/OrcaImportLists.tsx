// The printer list and the result lines an import from OrcaSlicer or from
// JusPrin 1 shows: on the first launch's Printers step (Home page, canvas
// boards 07, 17, 19 and 20) and in File › Import › Import from OrcaSlicer…
// and Import from JusPrin 1… (this page, boards orca-import-02 to 04). Both pages draw this one markup, with its own stylesheet, so the
// two cannot drift apart.

import { useId, useState } from 'react';
import './OrcaImportLists.css';

export interface OrcaPrinterRow {
  name: string;
  // JusPrin has it already, so the import skips it.
  already: boolean;
}

export interface OrcaImportResultLine {
  kind: 'printer' | 'filament' | 'process';
  name: string;
  renamedTo: string;
  imported: boolean;
  // Why it was not imported, as a sentence.
  reason: string;
}

// With `chosen` and `onChoose`, each printer JusPrin doesn't have yet has a
// tick box, and only the ticked ones are imported.
export function OrcaPrinterList({ app, printers, chosen, onChoose, disabled }: {
  // The app the printers are in, as "OrcaSlicer" or "JusPrin 1".
  app: string;
  printers: OrcaPrinterRow[];
  chosen?: string[];
  onChoose?: (name: string, ticked: boolean) => void;
  disabled?: boolean;
}) {
  return (
    <ul className="orca-import-list" aria-label={`${app} printers`}>
      {printers.map(({ name, already }, index) => (
        <li key={`${name}:${index}`} className={already ? 'orca-import-row orca-import-row--already' : 'orca-import-row'}>
          {!already && chosen && onChoose ? (
            <label className="orca-import-choice">
              <input type="checkbox" checked={chosen.includes(name)} disabled={disabled}
                onChange={(event) => onChoose(name, event.target.checked)} />
              <span className="orca-import-row-name">{name}</span>
            </label>
          ) : <>
            <span className={`orca-import-icon ${already ? 'orca-import-icon--check' : 'orca-import-icon--printer'}`} aria-hidden="true" />
            <span className="orca-import-row-name">{name}</span>
          </>}
          {already && <span className="orca-import-row-status">Already in JusPrin</span>}
        </li>
      ))}
    </ul>
  );
}

// The filaments and settings an import copies, a row each with its kind on
// the right, so they read as items of their own and not as a heading.
export function OrcaNewList({ filaments, processes }: { filaments: string[]; processes: string[] }) {
  const rows = [...filaments.map((name) => ({ name, kind: 'New filament' })),
    ...processes.map((name) => ({ name, kind: 'New print settings' }))];
  return (
    <ul className="orca-import-list" aria-label="New filaments and print settings">
      {rows.map(({ name, kind }, index) => (
        <li key={`${kind}:${name}:${index}`} className="orca-import-row">
          <span className="orca-import-icon orca-import-icon--new" aria-hidden="true" />
          <span className="orca-import-row-name">{name}</span>
          <span className="orca-import-row-status">{kind}</span>
        </li>
      ))}
    </ul>
  );
}

function status(line: OrcaImportResultLine) {
  if (!line.imported) return line.reason;
  return line.renamedTo ? `Imported as ‘${line.renamedTo}’` : 'Imported';
}

// Each line with its outcome under the name.
export function OrcaOutcomeList({ label, lines }: { label: string; lines: OrcaImportResultLine[] }) {
  return (
    <ul className="orca-import-list" aria-label={label}>
      {lines.map((line, index) => (
        <li key={`${line.kind}:${line.name}:${index}`} className="orca-import-row">
          <span className={`orca-import-icon ${line.imported ? 'orca-import-icon--done' : 'orca-import-icon--warning'}`}
            aria-hidden="true" />
          <span className="orca-import-line">
            <span className="orca-import-row-name">{line.name}</span>
            <span className="orca-import-row-status">{status(line)}</span>
          </span>
        </li>
      ))}
    </ul>
  );
}

// "2 filaments (Generic PLA, Elegoo PETG)": the ones of a kind that came,
// counted and named, or empty when none of that kind came.
function brought(lines: OrcaImportResultLine[], one: string, many: string) {
  const names = lines.filter((line) => line.imported).map((line) => line.renamedTo || line.name);
  if (names.length === 0) return '';
  return `${names.length} ${names.length === 1 ? one : many} (${names.join(', ')})`;
}

const KINDS: [OrcaImportResultLine['kind'], string, string][] = [
  ['printer', 'printer', 'printers'], ['filament', 'filament', 'filaments'], ['process', 'print setting', 'print settings']];

// "1 filament and 1 print setting weren't imported. They stay in OrcaSlicer."
function notImported(skipped: OrcaImportResultLine[], app: string) {
  const counts = KINDS.flatMap(([kind, one, many]) => {
    const count = skipped.filter((line) => line.kind === kind).length;
    return count > 0 ? [`${count} ${count === 1 ? one : many}`] : [];
  });
  const what = counts.length > 1 ? `${counts.slice(0, -1).join(', ')} and ${counts[counts.length - 1]}` : counts[0];
  return skipped.length === 1 ? `${what} wasn’t imported. It stays in ${app}.` : `${what} weren’t imported. They stay in ${app}.`;
}

// What wasn't imported, as one line under the result; each item's name and
// why, only on asking (boards first-run 17 and 17b).
function NotImported({ app, skipped }: { app: string; skipped: OrcaImportResultLine[] }) {
  const [open, setOpen] = useState(false);
  const detailsId = useId();
  return <>
    <div className="orca-import-skipped">
      <span className="orca-import-icon orca-import-icon--warning" aria-hidden="true" />
      <span>{notImported(skipped, app)}</span>
      <button type="button" className="orca-import-details-toggle" aria-expanded={open} aria-controls={detailsId}
        onClick={() => setOpen(!open)}>
        {open ? 'Hide details' : 'Show details'}
      </button>
    </div>
    {open && (
      <ul className="orca-import-details" id={detailsId} aria-label="Not imported">
        {skipped.map((line, index) => (
          <li key={`${line.kind}:${line.name}:${index}`}>
            <span className="orca-import-details-name">{line.name}</span>
            <span className="orca-import-details-reason">{line.reason}</span>
          </li>
        ))}
      </ul>
    )}
  </>;
}

// The printers imported, a line each; under them the filaments and print
// settings imported, counted and named on one line, and what wasn't
// imported on another, the reasons behind "Show details".
export function OrcaImportResultLines({ app, lines }: { app: string; lines: OrcaImportResultLine[] }) {
  const printers = lines.filter((line) => line.kind === 'printer' && line.imported);
  const skipped = lines.filter((line) => !line.imported);
  const also = [brought(lines.filter((line) => line.kind === 'filament'), 'filament', 'filaments'),
    brought(lines.filter((line) => line.kind === 'process'), 'print setting', 'print settings')].filter(Boolean).join(' and ');
  return (
    <div className="orca-import-result">
      {printers.length > 0 && <OrcaOutcomeList label="Imported" lines={printers} />}
      {also && <p className="orca-import-count">Also imported: {also}.</p>}
      {skipped.length > 0 && <NotImported app={app} skipped={skipped} />}
    </div>
  );
}
