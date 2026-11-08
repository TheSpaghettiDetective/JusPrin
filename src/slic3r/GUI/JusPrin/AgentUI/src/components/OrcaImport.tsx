// File › Import › Import from OrcaSlicer… and Import from JusPrin 1…: the
// printers the app's folder has, the ones JusPrin already has marked and
// skipped, the new filaments and settings, what it can't import and why, and
// once imported, what became of each (canvas "JusPrin v2", boards
// orca-import-02 found, 03 imported, 04 nothing new, 05 no folder, 06 nothing
// new but something it can't import, 07 only filaments and settings; JusPrin 1
// works the same way). The window's title bar carries the title; the host
// finds the folder and does the importing.

import { useEffect, useState } from 'react';
import type { OrcaImportLine, OrcaImportPayload } from '../bridge/protocol';
import { afterPaint } from '@shared/afterPaint';
import { OrcaImportResultLines, OrcaNewList, OrcaOutcomeList, OrcaPrinterList } from './OrcaImportLists';

export interface OrcaImportActions {
  // The printers left ticked, by name.
  importPrinters: (printers: string[]) => void;
  chooseFolder: () => void;
  close: () => void;
}

const plural = (count: number, one: string, many: string) => `${count} ${count === 1 ? one : many}`;

// Says whose folder it is, in the Printers step's words, so it isn't taken
// for JusPrin's own: JusPrin 1's folder is called JusPrin.
function Folder({ app, path, looked, onChoose }: { app: string; path: string; looked?: boolean; onChoose?: () => void }) {
  return (
    <div className="orca-import-folder">
      <span className="jp-icon jp-icon-monitor" aria-hidden="true" />
      <span className="orca-import-folder-path">{looked ? `Looked in ${path}` : `${app} keeps these in ${path}`}</span>
      {onChoose && <button type="button" className="link" onClick={onChoose}>Choose another folder…</button>}
    </div>
  );
}

// Board 03: the same lines as board 17 on the first launch. One printer is
// named as the menu shows it; several are counted, since the list below names
// each. The page isn't told the open project's printer, so it says only that
// the printer stayed.
function Result({ app, result }: { app: string; result: OrcaImportLine[] }) {
  const printers = result.filter((line) => line.kind === 'printer' && line.imported);
  const where = 'in the printer menu at the top of the window. ';
  return <>
    <p className="orca-import-lead">
      {printers.length === 1 && `${printers[0].renamedTo || printers[0].name} is now ${where}`}
      {printers.length > 1 && `The ${printers.length} imported printers are now ${where}`}
      The open project still uses the same printer as before. {app}’s own files were not changed.
    </p>
    <OrcaImportResultLines app={app} lines={result} />
  </>;
}

// The folder's printers, the new filaments and settings, and what JusPrin
// can't import, each with its reason (boards 02, 04, 06, 07).
function Offer({ state, chosen, onChoose, disabled }: {
  state: OrcaImportPayload;
  chosen?: string[];
  onChoose?: (name: string, ticked: boolean) => void;
  disabled?: boolean;
}) {
  const printers = state.printers ?? [];
  const filaments = state.filaments ?? [];
  const processes = state.processes ?? [];
  const skipped = state.skipped ?? [];
  return <>
    {printers.length > 0 &&
      <OrcaPrinterList app={state.app} printers={printers} chosen={chosen} onChoose={onChoose} disabled={disabled} />}
    {filaments.length + processes.length > 0 && <OrcaNewList filaments={filaments} processes={processes} />}
    {skipped.length > 0 && <OrcaOutcomeList label="Can’t import" lines={skipped} />}
  </>;
}

// What the found screen copies when no printer is new.
function copied(filaments: number, processes: number) {
  const what = [filaments === 1 ? 'a filament' : filaments > 1 ? 'filaments' : '', processes > 0 ? 'print settings' : '']
    .filter(Boolean).join(' and ');
  return { what, them: filaments === 1 && processes === 0 ? 'it' : 'them' };
}

