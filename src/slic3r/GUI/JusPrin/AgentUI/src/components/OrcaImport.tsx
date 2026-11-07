// File › Import › Import from OrcaSlicer…: the printers an OrcaSlicer folder
// has, the ones JusPrin already has marked and skipped, and once imported,
// what became of each (canvas "JusPrin v2", boards orca-import-02 found,
// 03 imported, 04 nothing new, 05 no folder). The window's title bar carries
// the title; the host finds the folder and does the importing.

import { useEffect, useState } from 'react';
import type { OrcaImportLine, OrcaImportPayload } from '../bridge/protocol';
import { afterPaint } from '@shared/afterPaint';
import { OrcaImportResultLines, OrcaPrinterList } from './OrcaImportLists';

export interface OrcaImportActions {
  // The printers left ticked, by name.
  importPrinters: (printers: string[]) => void;
  chooseFolder: () => void;
  close: () => void;
}

const plural = (count: number, one: string, many: string) => `${count} ${count === 1 ? one : many}`;

function Folder({ path, looked, onChoose }: { path: string; looked?: boolean; onChoose?: () => void }) {
  return (
    <div className="orca-import-folder">
      <span className="jp-icon jp-icon-monitor" aria-hidden="true" />
      <span className="orca-import-folder-path">{looked ? `Looked in ${path}` : path}</span>
      {onChoose && <button type="button" className="link" onClick={onChoose}>Choose another folder…</button>}
    </div>
  );
}

// Board 03: the same lines as board 17 on the first launch.
function Result({ result }: { result: OrcaImportLine[] }) {
  const printers = result.filter((line) => line.kind === 'printer' && line.imported).length;
  return <>
    <p className="orca-import-lead">
      {printers > 0 && (printers === 1 ? 'It is in the printer menu. ' : 'They are in the printer menu. ')}
      The printer this project uses didn’t change.
    </p>
    <OrcaImportResultLines lines={result} />
  </>;
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

  let body;
  let buttons;
  switch (state.screen) {
    case 'imported':
      body = <Result result={state.result ?? []} />;
      buttons = <button type="button" className="primary" onClick={actions.close}>Done</button>;
      break;
    case 'nothing_new':
      body = <>
        <p className="orca-import-lead">Every printer in this OrcaSlicer folder is already in JusPrin, so there is nothing to import.</p>
        <OrcaPrinterList printers={state.printers ?? []} />
        <Folder path={folder} onChoose={actions.chooseFolder} />
      </>;
      buttons = <button type="button" className="primary" onClick={actions.close}>Close</button>;
      break;
    case 'no_folder':
    case 'no_printers':
      body = <>
        <p className="orca-import-lead">
          {state.screen === 'no_folder'
            ? 'JusPrin didn’t find OrcaSlicer’s folder on this computer. '
            : 'JusPrin found no OrcaSlicer printers in this folder. '}
          If OrcaSlicer keeps its settings somewhere else, choose that folder.
        </p>
        <Folder path={folder} looked />
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
            ? `JusPrin found ${plural(newPrinters, 'printer', 'printers')} in OrcaSlicer that it doesn’t have yet. JusPrin copies ${newPrinters === 1 ? 'it' : 'them'} with ${newPrinters === 1 ? 'its' : 'their'} filaments, settings and printer connections. `
            : 'JusPrin found filaments or settings in OrcaSlicer that it doesn’t have yet. JusPrin copies them. '}
          OrcaSlicer keeps its own copy.
        </p>
        <OrcaPrinterList printers={state.printers ?? []} chosen={chosen} onChoose={choose} disabled={busy} />
        <Folder path={folder} onChoose={busy ? undefined : actions.chooseFolder} />
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
