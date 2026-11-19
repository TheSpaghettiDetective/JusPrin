// The new-version card at the top of Home's right column (board update-07).
// The host shows it while its update check has a newer release to offer;
// JusPrin downloads nothing itself: the person gets the file from the
// release page on GitHub.

import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import type { UpdateNotice } from '../bridge/protocol';
import { ExternalGlyph } from './Glyphs';

export interface UpdateActions {
  onNotes: () => void;
  onOpenPage: () => void;
  onRemindLater: () => void;
  onSkip: (version: string) => void;
}

export function UpdateCard({ update, actions }: { update: UpdateNotice; actions: UpdateActions }) {
  const { version: latest, running } = update;
  return (
    <section className="agent-card update-card" aria-label={t`New version`}>
      <h2 className="update-card-title"><Trans>JusPrin {latest} is available</Trans></h2>
      <p className="update-card-body">
        <Trans>
          You have version {running}. Your projects, printers and settings stay when you install the new one.
          Installing doesn’t affect a print your printer is already running.
        </Trans>
      </p>
      <button type="button" className="onboarding-link" onClick={actions.onNotes}>
        <Trans>See what’s new</Trans>
      </button>
      <p className="update-card-note">
        <Trans>The download page is on GitHub. Pick the file for your computer there: Mac, Windows or Linux.</Trans>
      </p>
      <button type="button" className="button-primary button-primary-small update-card-download" onClick={actions.onOpenPage}>
        <Trans>Open download page</Trans>
        <ExternalGlyph />
      </button>
      <button type="button" className="button-secondary button-secondary-small" onClick={actions.onRemindLater}>
        <Trans>Remind me tomorrow</Trans>
      </button>
      <button type="button" className="onboarding-link" onClick={() => actions.onSkip(latest)}>
        <Trans>Skip this version</Trans>
      </button>
    </section>
  );
}
