// Printer › Import printers from OrcaSlicer… and Import printers from JusPrin 1…: the
// printers the app's folder has, the ones JusPrin already has marked and
// skipped, the new filaments and settings, what it can't import and why, and
// once imported, what became of each (canvas "JusPrin v2", boards
// orca-import-02 found, 03 imported, 04 nothing new, 05 no folder, 06 nothing
// new but something it can't import, 07 only filaments and settings; JusPrin 1
// works the same way). The window's title bar carries the title; the host
// finds the folder and does the importing.

import { plural, t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
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

// Says whose folder it is, in the Printers step's words, so it isn't taken
// for JusPrin's own: JusPrin 1's folder is called JusPrin.
function Folder({ app, path, looked, onChoose }: { app: string; path: string; looked?: boolean; onChoose?: () => void }) {
  return (
    <div className="orca-import-folder">
      <span className="jp-icon jp-icon-monitor" aria-hidden="true" />
      <span className="orca-import-folder-path">{looked ? t`Looked in ${path}` : t`${app} keeps these in ${path}`}</span>
      {onChoose && <button type="button" className="link" onClick={onChoose}>{t`Choose another folder…`}</button>}
    </div>
  );
}

// Board 03: the same lines as board 17 on the first launch. One printer is
// named as the menu shows it; several are counted, since the list below names
// each. The page isn't told the open project's printer, so it says only that
// the printer stayed.
function Result({ app, result }: { app: string; result: OrcaImportLine[] }) {
  const printers = result.filter((line) => line.kind === 'printer' && line.imported);
  const name = printers.length === 1 ? printers[0].renamedTo || printers[0].name : '';
  const where = printers.length === 1 ? t`${name} is now in the printer menu at the top of the window.`
    : printers.length > 1 ? plural(printers.length, {
      one: 'The # imported printers are now in the printer menu at the top of the window.',
      other: 'The # imported printers are now in the printer menu at the top of the window.',
    }) : '';
  return <>
    <p className="orca-import-lead">
      {where && `${where} `}
      <Trans>The open project still uses the same printer as before. {app}’s own files were not changed.</Trans>
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
    {skipped.length > 0 && <OrcaOutcomeList label={t`Can’t import`} lines={skipped} />}
  </>;
}

// The found screen's lead: what JusPrin found that it doesn't have yet, what
// an import copies and skips, and that the other app keeps its own copy.
// Each sentence is one message for every count it can have.
function foundLead(app: string, newPrinters: number, filaments: number, processes: number, skipped: number): string {
  let found: string;
  if (newPrinters > 0)
    found = plural(newPrinters, {
      one: `JusPrin found # printer in ${app} that it doesn’t have yet.`,
      other: `JusPrin found # printers in ${app} that it doesn’t have yet.`,
    });
  else if (filaments === 1)
    found = processes > 0 ? t`JusPrin found a filament and print settings in ${app} that it doesn’t have yet.`
      : t`JusPrin found a filament in ${app} that it doesn’t have yet.`;
  else if (filaments > 1)
    found = processes > 0 ? t`JusPrin found filaments and print settings in ${app} that it doesn’t have yet.`
      : t`JusPrin found filaments in ${app} that it doesn’t have yet.`;
  else found = t`JusPrin found print settings in ${app} that it doesn’t have yet.`;

  // "It" is one printer, or one filament and nothing else; "them" is the rest.
  const one = newPrinters > 0 ? newPrinters === 1 : filaments === 1 && processes === 0;
  let copies: string;
  if (newPrinters > 0 && one)
    copies = skipped === 0 ? t`When you import, JusPrin copies it with its filaments and print settings.` : plural(skipped, {
      1: 'When you import, JusPrin copies it with its filaments and print settings and skips the item with the warning sign.',
      other: 'When you import, JusPrin copies it with its filaments and print settings and skips the # items with the warning sign.',
    });
  else if (newPrinters > 0)
    copies = skipped === 0 ? t`When you import, JusPrin copies them with their filaments and print settings.` : plural(skipped, {
      1: 'When you import, JusPrin copies them with their filaments and print settings and skips the item with the warning sign.',
      other: 'When you import, JusPrin copies them with their filaments and print settings and skips the # items with the warning sign.',
    });
  else if (one)
    copies = skipped === 0 ? t`When you import, JusPrin copies it.` : plural(skipped, {
      1: 'When you import, JusPrin copies it and skips the item with the warning sign.',
      other: 'When you import, JusPrin copies it and skips the # items with the warning sign.',
    });
  else
    copies = skipped === 0 ? t`When you import, JusPrin copies them.` : plural(skipped, {
      1: 'When you import, JusPrin copies them and skips the item with the warning sign.',
      other: 'When you import, JusPrin copies them and skips the # items with the warning sign.',
    });
  return `${found} ${copies} ${t`${app} keeps its own copy.`}`;
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

  let body;
  let buttons;
  switch (state.screen) {
    case 'imported':
      body = <Result app={app} result={state.result ?? []} />;
      buttons = <button type="button" className="primary" onClick={actions.close}>{t`Done`}</button>;
      break;
    case 'nothing_new':
      body = <>
        <p className="orca-import-lead">
          {skipped > 0
            ? plural(skipped, {
              1: `There is nothing to import. JusPrin already has everything in this ${app} folder except the item with the warning sign, which it can’t import.`,
              other: `There is nothing to import. JusPrin already has everything in this ${app} folder except the # items with the warning sign, which it can’t import.`,
            })
            : t`Every printer in this ${app} folder is already in JusPrin, so there is nothing to import.`}
        </p>
        <Offer state={state} />
        <Folder app={app} path={folder} onChoose={actions.chooseFolder} />
      </>;
      buttons = <button type="button" className="primary" onClick={actions.close}>{t`Close`}</button>;
      break;
    case 'no_folder':
    case 'no_printers':
      body = <>
        <p className="orca-import-lead">
          {state.screen === 'no_folder'
            ? `${t`JusPrin didn’t find ${app}’s folder on this computer.`} `
            : `${t`JusPrin found no ${app} printers in this folder.`} `}
          <Trans>If {app} keeps its settings somewhere else, choose that folder.</Trans>
        </p>
        <Folder app={app} path={folder} looked />
      </>;
      buttons = <>
        <button type="button" onClick={actions.close}>{t`Cancel`}</button>
        <button type="button" className="primary" onClick={actions.chooseFolder}>{t`Choose the folder…`}</button>
      </>;
      break;
    case 'found':
      body = <>
        <p className="orca-import-lead">
          {foundLead(app, newPrinters, (state.filaments ?? []).length, (state.processes ?? []).length, skipped)}
        </p>
        <Offer state={state} chosen={chosen} onChoose={choose} disabled={busy} />
        <Folder app={app} path={folder} onChoose={busy ? undefined : actions.chooseFolder} />
      </>;
      buttons = <>
        <button type="button" disabled={busy} onClick={actions.close}>{t`Cancel`}</button>
        <button type="button" className="primary" disabled={busy || (newPrinters > 0 && chosen.length === 0)}
          onClick={() => run(() => actions.importPrinters(chosen))}>
          {busy ? t`Importing…` : chosen.length > 0
            ? plural(chosen.length, { one: 'Import # printer', other: 'Import # printers' }) : t`Import`}
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
