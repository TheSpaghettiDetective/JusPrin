// The part of the design-token application that every JusPrin web surface
// needs: resources/jusprin/ui/design-tokens.json becomes CSS custom
// properties, so a stylesheet can use var(--...) instead of a literal color,
// radius, or type metric. Each page adds its own component variables on top
// (the Agent page's thread row, Home's cards) and composes the variable-name
// list its own styles test checks against.

import tokens from '@resources/jusprin/ui/design-tokens.json';

export type Appearance = 'light' | 'dark';

type TokenGroup = Record<string, string>;
type SemanticMode = Record<string, TokenGroup>;

export interface TypeRole {
  size: number;
  lineHeight: number;
  weight: number;
}

interface ButtonRecipe {
  paddingX?: number;
  paddingY?: number;
}

interface StaticTokens {
  dimension: { radius: Record<string, number> };
  typography: {
    ui: { family: string; cssFallback: string };
    code: TypeRole & { cssFamily: string };
    roles: Record<string, TypeRole>;
  };
  component: { button: Record<string, ButtonRecipe> };
}

function fontShorthand(role: TypeRole, family: string): string {
  return `${role.weight} ${role.size}px/${role.lineHeight}px ${family}`;
}

// The token file names roles in camelCase (pageTitle, bodyBold); CSS custom
// properties take the kebab-case form (--font-page-title, --font-body-bold).
export function kebab(name: string): string {
  return name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`);
}

// Writes one `--<prefix>-<name>: <value>px` per entry, for a page's own
// component section of the token file.
export function applyPixelTokens(prefix: string, values: Record<string, number>): void {
  const root = document.documentElement;
  for (const [name, value] of Object.entries(values)) {
    root.style.setProperty(`--${prefix}-${kebab(name)}`, `${value}px`);
  }
}

export function pixelVariableNames(prefix: string, values: Record<string, unknown>): string[] {
  return Object.keys(values).map((name) => `--${prefix}-${kebab(name)}`);
}

// Every --font-* and --button-* variable applySharedStaticTokens writes, so a
// page's styles test can check that its stylesheet only asks for variables
// that exist.
export function sharedStaticVariableNames(): string[] {
  const { typography, component } = tokens as unknown as StaticTokens;
  return [
    ...Object.keys(typography.roles).map((name) => `--font-${kebab(name)}`),
    '--font-code',
    ...Object.entries(component.button)
      .filter(([, recipe]) => recipe.paddingX !== undefined && recipe.paddingY !== undefined)
      .map(([name]) => `--button-${kebab(name)}-padding`),
  ];
}

// Colors depend on the appearance; everything below does not, so it is
// written once at startup:
//   --radius-<name>       e.g. --radius-container: 8px
//   --font-<role>         full `font` shorthand in the UI face, kebab-case
//   --font-code           the one monospace role, for code, keys, paths, IDs
//   --button-<recipe>-padding  "<y>px <x>px" from component.button; control
//                         padding follows the recipes, not the spacing scale
export function applySharedStaticTokens(): void {
  const { dimension, typography, component } = tokens as unknown as StaticTokens;
  const root = document.documentElement;
  for (const [name, value] of Object.entries(dimension.radius)) {
    root.style.setProperty(`--radius-${name}`, `${value}px`);
  }
  const uiFamily = `'${typography.ui.family}', ${typography.ui.cssFallback}`;
  for (const [name, role] of Object.entries(typography.roles)) {
    root.style.setProperty(`--font-${kebab(name)}`, fontShorthand(role, uiFamily));
  }
  root.style.setProperty('--font-code', fontShorthand(typography.code, typography.code.cssFamily));
  for (const [name, recipe] of Object.entries(component.button)) {
    if (recipe.paddingX === undefined || recipe.paddingY === undefined) continue;
    root.style.setProperty(`--button-${kebab(name)}-padding`, `${recipe.paddingY}px ${recipe.paddingX}px`);
  }
}

interface ElevationTier {
  offsetX: number;
  offsetY: number;
  blur: number;
  spread: number;
  color: string;
  opacity: Record<string, number>;
}

function elevationTiers(): Record<string, ElevationTier> {
  return (tokens as unknown as { elevation: Record<string, ElevationTier> }).elevation;
}

// Every --elevation-<tier> variable applyAppearance writes. They are appearance
// variables rather than static ones: the geometry is shared across modes but
// the opacity is not, so the value is rewritten whenever the mode changes.
export function elevationVariableNames(): string[] {
  return Object.keys(elevationTiers()).map((tier) => `--elevation-${kebab(tier)}`);
}

// "#000000" + 0.24 -> "rgba(0, 0, 0, 0.24)". The token keeps the colour as hex
// so it reads like every other colour in the file; CSS needs it with an alpha.
function rgba(hex: string, opacity: number): string {
  const value = hex.replace('#', '');
  const channel = (at: number) => parseInt(value.slice(at, at + 2), 16);
  return `rgba(${channel(0)}, ${channel(2)}, ${channel(4)}, ${opacity})`;
}

export function applyAppearance(appearance: Appearance): void {
  const semantic = (tokens as { semantic: Record<string, SemanticMode> }).semantic;
  const mode = semantic[appearance] ?? semantic.light;
  const root = document.documentElement;
  for (const [group, values] of Object.entries(mode)) {
    for (const [name, value] of Object.entries(values)) {
      if (typeof value === 'string') root.style.setProperty(`--${group}-${name}`, value);
    }
  }
  for (const [tier, shadow] of Object.entries(elevationTiers())) {
    const opacity = shadow.opacity[appearance] ?? shadow.opacity.light;
    root.style.setProperty(
      `--elevation-${kebab(tier)}`,
      `${shadow.offsetX}px ${shadow.offsetY}px ${shadow.blur}px ${shadow.spread}px ${rgba(shadow.color, opacity)}`,
    );
  }
  root.dataset.appearance = appearance;
}
