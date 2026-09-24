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
  return [...sharedStaticVariableNames(), '--thread-row-line-gap', '--printer-setup-width',
    '--printer-dialog-width', '--printer-dialog-radius', '--printer-dialog-scrim',
    '--reply-chip-height', '--reply-chip-padding-x'];
}

// In addition to the shared radii, fonts, and button paddings:
//   --thread-row-line-gap  the space between a thread row's title and its
//                          metadata line, an internal size of the row
//   --printer-dialog-*     the printer panel's own dialog: the same
//                          component.printerDialog token Home's rename and
//                          remove dialogs use, so every printer-feature
//                          dialog has one width, radius, and scrim (its
//                          strength, a percentage for color-mix())
//   --reply-chip-height,   a reply the assistant offers as a chip under its
//   --reply-chip-padding-x message: its height and its text's inset.
export function applyStaticTokens(): void {
  applySharedStaticTokens();
  const { component } = tokens as unknown as {
    component: { threadRow: { lineGap: number }; printerSetup: { contentWidth: number };
      printerDialog: { width: number; radius: number; scrimAlpha: number };
      replyChip: { height: number; paddingX: number } };
  };
  document.documentElement.style.setProperty('--thread-row-line-gap', `${component.threadRow.lineGap}px`);
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
}
