// The printer list and the result lines an OrcaSlicer import shows: on the
// first launch's Printers step (Home page, canvas boards 07 and 17) and in
// File › Import › Import from OrcaSlicer… (this page, boards orca-import-02
// to 04). Both pages draw this one markup, with its own stylesheet, so the
// two cannot drift apart.

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
export function OrcaPrinterList({ printers, chosen, onChoose, disabled }: {
  printers: OrcaPrinterRow[];
  chosen?: string[];
  onChoose?: (name: string, ticked: boolean) => void;
  disabled?: boolean;
}) {
  return (
    <ul className="orca-import-list" aria-label="OrcaSlicer printers">
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

function status(line: OrcaImportResultLine) {
  if (!line.imported) return line.reason;
  return line.renamedTo ? `Imported as ‘${line.renamedTo}’` : 'Imported';
}

// Printers and processes a line each, the outcome under the name; filaments
// as a count with the names of those imported, and any not imported listed
// so each says why.
export function OrcaImportResultLines({ lines }: { lines: OrcaImportResultLine[] }) {
  const filaments = lines.filter((line) => line.kind === 'filament');
  const imported = filaments.filter((line) => line.imported).map((line) => line.renamedTo || line.name);
  const importedFilaments = imported.length;
  const listed = lines.filter((line) => line.kind !== 'filament' || !line.imported);
  const noun = filaments.length === 1 ? 'filament' : 'filaments';
  return <>
    {listed.length > 0 && (
      <ul className="orca-import-list" aria-label="Imported">
        {listed.map((line, index) => (
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
    )}
    {filaments.length > 0 && (
      <p className="orca-import-count">
        {importedFilaments === filaments.length
          ? `${filaments.length} ${noun} imported`
          : `${importedFilaments} of ${filaments.length} ${noun} imported`}
        {importedFilaments > 0 ? `: ${imported.join(', ')}.` : '.'}
      </p>
    )}
  </>;
}
