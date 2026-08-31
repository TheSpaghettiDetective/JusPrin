// One entry of the shared manufacturing-revision timeline, rendered inline
// where it happened in the conversation. Revert here is destructive and
// always asks at action time: the button expands into an explicit inline
// confirmation before anything is sent to the host.

import { useState } from 'react';
import { RevisionInfo } from '../bridge/protocol';

interface Props {
  revision: RevisionInfo;
  onRevert: (revisionId: string) => void;
}

export function RevisionMarker({ revision, onRevert }: Props) {
  const [confirming, setConfirming] = useState(false);

  return (
    <div className={revision.current ? 'revision-marker current' : 'revision-marker'} data-testid={`revision-${revision.id}`}>
      <span className="revision-label">
        {revision.cause === 'initial' ? 'Project start' : `Project changed (${revision.cause})`}
        {revision.current && ' — current'}
      </span>
      {!revision.current && revision.revertible && !confirming && (
        <button onClick={() => setConfirming(true)}>Revert here</button>
      )}
      {confirming && (
        <span className="revision-confirm" role="alertdialog" aria-label="Confirm revert">
          Reverting permanently removes everything after this point — later messages, approvals, and project changes in
          every conversation. This cannot be undone.
          <button
            className="primary"
            onClick={() => {
              setConfirming(false);
              onRevert(revision.id);
            }}
          >
            Revert permanently
          </button>
          <button onClick={() => setConfirming(false)}>Keep everything</button>
        </span>
      )}
      {!revision.current && !revision.revertible && <span className="revision-label">No checkpoint available</span>}
    </div>
  );
}
