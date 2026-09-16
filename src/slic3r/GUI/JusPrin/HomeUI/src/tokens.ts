// Applies the repository's design tokens as CSS custom properties. The
// universal part lives in WebShared/tokens.ts; Home adds its own card
// geometry, guarded in tests/brand/test_brand_tokens.cpp:
//   --project-card-<name>   min/max width, footer height, radius
//   --project-card-thumbnail-aspect  the unitless `4 / 3` for aspect-ratio
//   --printer-card-<name>   column width, progress height, radius
//   --status-dot-size, --swatch-size, --swatch-radius
// and the printer card's menu, from the shared menu geometry:
//   --button-icon-<name>    the kebab button: width, height, icon size, radius
//   --menu-row-<name>       height, radius, side inset
//   --popover-<name>        vertical padding, row gap, radius
// and its rename and remove dialogs, which are the add-printer dialog's kind:
//   --printer-dialog-width, --printer-dialog-radius
//   --printer-dialog-scrim  the scrim's strength, a percentage for color-mix()

import tokens from '@resources/jusprin/ui/design-tokens.json';
import {
  applyPixelTokens,
  applySharedStaticTokens,
  elevationVariableNames,
  pixelVariableNames,
  sharedStaticVariableNames,
} from '@shared/tokens';

export { applyAppearance } from '@shared/tokens';

interface CardTokens {
  component: {
    projectCard: Record<string, number>;
    printerCard: Record<string, number>;
    statusDot: { size: number };
    swatch: Record<string, number>;
    icon: { sizes: number[] };
    button: { icon: Record<string, number> };
    menuRow: Record<string, number>;
    popover: Record<string, number>;
    printerSetup: { dialogWidth: number; radius: number; scrimAlpha: number };
  };
}

const { component } = tokens as unknown as CardTokens;

// The aspect ratio is a pair of unitless numbers, not a length, so it is
// written as one `4 / 3` variable rather than two pixel variables.
const ASPECT = '--project-card-thumbnail-aspect';

// An inline glyph -- the printer beside a name, the monitor on its button --
// is the smallest size on the documented icon scale, so it sits on the text
// line rather than beside it.
const GLYPH = '--glyph-size';

function pixelSection(section: Record<string, number>): Record<string, number> {
  const { thumbnailAspectWidth, thumbnailAspectHeight, ...rest } = section;
  void thumbnailAspectWidth;
  void thumbnailAspectHeight;
  return rest;
}

// The native scrim takes an alpha out of 255; CSS mixes the scrim colour by a
// percentage.
function printerDialog(): Record<string, string> {
  const setup = component.printerSetup;
  return {
    width: `${setup.dialogWidth}px`,
    radius: `${setup.radius}px`,
    scrim: `${Math.round((setup.scrimAlpha / 255) * 100)}%`,
  };
}

export function staticVariableNames(): string[] {
  return [
    ...sharedStaticVariableNames(),
    ...pixelVariableNames('project-card', pixelSection(component.projectCard)),
    ASPECT,
    ...pixelVariableNames('printer-card', component.printerCard),
    ...pixelVariableNames('status-dot', component.statusDot),
    ...pixelVariableNames('swatch', component.swatch),
    ...pixelVariableNames('button-icon', component.button.icon),
    ...pixelVariableNames('menu-row', component.menuRow),
    ...pixelVariableNames('popover', component.popover),
    ...Object.keys(printerDialog()).map((name) => `--printer-dialog-${name}`),
    ...elevationVariableNames(),
    GLYPH,
  ];
}

export function applyStaticTokens(): void {
  applySharedStaticTokens();
  const card = component.projectCard;
  applyPixelTokens('project-card', pixelSection(card));
  document.documentElement.style.setProperty(
    ASPECT,
    `${card.thumbnailAspectWidth} / ${card.thumbnailAspectHeight}`,
  );
  applyPixelTokens('printer-card', component.printerCard);
  applyPixelTokens('status-dot', component.statusDot);
  applyPixelTokens('swatch', component.swatch);
  applyPixelTokens('button-icon', component.button.icon);
  applyPixelTokens('menu-row', component.menuRow);
  applyPixelTokens('popover', component.popover);
  for (const [name, value] of Object.entries(printerDialog())) {
    document.documentElement.style.setProperty(`--printer-dialog-${name}`, value);
  }
  document.documentElement.style.setProperty(GLYPH, `${Math.min(...component.icon.sizes)}px`);
}
