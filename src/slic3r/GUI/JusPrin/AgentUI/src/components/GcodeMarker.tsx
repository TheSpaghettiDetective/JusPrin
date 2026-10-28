import { GcodeMarkerInfo } from '../bridge/protocol';
import { chatTimestamp } from './ChatNavigation';

// The leading eight hex digits in the two groups the history reads them as.
export function shortHash(hash: string): string {
  return `${hash.slice(0, 4)} ${hash.slice(4, 8)}`;
}

// What a marker does: its "history" link opens the Print history at its
// G-code, and the history's Open in chat comes back to it, marked.
export interface MarkerActions {
  onHistory: (hash: string) => void;
  revealedId?: string | null;
}

// One line between turns where a G-code left the project: the only thing the
// chat draws about it. What happened to it after that is the print history's.
export function GcodeMarker({ marker, actions }: { marker: GcodeMarkerInfo; actions?: MarkerActions }) {
  const time = chatTimestamp(marker.at);
  const subject = marker.kind === 'export'
    ? `${marker.fileName} · exported`
    : `${marker.plateName || `Plate ${marker.plateIndex + 1}`} · ${shortHash(marker.hash)} → ${marker.printerName}`;
  const revealed = actions?.revealedId === marker.id;
  return (
    <div className={revealed ? 'gcode-marker revealed' : 'gcode-marker'} role="note" data-item-id={marker.id}>
      <span className="gcode-marker-rule" aria-hidden="true" />
      <span className="gcode-marker-text">
        ▱ {subject}{time && ` · ${time}`}
        {actions && (
          <>
            {' · '}
            <button type="button" className="gcode-marker-link" onClick={() => actions.onHistory(marker.hash)}>history</button>
          </>
        )}
      </span>
      <span className="gcode-marker-rule" aria-hidden="true" />
    </div>
  );
}
