// One attachment shown as a compact chip: an image thumbnail when the host
// decoded one, otherwise a kind label. Used both for staged attachments in the
// composer (removable) and for sent attachments in the transcript (read-only).

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

export function AttachmentChip({ attachment, onRemove }: Props) {
  const errored = attachment.state === 'error';
  const label = kindLabel[attachment.kind] ?? 'File';
  const title = attachment.name || attachment.summary || label;
  return (
    <div className={`attachment-chip${errored ? ' errored' : ''}`} title={title}>
      {attachment.previewDataUrl ? (
        <img className="attachment-thumb" src={attachment.previewDataUrl} alt={title} />
      ) : (
        <span className="attachment-kind" aria-hidden="true">
          {label}
        </span>
      )}
      <span className="attachment-meta">
        <span className="attachment-name">{title}</span>
        {errored ? (
          <span className="attachment-error">{attachment.error?.message ?? 'Could not be attached'}</span>
        ) : (
          <span className="attachment-sub">{label}</span>
        )}
      </span>
      {onRemove && (
        <button
          type="button"
          className="attachment-remove"
          aria-label={`Remove ${title}`}
          onClick={() => onRemove(attachment.id)}
        >
          ×
        </button>
      )}
    </div>
  );
}
