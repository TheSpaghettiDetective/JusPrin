// Applies the repository's design tokens as CSS custom properties.
// resources/jusprin/ui/design-tokens.json is the implementation source of
// truth; the stylesheet must use var(--...) rather than literal colors,
// radii, or type metrics. styles.test.ts enforces that on the stylesheet.

import tokens from '@resources/jusprin/ui/design-tokens.json';
import type { Appearance } from './bridge/protocol';

type TokenGroup = Record<string, string>;
type SemanticMode = Record<string, TokenGroup>;

interface TypeRole {
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
function kebab(name: string): string {
  return name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`);
}

// Every --font-* and --button-* variable applyStaticTokens writes, so a test
// can check that the stylesheet only asks for variables that exist.
export function staticVariableNames(): string[] {
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
export function applyStaticTokens(): void {
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

export function applyAppearance(appearance: Appearance): void {
  const semantic = (tokens as { semantic: Record<string, SemanticMode> }).semantic;
  const mode = semantic[appearance] ?? semantic.light;
  const root = document.documentElement;
  for (const [group, values] of Object.entries(mode)) {
    for (const [name, value] of Object.entries(values)) {
      if (typeof value === 'string') root.style.setProperty(`--${group}-${name}`, value);
    }
  }
  root.dataset.appearance = appearance;
}
