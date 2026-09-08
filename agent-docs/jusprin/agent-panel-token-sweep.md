# Agent panel token sweep

**Status:** Complete, 2026-09-08.

`src/slic3r/GUI/JusPrin/AgentUI/src/styles.css` now resolves every color,
radius, and type metric through the custom properties `tokens.ts` writes from
`resources/jusprin/ui/design-tokens.json` (`--<group>-<name>`,
`--radius-<name>`, `--font-<role>`, `--font-mono-<role>`), and every padding,
margin, and gap sits on the spacing scale. The two text glyphs that had been
sized off the scale (`+` and `×`) are SVG masks under
`resources/jusprin/ui/icons/`.

The sweep is guarded by `src/slic3r/GUI/JusPrin/AgentUI/src/styles.test.ts`,
which reads the stylesheet from disk and fails, naming the line, on any hex or
rgb color, any pixel `border-radius`, any `font-size`, `line-height`,
`font-weight`, or `font` shorthand with a pixel literal outside `@font-face`,
and any spacing value off the scale. It runs with `npm test` in that package.

The native side of the same sweep is tracked separately; see
[design-system.md](design-system.md) for the roles and scales.
