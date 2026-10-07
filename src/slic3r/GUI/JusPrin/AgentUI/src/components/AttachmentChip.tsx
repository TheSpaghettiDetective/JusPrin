// Shared attachment row for the project and focused printer/filament chats.
// Staged and failed items are removable; sent items are read-only. A decoded
// photo has a thumbnail, while other files show their kind as a small badge.

import { AttachmentInfo } from '../bridge/protocol';

interface Props {
  attachment: AttachmentInfo;
  onRemove?: (id: string) => void;
}

const kindLabel: Record<string, string> = {
  text: 'Text',
  image: 'Image',
  svg: 'SVG',
  pdf: 'PDF',
  gcode: 'G-code',
  model: '3D model',
  unsupported: 'Unsupported',
};

function sizeLabel(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export function AttachmentChip({ attachment, onRemove }: Props) {
  const errored = attachment.state === 'error';
  const label = kindLabel[attachment.kind] ?? 'File';
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
          <span className="attachment-sub">{format} · {sizeLabel(attachment.sizeBytes)}</span>
        </span>
        {onRemove && (
          <button
            type="button"
            className="attachment-remove"
            aria-label={`Remove ${title}`}
            onClick={() => onRemove(attachment.id)}
          >
            Remove
          </button>
        )}
      </div>
      {errored && <span className="attachment-error">{attachment.error?.message ?? 'Could not be attached'}</span>}
    </div>
  );
}