export function OrcaImport({ state, actions }: { state: OrcaImportPayload; actions: OrcaImportActions }) {
  // Importing holds the app for a while; the buttons say so meanwhile.
  // The host answers with the next screen once it is done.
  const [busy, setBusy] = useState(false);
  useEffect(() => setBusy(false), [state]);
  // Every printer JusPrin doesn't have yet starts ticked.
  const fresh = (state.printers ?? []).filter((printer) => !printer.already).map((printer) => printer.name);
  const [chosen, setChosen] = useState(fresh);
  // A new list, as after choosing another folder, starts over; the same one pushed again does not.
  const listed = fresh.join('\n');
  useEffect(() => setChosen(fresh), [listed]);
  const choose = (name: string, ticked: boolean) =>
    setChosen((now) => (ticked ? [...now, name] : now.filter((one) => one !== name)));
  const run = (action: () => void) => {
    setBusy(true);
    afterPaint(action);
  };
  const folder = state.folder ?? '';
  const newPrinters = state.newPrinters ?? 0;
  const { app } = state;
  // Only the rows with the warning sign; the printers above them are fine.
  // Each of those rows says itself why JusPrin can't import it.
  const skipped = (state.skipped ?? []).length;
  const warned = skipped === 1 ? 'the item with the warning sign' : `the ${skipped} items with the warning sign`;
  const skips = skipped > 0 ? ` and skips ${warned}` : '';
  const others = copied((state.filaments ?? []).length, (state.processes ?? []).length);

  let body;
  let buttons;
  switch (state.screen) {
    case 'imported':
      body = <Result app={app} result={state.result ?? []} />;
      buttons = <button type="button" className="primary" onClick={actions.close}>Done</button>;
      break;
    case 'nothing_new':
      body = <>
        <p className="orca-import-lead">
          {skipped > 0
            ? `There is nothing to import. JusPrin already has everything in this ${app} folder except ${warned}, which it can’t import.`
            : `Every printer in this ${app} folder is already in JusPrin, so there is nothing to import.`}
        </p>
        <Offer state={state} />
        <Folder app={app} path={folder} onChoose={actions.chooseFolder} />
      </>;
      buttons = <button type="button" className="primary" onClick={actions.close}>Close</button>;
      break;
    case 'no_folder':
    case 'no_printers':
      body = <>
        <p className="orca-import-lead">
          {state.screen === 'no_folder'
            ? `JusPrin didn’t find ${app}’s folder on this computer. `
            : `JusPrin found no ${app} printers in this folder. `}
          If {app} keeps its settings somewhere else, choose that folder.
        </p>
        <Folder app={app} path={folder} looked />
      </>;
      buttons = <>
        <button type="button" onClick={actions.close}>Cancel</button>
        <button type="button" className="primary" onClick={actions.chooseFolder}>Choose the folder…</button>
      </>;
      break;
    case 'found':
      body = <>
        <p className="orca-import-lead">
          {newPrinters > 0
            ? `JusPrin found ${plural(newPrinters, 'printer', 'printers')} in ${app} that it doesn’t have yet. When you import, JusPrin copies ${newPrinters === 1 ? 'it' : 'them'} with ${newPrinters === 1 ? 'its' : 'their'} filaments and print settings${skips}. `
            : `JusPrin found ${others.what} in ${app} that it doesn’t have yet. When you import, JusPrin copies ${others.them}${skips}. `}
          {app} keeps its own copy.
        </p>
        <Offer state={state} chosen={chosen} onChoose={choose} disabled={busy} />
        <Folder app={app} path={folder} onChoose={busy ? undefined : actions.chooseFolder} />
      </>;
      buttons = <>
        <button type="button" disabled={busy} onClick={actions.close}>Cancel</button>
        <button type="button" className="primary" disabled={busy || (newPrinters > 0 && chosen.length === 0)}
          onClick={() => run(() => actions.importPrinters(chosen))}>
          {busy ? 'Importing…' : chosen.length > 0 ? `Import ${plural(chosen.length, 'printer', 'printers')}` : 'Import'}
        </button>
      </>;
      break;
  }

  return (
    <div className="orca-import" data-testid="orca-import">
      <div className="orca-import-content">
        {body}
        {state.error && <p className="orca-import-error" role="alert">{state.error}</p>}
      </div>
      <div className="orca-import-buttons">{buttons}</div>
    </div>
  );
}
