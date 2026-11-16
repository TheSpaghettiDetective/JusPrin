// Shared attachment row for the project and focused printer/filament chats.
// Staged and failed items are removable; sent items are read-only. A decoded
// photo has a thumbnail, while other files show their kind as a small badge.

import { AttachmentInfo } from '../bridge/protocol';
import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import { decimalText } from '../i18n';

interface Props {
  attachment: AttachmentInfo;
  onRemove?: (id: string) => void;
  // The chat's model cannot see pictures: the row says this one will not be sent.
  notSent?: boolean;
}

const kindLabel: Record<string, string> = {
  text: t({ message: 'Text', comment: 'The kind of an attached file: a text file' }),
  image: t({ message: 'Image', comment: 'The kind of an attached file' }),
  svg: 'SVG',
  pdf: 'PDF',
  gcode: 'G-code',
  model: t`3D model`,
  unsupported: t({ message: 'Unsupported', comment: 'The kind of an attached file the app cannot read' }),
};

function sizeLabel(bytes: number): string {
  // TRN: a file size in bytes.
  if (bytes < 1024) return t`${bytes} B`;
  const kilobytes = Math.round(bytes / 1024);
  // TRN: a file size in kilobytes.
  if (bytes < 1024 * 1024) return t`${kilobytes} KB`;
  const megabytes = decimalText(bytes / (1024 * 1024), 1, 1);
  // TRN: a file size in megabytes.
  return t`${megabytes} MB`;
}

export function AttachmentChip({ attachment, onRemove, notSent }: Props) {
  const errored = attachment.state === 'error';
  const label = kindLabel[attachment.kind] ?? t({ message: 'File', comment: 'The kind of an attached file of no known kind' });
  const title = attachment.name || attachment.summary || label;
  const extension = attachment.name.split('.').pop()?.toUpperCase();
  const kind = attachment.previewDataUrl ? label : extension && extension.length <= 5 ? extension : label;
  // The format the size is of: the picture's own, or the kind on the badge.
  const format = attachment.previewDataUrl ? attachment.mime.split('/')[1]?.toUpperCase() || label : kind;

  return (
    <div className={`attachment-chip${errored ? ' errored' : ''}`} title={title}>
      <div className="attachment-content">
        {attachment.previewDataUrl ? (
          <img className="attachment-thumb" src={attachment.previewDataUrl} alt="" />
        ) : (
          <span className="attachment-kind" aria-hidden="true">
            {kind}
          </span>
        )}
        <span className="attachment-meta">
          <span className="attachment-name">{title}</span>
          {notSent
            ? <span className="attachment-not-sent">
                <span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />
                <span><Trans>Standard can’t see pictures, so this one will not be sent.</Trans></span>
              </span>
            : <span className="attachment-sub">{format} · {sizeLabel(attachment.sizeBytes)}</span>}
        </span>
        {onRemove && (
          <button
            type="button"
            className="attachment-remove"
            aria-label={t`Remove ${title}`}
            onClick={() => onRemove(attachment.id)}
          >
            <Trans>Remove</Trans>
          </button>
        )}
      </div>
      {errored && <span className="attachment-error">{attachment.error?.message ?? t`Could not be attached`}</span>}
    </div>
  );
}
