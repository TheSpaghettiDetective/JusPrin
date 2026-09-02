# JusPrin design system

**Status:** Canonical product and brand guidance for `jusprin-newui`.

JusPrin should make advanced 3D printing feel immediate: a confident path from model to machine that removes detours without hiding control. The product should feel focused, assured, inventive, and approachable. Its voice is direct, capable, and optimistic—never cute, cryptic, or over-engineered.

The original JusPrin logo was supplied by the user and restored from repository revision `6eecb78b19`. The purple gradient endpoints were verified from the source SVG. Positioning language in the POC brand kit is proposed product direction, not an externally sourced market claim.

## Brand behavior

- **Focused:** every element earns its place.
- **Assured:** guidance is calm and actions are decisive.
- **Inventive:** the product is technical without being intimidating.
- **Approachable:** use plain language instead of slicer jargon where possible.
- Make the next action obvious. Brand behavior should feel as focused as the visual system.

The logo suggests an end-to-end journey through its orbit, precision and momentum through the crossing diagonals, flexible control through the open circle, and technical confidence softened by warmth through the purple gradient.

Write the name exactly as **JusPrin**: capital J and P, no space, never all caps in body copy. Use surrounding text color for the name in ordinary copy; do not force a gradient into small text.

## Logo use

The primary expressions are the horizontal lockup, the compact product mark, and the text wordmark. Use the compact mark for application icons and small product contexts.

- Clear space: at least one quarter of the mark width on every side.
- Minimum digital mark: 24 px.
- Minimum digital horizontal lockup: 120 px.
- Minimum printed mark: 8 mm.
- Minimum printed lockup: 32 mm.
- Below these sizes, use the mark alone and verify that the orbit arcs remain distinct.

Always use the original artwork, preserve its aspect ratio and components, maintain clear space, and place it on White, Mist, Ink, or another quiet solid field. Do not stretch, rotate, redraw, arbitrarily recolor, outline, shadow, separate, or place it directly over visual noise. Monochrome is allowed only when required. If the center crossing or orbit loses clarity, increase size, contrast, or space.

## Brand palette

| Name | Value | Use |
|---|---:|---|
| Orbit | `#3A2D64` | Primary brand color and decisive light-mode action |
| Violet | `#6F4F84` | Secondary brand color, headings, and essential boundaries |
| Bloom | `#CE84B7` | Expressive accent and dark-mode primary action |
| Ink | `#17131F` | Primary text and dark canvas |
| Mist | `#F6F2F7` | Soft canvas and grouped surfaces |
| Trace | `#E8DFEB` | Light dividers and quiet structure |
| White | `#FFFFFF` | Primary light canvas |
| Success | `#2D6A55` | Light success state |
| Danger | `#B44757` | Light error or destructive state |
| Warning | `#D49A2A` | Light warning state |

The signature gradient runs from Orbit to Bloom. Reserve it for the original logo, splash, onboarding, and rare hero moments. Do not use it as ordinary product chrome.

Product code must resolve colors through semantic tokens in `resources/jusprin/ui/design-tokens.json`; raw palette values are not a component API.

## Semantic color behavior

Dark mode is a semantic remapping, not a visual inversion. Preserve hierarchy, density, and geometry while choosing the dark value for the same named intent.

Core mappings include:

| Token | Light | Dark | Purpose |
|---|---:|---:|---|
| `surface.canvas` | `#FFFFFF` | `#17131F` | Main application background |
| `surface.subtle` | `#F6F2F7` | `#211B2A` | Sidebars and grouped settings |
| `surface.raised` | `#FFFFFF` | `#2B2336` | Dialogs, menus, elevated panels |
| `text.primary` | `#17131F` | `#F6F2F7` | Headings, values, body copy |
| `text.secondary` | `#5F5767` | `#CFC5D3` | Descriptions and secondary labels |
| `border.subtle` | `#E8DFEB` | `#4B3E57` | Dividers and quiet structure |
| `border.strong` | `#6F4F84` | `#7D6A8D` | Focus and essential boundaries |
| `action.primary` | `#3A2D64` | `#CE84B7` | Decisive primary action |
| `action.primary.hover` | `#4B3A77` | `#D99AC5` | Pointer hover |
| `action.primary.pressed` | `#2B214F` | `#B76CA0` | Pressed or active state |

The JSON token file is authoritative and contains the complete action, status, selected-surface, disabled, text, and component values.

## Typography

Product UI uses fonts already shipped by OrcaSlicer:

- HarmonyOS Sans SC Regular for body and controls.
- HarmonyOS Sans SC Bold for hierarchy and actions.
- NanumGothic for Korean and Sarabun for Thai where the existing application requires them.
- The system GUI font as fallback.
- The system teletype font only for measurements, temperatures, filenames, machine status, and other technical values.

Never require Menlo or another platform-specific face. The broader marketing brand kit used Avenir Next for display and marketing; that is not the cross-platform product UI requirement.

| Role | Size/line height in DIP | Weight |
|---|---:|---|
| Page title | 24/30 | Bold |
| Section | 18/24 | Bold |
| Body and control | 14/20 | Regular |
| Label | 12/16 | Bold |
| Dense metadata | 10/14 | Regular |

## Layout and geometry

Design for a dense, native, high-DPI desktop application on Windows, macOS, and Linux. Use device-independent pixels (DIP), not physical pixels. Verify 100%, 150%, and 200% scaling.

- Spacing scale: 4, 8, 12, 16, 20, 24, 32, 40, and 48 DIP.
- Standard control radius: 4 DIP.
- Compact or branded action radius: 8 DIP.
- Window action and large-dialog radius: 12 DIP.
- Reserve 16–24 DIP radii for onboarding and marketing, not the main application shell.

