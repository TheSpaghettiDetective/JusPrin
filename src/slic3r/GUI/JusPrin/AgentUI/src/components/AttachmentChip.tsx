// One attachment shown as a compact chip: an image thumbnail when the host
// decoded one, otherwise a kind label. Used both for staged attachments in the
// composer (removable) and for sent attachments in the transcript (read-only).
//
// `compact`: the printer panel's photo is the point of the tap, so it takes
// the thumbnail's place, but the wireframe (3.1 item 4, 3.3 state E) still
// keeps the file name beside it -- no kind label, since a thumbnail already
// says "this is a photo". Falls back to the ordinary chip for a compact
// attachment with no preview (nothing to show as a thumbnail) or an error
// (the message needs to be read).

import { AttachmentInfo } from '../bridge/protocol';

interface Props {
  attachment: AttachmentInfo;
  onRemove?: (id: string) => void;
  compact?: boolean;
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

export function AttachmentChip({ attachment, onRemove, compact }: Props) {
  const errored = attachment.state === 'error';
  const label = kindLabel[attachment.kind] ?? 'File';
  const title = attachment.name || attachment.summary || label;

  if (compact && attachment.previewDataUrl && !errored)
    return (
      <div className="attachment-chip attachment-chip-compact" title={title}>
        <img className="attachment-thumb attachment-thumb-compact" src={attachment.previewDataUrl} alt={title} />
        <span className="attachment-name attachment-name-compact">{title}</span>
        {onRemove && (
          <button
            type="button"
            className="attachment-remove"
            aria-label={`Remove ${title}`}
            onClick={() => onRemove(attachment.id)}
          >
            <span className="remove-glyph" aria-hidden="true" />
          </button>
        )}
      </div>
    );

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
          <span className="remove-glyph" aria-hidden="true" />
        </button>
      )}
    </div>
  );
}
