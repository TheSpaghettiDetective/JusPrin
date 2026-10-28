// Applies the repository's design tokens as CSS custom properties. The
// universal part lives in WebShared/tokens.ts; Home adds its own card
// geometry, guarded in tests/brand/test_brand_tokens.cpp:
//   --project-card-<name>   min/max width, footer height, radius
//   --project-card-thumbnail-aspect  the unitless `4 / 3` for aspect-ratio
//   --printer-card-<name>   column width, picture size, radius
//   --progress-height       shared progress-track height from WebShared
//   --status-dot-size, --swatch-size, --swatch-radius
// and the printer card's menu, from the shared menu geometry:
//   --button-icon-<name>    the kebab button: width, height, icon size, radius
//   --menu-row-<name>       height, radius, side inset
//   --popover-<name>        vertical padding, row gap, radius
// and its rename and remove dialogs, which are the add-printer dialog's kind:
//   --printer-dialog-width, --printer-dialog-radius
//   --confirmation-dialog-width  the design system's Confirmation dialog, for
//                          a question the host asks over Home
//   --printer-dialog-scrim  the scrim's strength, a percentage for color-mix()
// and the Print history screen's list width:
//   --print-history-list-width
// and the gallery header's search and sort, the design system's field and
// select at the Main Page board's width:
//   --field-<name>, --select-<name>  height, radius, horizontal padding
//   --project-gallery-search-width

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
    projectGallery: Record<string, number>;
    homeHeader: Record<string, number>;
    printHistory: Record<string, number>;
    field: Record<string, number | string>;
    select: Record<string, number | string>;
    printerCard: Record<string, number>;
    statusDot: { size: number };
    swatch: Record<string, number>;
    icon: { sizes: number[] };
    button: { icon: Record<string, number> };
    menuRow: Record<string, number>;
    popover: Record<string, number>;
    printerDialog: { width: number; radius: number; scrimAlpha: number };
    confirmationDialog: Record<string, number>;
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
// The drop target's upload glyph is the largest size on the same scale.
const GLYPH_LARGE = '--glyph-size-large';

// A field's text role is a name, not a length; only its sizes become variables.
function pixels(section: Record<string, number | string>): Record<string, number> {
  return Object.fromEntries(Object.entries(section).filter(([, value]) => typeof value === 'number')) as Record<string, number>;
}

function pixelSection(section: Record<string, number>): Record<string, number> {
  const { thumbnailAspectWidth, thumbnailAspectHeight, ...rest } = section;
  void thumbnailAspectWidth;
  void thumbnailAspectHeight;
  return rest;
}

// The native scrim takes an alpha out of 255; CSS mixes the scrim colour by a
// percentage.
function printerDialog(): Record<string, string> {
  const setup = component.printerDialog;
  return {
    width: `${setup.width}px`,
    radius: `${setup.radius}px`,
    scrim: `${Math.round((setup.scrimAlpha / 255) * 100)}%`,
  };
}

export function staticVariableNames(): string[] {
  return [
    ...sharedStaticVariableNames(),
    ...pixelVariableNames('project-card', pixelSection(component.projectCard)),
    ASPECT,
    ...pixelVariableNames('project-gallery', component.projectGallery),
    ...pixelVariableNames('home-header', component.homeHeader),
    ...pixelVariableNames('print-history', component.printHistory),
    ...pixelVariableNames('field', pixels(component.field)),
    ...pixelVariableNames('select', pixels(component.select)),
    ...pixelVariableNames('printer-card', component.printerCard),
    ...pixelVariableNames('status-dot', component.statusDot),
    ...pixelVariableNames('swatch', component.swatch),
    ...pixelVariableNames('button-icon', component.button.icon),
    ...pixelVariableNames('menu-row', component.menuRow),
    ...pixelVariableNames('popover', component.popover),
    ...Object.keys(printerDialog()).map((name) => `--printer-dialog-${name}`),
    ...pixelVariableNames('confirmation-dialog', component.confirmationDialog),
    ...elevationVariableNames(),
    GLYPH,
    GLYPH_LARGE,
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
  applyPixelTokens('project-gallery', component.projectGallery);
  applyPixelTokens('home-header', component.homeHeader);
  applyPixelTokens('print-history', component.printHistory);
  applyPixelTokens('field', pixels(component.field));
  applyPixelTokens('select', pixels(component.select));
  applyPixelTokens('printer-card', component.printerCard);
  applyPixelTokens('status-dot', component.statusDot);
  applyPixelTokens('swatch', component.swatch);
  applyPixelTokens('button-icon', component.button.icon);
  applyPixelTokens('menu-row', component.menuRow);
  applyPixelTokens('popover', component.popover);
  for (const [name, value] of Object.entries(printerDialog())) {
    document.documentElement.style.setProperty(`--printer-dialog-${name}`, value);
  }
  applyPixelTokens('confirmation-dialog', component.confirmationDialog);
  document.documentElement.style.setProperty(GLYPH, `${Math.min(...component.icon.sizes)}px`);
  document.documentElement.style.setProperty(GLYPH_LARGE, `${Math.max(...component.icon.sizes)}px`);
}
