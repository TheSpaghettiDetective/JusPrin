// Applies the repository's design tokens as CSS custom properties.
// resources/jusprin/ui/design-tokens.json is the implementation source of
// truth; the stylesheet must use var(--...) rather than literal colors,
// radii, or type metrics. styles.test.ts enforces that on the stylesheet.
//
// The universal part lives in WebShared/tokens.ts, shared with the Home page.
// Only the Agent page's own component variables are written here.

import tokens from '@resources/jusprin/ui/design-tokens.json';
import { applySharedStaticTokens, sharedStaticVariableNames } from '@shared/tokens';

export { applyAppearance } from '@shared/tokens';

// Every --font-*, --button-*, and Agent-page variable applyStaticTokens
// writes, so a test can check that the stylesheet only asks for variables
// that exist.
export function staticVariableNames(): string[] {
  return [...sharedStaticVariableNames(), '--thread-row-line-gap',
    '--timeline-summary-padding', '--timeline-summary-padding-x', '--timeline-summary-icon-gap', '--printer-setup-width',
    '--printer-dialog-width', '--printer-dialog-radius', '--printer-dialog-scrim',
    '--reply-chip-height', '--reply-chip-padding-x', '--timeline-revert-width', '--status-dot-size',
    '--chat-attachment-max-width', '--chat-attachment-thumbnail-size', '--chat-attachment-kind-size',
    '--field-height', '--field-padding-x', '--setup-card-icon-size', '--swatch-size'];
}

// In addition to the shared radii, fonts, and button paddings:
//   --thread-row-line-gap  the space between a thread row's title and its
//                          metadata line, an internal size of the row
//   --timeline-summary-*     the compact edit button's insets and icon gap
//   --printer-dialog-*     the printer panel's own dialog: the same
//                          component.printerDialog token Home's rename and
//                          remove dialogs use, so every printer-feature
//                          dialog has one width, radius, and scrim (its
//                          strength, a percentage for color-mix())
//   --setup-card-icon-size the setup card's fact glyphs, which sit inside a
//                          row of Label or Metadata text
//   --swatch-size          a colour drawn beside a note, the design
//                          system's Swatch
//   --reply-chip-height,   a reply the assistant offers as a chip under its
//   --reply-chip-padding-x message: its height and its text's inset.
export function applyStaticTokens(): void {
  applySharedStaticTokens();
  const { component } = tokens as unknown as {
    component: { threadRow: { lineGap: number };
      timelineSummary: { paddingX: number; paddingY: number; iconGap: number };
      printerSetup: { contentWidth: number };
      printerDialog: { width: number; radius: number; scrimAlpha: number };
      timelineRevert: { width: number };
      statusDot: { size: number };
      replyChip: { height: number; paddingX: number };
      chatAttachment: { maxWidth: number; thumbnailSize: number; kindSize: number };
      field: { height: number; paddingX: number };
      printerCard: { pictureSize: number };
      setupCard: { iconSize: number };
      swatch: { size: number } };
  };
  document.documentElement.style.setProperty('--thread-row-line-gap', `${component.threadRow.lineGap}px`);
  document.documentElement.style.setProperty('--timeline-summary-padding',
    `${component.timelineSummary.paddingY}px ${component.timelineSummary.paddingX}px`);
  document.documentElement.style.setProperty('--timeline-summary-padding-x', `${component.timelineSummary.paddingX}px`);
  document.documentElement.style.setProperty('--timeline-summary-icon-gap', `${component.timelineSummary.iconGap}px`);
  document.documentElement.style.setProperty('--printer-setup-width', `${component.printerSetup.contentWidth}px`);
  document.documentElement.style.setProperty('--printer-dialog-width', `${component.printerDialog.width}px`);
  document.documentElement.style.setProperty('--printer-dialog-radius', `${component.printerDialog.radius}px`);
  // The native scrim takes an alpha out of 255; CSS mixes by a percentage.
  document.documentElement.style.setProperty(
    '--printer-dialog-scrim',
    `${Math.round((component.printerDialog.scrimAlpha / 255) * 100)}%`,
  );
  document.documentElement.style.setProperty('--reply-chip-height', `${component.replyChip.height}px`);
  document.documentElement.style.setProperty('--reply-chip-padding-x', `${component.replyChip.paddingX}px`);
  document.documentElement.style.setProperty('--timeline-revert-width', `${component.timelineRevert.width}px`);
  document.documentElement.style.setProperty('--status-dot-size', `${component.statusDot.size}px`);
  document.documentElement.style.setProperty('--setup-card-icon-size', `${component.setupCard.iconSize}px`);
  document.documentElement.style.setProperty('--swatch-size', `${component.swatch.size}px`);
  document.documentElement.style.setProperty('--chat-attachment-max-width', `${component.chatAttachment.maxWidth}px`);
  document.documentElement.style.setProperty('--chat-attachment-thumbnail-size', `${component.chatAttachment.thumbnailSize}px`);
  document.documentElement.style.setProperty('--chat-attachment-kind-size', `${component.chatAttachment.kindSize}px`);
  document.documentElement.style.setProperty('--field-height', `${component.field.height}px`);
  document.documentElement.style.setProperty('--field-padding-x', `${component.field.paddingX}px`);
  document.documentElement.style.setProperty('--printer-card-picture-size', `${component.printerCard.pictureSize}px`);
}
