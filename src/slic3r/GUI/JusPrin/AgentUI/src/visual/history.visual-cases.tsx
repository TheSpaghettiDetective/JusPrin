// The manufacturing history matrix (Figma 1619:2204) and the historical-chat
// recovery matrix (1619:2557). History cards are the production card from a
// record the host could send; the recovery states are the real App viewing an
// earlier chat.

import { fireEvent, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { ChangeInfo, PhysicalPrintInfo, StatePayload } from '../bridge/protocol';
import { ChangeRows } from '../components/ChangeRows';
import { ManufacturingHistoryCard, ManufacturingHistoryEntry } from '../components/ManufacturingHistoryCard';
import { mounted, mountedApp, VisualCase } from '../test/visual';
import { chatState, workspace } from './states';

const hash = 'a71f8c04'.repeat(8);
const statistics = { printTimeSeconds: 9360, filamentMm: 1842.5, materialGrams: 68, materialCost: 1.12, layerCount: 181 };

const print = (overrides: Partial<PhysicalPrintInfo> = {}): PhysicalPrintInfo => ({
  id: 'p-1', seq: 12, startedAt: '2026-10-01T13:09:00Z', endedAt: '2026-10-01T14:13:00Z', outcome: 'completed', failure: '',
  projectId: 'project-1', conversationId: 'conv-1', afterMessageId: 'm-2', plateIndex: 0, plateName: 'Plate 1',
  printer: 'Bambu X1C 0.4', material: 'Generic PLA', manufacturingInputHash: hash, outputHash: hash, gcodeHash: hash,
  statistics, ...overrides,
});

const failed = print({ outcome: 'failed', failure: 'Layer shift reported near layer 62.', stoppedPercent: 41 });

const record = (id: string, name: string, expects: string[], entry: ManufacturingHistoryEntry, open = false): VisualCase => ({
  node: '1619:2204', matrix: 'Manufacturing history', id: `history-${id}`, name, frame: 'card', expects,
  build: () => mounted(<ManufacturingHistoryCard entry={entry} onDiscussFailure={() => {}} />,
    (root) => { if (open) root.querySelector('details')!.open = true; }),
});

const printEntry = (info: PhysicalPrintInfo): ManufacturingHistoryEntry =>
  ({ kind: 'print', seq: info.seq, afterMessageId: info.afterMessageId, record: info });

// ---- The change log between the turns -----------------------------------------
// The matrix's own frame draws no change rows; the context screen (1424:396)
// does, so these states are compared with it.

let changeSeq = 0;
const change = (kind: ChangeInfo['kind'], actor: ChangeInfo['actor'], label: string, extra: Partial<ChangeInfo> = {}): ChangeInfo => {
  changeSeq += 1;
  return { seq: changeSeq, createdAt: '2026-10-01T15:02:00Z', kind, actor, label, conversationId: 'conv-1', afterId: 'm-2', ...extra };
};
const setting = (label: string, from: string, to: string) => change('setting', 'agent', label, { from, to, preset: 'Strong' });

const six = () => [setting('Brim', '', '5 mm'), setting('First-layer speed', '40', '18 mm/s'), setting('Plate temp', '60', '70 °C'),
  setting('Layer height', '0.20', '0.20 mm'), setting('Wall loops', '2', '5'), setting('Sparse infill density', '15%', '35%')];
// A step the Agent took, then the settings it changed with it: one summary.
const laidFlat = [change('step', 'agent', 'Laid flat'), ...six()];
const adhesion = six();

const rows = (id: string, name: string, expects: string[], changes: ChangeInfo[], expand?: string, revertable = false): VisualCase => ({
  node: '1424:396', matrix: 'Change rows', id: `changes-${id}`, name, frame: 'card', expects,
  build: () => mounted(<ChangeRows changes={changes} onRevert={() => {}}
    restorePoints={revertable ? [{ changeSeq: changes[changes.length - 1].seq, versionId: 'v-1' }] : []} />,
    (root) => { if (expand) fireEvent.click([...root.querySelectorAll('button')].find((button) => button.textContent!.includes(expand))!); }),
});

// ---- An earlier chat ----------------------------------------------------------

const earlier = (status: 'changed' | 'unchanged' | 'unavailable', overrides: Partial<StatePayload> = {}): StatePayload => chatState({
  conversations: [
    { id: 'conv-a', title: 'Quick print', createdAt: '2026-10-03T12:00:00Z' },
    { id: 'conv-b', title: 'Strong print', createdAt: '2026-10-05T10:42:00Z' },
  ],
  activeConversationId: 'conv-b', viewedConversationId: 'conv-a', docRevision: 23,
  chatResume: status === 'unavailable' ? { status } : { status, savedAt: '2026-10-03T12:00:00Z', versionId: 'v-a', summary: workspace },
  ...overrides,
});

const recovery = (id: string, name: string, expects: string[], state: StatePayload, after?: () => Promise<void>): VisualCase => ({
  node: '1619:2557', matrix: 'Historical chat', id: `recovery-${id}`, name, frame: 'pane', expects,
  build: () => mountedApp(state, { after }),
});

export const historyCases: VisualCase[] = [
  record('print-completed', 'Physical print · Completed', ['Physical print', 'p-1', 'completed'], printEntry(print())),
  record('print-warning', 'Physical print · Warning', ['cancelled', 'First layer required intervention'],
    printEntry(print({ outcome: 'cancelled', failure: 'First layer required intervention' }))),
  record('print-failed', 'Failed print · Collapsed', ['Print failed', 'Bambu X1C 0.4 · stopped at 41%, 1h 04m in',
    'Layer shift reported near layer 62.', 'Discuss this failure'], printEntry(failed)),
  record('print-failed-open', 'Failed print · Expanded', ['Printed G-code SHA-256', 'Started', 'Ended'], printEntry(failed), true),

  rows('summary', 'Agent change summary', ['agent-change-summary', 'Laid flat', 'settings', 'aria-expanded="false"'],
    laidFlat),
  rows('summary-open', 'Agent change summary · expanded', ['aria-expanded="true"', 'First-layer speed', '18 mm/s'],
    laidFlat, 'Laid flat'),
  rows('settings', 'Agent setting changes · 4 of 6', ['4 of 6 settings', 'See the other 2'], adhesion),
  rows('settings-all', 'Agent setting changes · show fewer', ['Show fewer', 'Sparse infill density'], adhesion, 'See the other'),
  rows('single', 'Single agent-authored change', ['change-row'], [setting('Brim', '', '5 mm')]),
  rows('hand', 'Hand edits · undo, redo, preset, restore', ['Revert to here'], [
    change('step', 'person', 'Rotate', { location: 'on the plate' }),
    change('undo', 'person', 'Rotate'), change('redo', 'person', 'Rotate'),
    change('preset', 'person', '0.20mm Strong @BBL X1C'), change('restore', 'person', ''),
    change('setting', 'person', 'Sparse infill density', { from: '35%', to: '45%', preset: 'Strong' }),
  ], undefined, true),

  recovery('changed', 'Reason=Changed · Availability=Ready',
    ['There have been project updates since this chat. Restore its saved project version to continue.', 'Restore and resume',
      'Return to active chat', 'Ask current chat about this'], earlier('changed')),
  recovery('unchanged', 'Reason=Unchanged', ['This chat is inactive. Resume from its saved setup to continue.', 'Resume saved setup'],
    earlier('unchanged')),
  recovery('unavailable', 'Reason=Unavailable', ['This chat has no recoverable project checkpoint. Its saved state is missing or corrupt.'],
    earlier('unavailable')),
  recovery('busy', 'Reason=Changed · Availability=Busy', ['class="primary" disabled'], earlier('changed', { conversationBusy: true })),
  recovery('confirm', 'Restore-and-resume confirmation', ['Restore this chat\'s project setup?', 'role="dialog"'],
    earlier('changed'), async () => { await userEvent.click(screen.getByRole('button', { name: 'Restore and resume' })); }),
  recovery('blocked', 'Project restoration blocked',
    ['Project restoration needs recovery. This chat cannot run project actions.', 'placeholder="Project restoration needs recovery."'],
    chatState({ projectChatBlocked: true })),
];