Avoid web-only assumptions such as oversized controls, blur-heavy or glass surfaces, fixed physical pixels, and platform-specific fonts.

## Components and states

Use the existing native component density and established SVG icon library. Functional clarity comes before logo-derived decoration.

- Functional icons: 16, 20, or 24 DIP, with PNG fallback when the platform path requires it.
- Orbit motif: progress, connection, or active workflow; verify stroke clarity at output size.
- Directional accent: at most one diagonal for momentum; avoid decorative competing angles.
- Compact button: 8×3 DIP padding, 8 DIP radius, dense metadata text.
- Window button: minimum 58×24 DIP, 12 DIP radius, 12 DIP text.
- Choice control: minimum 100×32 DIP, 12×8 DIP padding, 4 DIP radius.
- Parameter control: 120×26 DIP, 4 DIP radius, 14 DIP text.
- Icon button: 26×26 DIP, 16 DIP icon, 4 DIP radius.
- Expanded button: at least 32 DIP high, 12×8 DIP padding, 4 DIP radius.

Every interactive component must define normal, hover, pressed, disabled, focused, success, warning, and error states where relevant. Status must combine color with an icon and plain-language label.

## Accessibility

- Normal text requires at least 4.5:1 contrast; 3:1 is allowed only for large text.
- Essential control boundaries and state indicators require 3:1.
- Keyboard focus uses a visible 2 DIP ring and may not rely on a fill-color change alone.
- Orbit on White measures 12.07:1 in the source design system.
- Bloom on Ink measures 6.62:1.
- Light secondary text measures 6.89:1 on White.
- Dark secondary text measures 10.95:1 on Ink.

## Runtime application in the native app

The token file is applied to the running application in three layers. All
three read `resources/jusprin/ui/design-tokens.json`; none of them carries a
color literal of its own, and the whole layer touches OrcaSlicer code in only
three small, additive seams plus one attachment point.

1. **Fork-owned surfaces** (`src/slic3r/GUI/JusPrin/Shell/*`, the React Agent
   page) resolve colors through `ShellTheme` and `tokens.ts` directly.
2. **Retained OrcaSlicer surfaces** are retinted through the color tables
   OrcaSlicer already routes its colors through, installed once per appearance
   mode by `JusPrin/Brand/BrandPalette.cpp` from the end of
   `GUI_App::init_label_colours`:
   - `StateColor::SetColorOverrides` maps OrcaSlicer's brand and surface
     literals (teal family, grays, whites, text grays) to semantic tokens before
     OrcaSlicer's own dark-mode remap runs. Every widget built on `StateColor`
     switches at once.
   - `BitmapCache::SetColorReplaces` rewrites the teal in the SVG icon set to
     the action color at load time, and the monochrome brand mark to the text
     color.
   - The ImGui panels, the canvas selection rectangle, and the About dialog
     are left alone in source: those surfaces are slated to be hidden or
     replaced, so editing them would be divergence with no lasting value.
   - The mapping table in `BrandPalette.cpp` is the reviewed teal-to-purple
     decision. Extend it there; never add a purple literal to an OrcaSlicer file.
   - Widgets that paint the teal without going through `StateColor` (hyperlinks,
     the settings tab underline, notification accents, Bambu-only dialogs) are
     deliberately left teal; the cost of editing them in busy upstream files
     outweighs surfaces JusPrin rarely or never shows.
3. **Identity** comes from two data-only mechanisms:
   - `resources/jusprin/overlay/images/` holds the splash, About and horizontal
     lockups, monochrome mark, wizard watermark, and window icons under
     OrcaSlicer's own asset names. `Slic3r::var()` checks that directory
     first, so no logo call site is edited. See the README there.
   - `Brand/brand_catalogs.py` builds `resources/i18n/<lang>/<app key>.mo`
     from upstream's `.po` files with the product name substituted, plus an
     English catalog for the source strings, so every language reads "JusPrin"
     without touching a string or macro in upstream code. Attribution strings
     in About are excluded by the script.
   - The generic printer profiles' plate texture,
     `resources/profiles/Custom/orcaslicer_bed_texture.svg`, is overwritten
     with the JusPrin lockup; the file name stays because five profiles
     reference it.

`tests/brand/test_brand_tokens.cpp` fails the build if a token edit breaks the
contrast rules above or removes a token from one mode only.

Known gaps, all deliberate: ImGui panels and canvas notifications keep the
teal accent; the first-run setup wizard (an OrcaSlicer HTML page) is
Orca-branded until JusPrin's own onboarding replaces it; the About dialog
links to the OrcaSlicer website; vendor printer profiles other than the
generic ones keep their own plate textures; dark mode and the Preview canvas
were not visually verified.

## Design handoff requirements

Every high-fidelity product frame must:

1. Include paired light and dark versions using the same semantic token names.
2. State the mode, viewport, component variants, keyboard focus, and any deliberate exception.
3. Use DIP, native density, and the standard radius scale.
4. Use HarmonyOS Sans SC and system teletype for product UI.
5. Show normal, hover, pressed, disabled, focus, error, warning, and success states as applicable.
6. Test long filenames, localized labels, dense settings, empty states, and narrow windows.
7. Use the original logo and existing functional icon system.
8. Label and justify any value that is not in the token files.

The POC decks, rendered examples, horizontal lockup PNG, and generation sources remain available for visual-reference work through [POC reference](poc-reference.md). They are not runtime dependencies.
