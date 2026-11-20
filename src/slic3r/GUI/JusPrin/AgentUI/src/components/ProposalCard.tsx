// The card a settings preview that saves a preset is approved from (Chat/Tool
// Activity, Status=Proposed): the app's title and rows for the change, Save
// preset, which has the app save exactly what was previewed, and the
// other choice, which the person sends as their reply. Nothing is written
// until Save preset. A message of the person's answers it another way, and
// the card then says it was not saved. Once saved it goes: the receipt note
// that follows says so.

import { isStale, ToolActivityInfo } from '../bridge/protocol';
import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';

interface Props {
  preview: ToolActivityInfo;
  // The apply Save preset started, once pressed.
  save?: ToolActivityInfo;
  // A later message of the person's answered the card, or the app no
  // longer holds the preview it would save.
  closed: boolean;
  disabled: boolean;
  onSave: (actionId: string) => void;
  onSend: (text: string) => void;
}

export function ProposalCard({ preview, save, closed, disabled, onSave, onSend }: Props) {
  const proposal = preview.proposal!;
  if (save?.state === 'succeeded') return null;
  const saving = save?.state === 'running';
  // The save's error is the AI model's to read; the card says only what
  // happened, as an action's card does.
  const failure = save?.state === 'failed'
    ? isStale(save) ? t`The project changed before this could run. Ask AI again.` : t`The action failed.`
    : undefined;
  return (
    <div className="proposal-card" role="group" aria-label={proposal.title}>
      <div className="proposal-title">{proposal.title}</div>
      {proposal.rows.map((row) => (
        <div key={row.text} className="proposal-row">
          <span className={`jp-icon jp-icon-${row.icon}`} aria-hidden="true" />
          <span>{row.text}</span>
        </div>
      ))}
      {failure && (
        <div className="proposal-row proposal-failure" role="alert">
          <span className="jp-icon jp-icon-circle-alert" aria-hidden="true" />
          <span>{failure}</span>
        </div>
      )}
      {closed ? (
        <div className="proposal-row">
          <span className="jp-icon jp-icon-circle-minus" aria-hidden="true" />
          <span><Trans>Not saved</Trans></span>
        </div>
      ) : (
        <div className="proposal-actions">
          <button type="button" className="primary" disabled={disabled || saving} onClick={() => onSave(preview.actionId)}>
            {saving ? t`Saving…` : proposal.save}
          </button>
          {proposal.other && (
            <button type="button" className="secondary" disabled={disabled || saving} onClick={() => onSend(proposal.other!)}>
              {proposal.other}
            </button>
          )}
        </div>
      )}
    </div>
  );
}
