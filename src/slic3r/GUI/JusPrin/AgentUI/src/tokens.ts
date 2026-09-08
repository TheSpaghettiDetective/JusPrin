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

interface StaticTokens {
  dimension: { radius: Record<string, number> };
  typography: {
    ui: { family: string; cssFallback: string };
    technical: { cssFamily: string };
    roles: Record<string, TypeRole>;
  };
}

// The three roles that also exist in the technical (monospace) face: Figma's
// Mono Body, Mono Label, and Mono Metadata share the UI roles' metrics.
const monoRoles = ['body', 'label', 'metadata'] as const;

function fontShorthand(role: TypeRole, family: string): string {
  return `${role.weight} ${role.size}px/${role.lineHeight}px ${family}`;
}

// The token file names roles in camelCase (pageTitle, bodyBold); CSS custom
// properties take the kebab-case form (--font-page-title, --font-body-bold).
function kebab(name: string): string {
  return name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`);
}

// Every --font-* variable applyStaticTokens writes, so a test can check that
// the stylesheet only asks for variables that exist.
export function fontVariableNames(): string[] {
  const { typography } = tokens as unknown as StaticTokens;
  return [
    ...Object.keys(typography.roles).map((name) => `--font-${kebab(name)}`),
    ...monoRoles.map((name) => `--font-mono-${kebab(name)}`),
  ];
}

// Colors depend on the appearance; everything below does not, so it is
// written once at startup:
//   --radius-<name>       e.g. --radius-container: 8px
//   --font-<role>         full `font` shorthand in the UI face, kebab-case
//   --font-mono-<role>    the same metrics in the technical face
export function applyStaticTokens(): void {
  const { dimension, typography } = tokens as unknown as StaticTokens;
  const root = document.documentElement;
  for (const [name, value] of Object.entries(dimension.radius)) {
    root.style.setProperty(`--radius-${name}`, `${value}px`);
  }
  const uiFamily = `'${typography.ui.family}', ${typography.ui.cssFallback}`;
  for (const [name, role] of Object.entries(typography.roles)) {
    root.style.setProperty(`--font-${kebab(name)}`, fontShorthand(role, uiFamily));
  }
  for (const name of monoRoles) {
    const role = typography.roles[name];
    root.style.setProperty(`--font-mono-${kebab(name)}`, fontShorthand(role, typography.technical.cssFamily));
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
