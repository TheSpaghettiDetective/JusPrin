// Applies the repository's semantic design tokens as CSS custom properties.
// resources/jusprin/ui/design-tokens.json is the implementation source of
// truth; components must use var(--...) rather than hard-coded colors.

import tokens from '@resources/jusprin/ui/design-tokens.json';
import type { Appearance } from './bridge/protocol';

type TokenGroup = Record<string, string>;
type SemanticMode = Record<string, TokenGroup>;

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
