# Home screen handoff

**Status:** Implementation handoff for the JusPrin Home screen. Design is settled; the hosting decision is not.

> **Implementation note (2026-09-12).** Built as option **B** (§5), a fork-owned page at
> `src/slic3r/GUI/JusPrin/HomeUI/` over the `jusprin-home-bridge` protocol. Three things the
> frames settled that this document does not state: the page draws **no top bar** — the native
> shell owns the header above it, and a second one would duplicate it; **Import** and **+ New**
> sit in the gallery's own header row, not the top bar; and an **idle printer collapses to a
> single row**, name and dot only. `surface.selected` was left alone and the highlighted card
> uses `surface.subtle` (§4). Per-project status (§6) is stubbed: the host sends
> `kind: "unknown"` with the file's modified time, except where a printing device's job name
> matches a project, which sends `kind: "printing"`. §8 remains undesigned and unbuilt.

Home is the screen a user lands on before opening a project: a gallery of project cards in the main area, a narrower printer column on the right. The design is finished and lives in Figma — open it first (§1). This document deliberately does not describe the mockup; it covers what the mockup cannot say: how the layout must behave at other window sizes, which values still need tokens, where the code attaches in this repository, where the data comes from, and which questions are still open.

Read [product-definition.md](product-definition.md) §3.1 for what Home owes the product, [design-system.md](design-system.md) "Writing UI code" before typing any value, and [fork-stewardship.md](fork-stewardship.md) before changing any OrcaSlicer-owned file.

---

## 1. Open the mockup before reading further

The design is in Figma. Go and look at it — this document does not restate what the frames already show, and a paraphrase of a layout goes stale the first time someone nudges it.

Figma file **JusPrin v2**, page **Main Page**:

| Frame | Node ID | Link |
|---|---|---|
| `home-light` | `591:1045` | `https://www.figma.com/design/jo9J1sK9ZZ0vxncWnSp0vH/JusPrin-v2?node-id=591-1045` |
| `home-dark` | `601:1267` | `https://www.figma.com/design/jo9J1sK9ZZ0vxncWnSp0vH/JusPrin-v2?node-id=601-1267` |

Two ways in, depending on what your session has:

- **Figma MCP tools**, if they are connected: `get_screenshot` for the rendered frame, `get_design_context` for structure and the variables each layer is bound to, `get_metadata` for the layer tree. Pass the node IDs above. Note that a View-seat plan caps these calls per month — check `whoami` first and spend them deliberately.
- **A browser**, otherwise: open a link above, which selects that frame. `Shift+2` zooms to the selection and `Cmd+\` hides the Figma panels for a clean view. Select any layer to read its real size and its bound variables in the Design panel on the right.

Read both frames: light and dark are not the same screen with swapped colors, and some problems (a black filament swatch on a dark card) only show in one.

Two cautions. Other frames in the same file (`prepare-first-print-light`, `print-timeline-*`, `chat-list-panel`) are different surfaces — don't borrow sizes from them. And `home-light` overlaps a neighbouring frame on the canvas; that is cosmetic, not a layout signal.

Where Figma and the token file disagree, the token file wins (design-system rule 5). One known disagreement is in §4.

The rest of this document is only what the frames cannot tell you: how the layout must behave at sizes other than 1280 × 800, which values need tokens, where the code attaches, where the data comes from, and which decisions were made the hard way.

---

## 2. How the layout behaves at other window sizes

The frames are a single 1280 × 800 snapshot. They cannot express what happens when the window is a different size, and that is the part the earlier wireframe got wrong: a fixed two-column grid grows cards to 1088 px wide on a 27-inch monitor while leaving only four projects visible. Build to these rules, and treat the frames as one instance of them.

| Element | Rule |
|---|---|
| Printer column | Fixed 320 DIP, right side, full height below the top bar |
| Gallery padding | 24 DIP |
| Grid gaps | 16 DIP, horizontal and vertical |
| Card width | Minimum 240, maximum 320 DIP; as many columns as fit |
| Thumbnail | Width of the card, aspect ratio **4:3**, model fitted whole — never cropped |
| Card footer | 64 DIP: title (Body Bold, one line, ellipsis) over status (Body Small) |
| Card height | Thumbnail + footer, so the card is near-square (≈293 × 286 at the Figma width) |
| Gallery | Scrolls vertically; rows are equal height |

What that produces, assuming a 48 DIP app top bar plus OS chrome:

| Window | Columns | Card | Projects visible |
|---|---|---|---|
| 1280 × 800 (Figma frame) | 3 | 293 × 284 | 6 + a partial row |
| 1512 × 982 (MacBook Pro 14) | 4 | 274 × 270 | 8 |
| 1920 × 1080 at 100% | 6 | 245 × 248 | 18 |
| 2560 × 1440 | 8 | 260 × 259 | 32 |

A 4:3 thumbnail lets a three-quarter-view render fill about 75% of the card width. Wider ratios waste width on empty build plate: 16:9 leaves the model at 56%. If a title must wrap to two lines, keep cards in a row equal height rather than letting one grow.

---

## 3. Why parts of it look the way they do

The frames show *what* the screen looks like. These are the decisions behind the details that are easy to undo by accident, each one arrived at by getting it wrong first.

- **The printing project card is tinted, not filled.** It uses `surface.subtle` with a 1 px `status.success` border, and its status line and dot in `status.success`. Filling the card with the status color was tried: it hid the status text (green on green) and read as an error. A grey tint was also tried and read as disabled.
- **Only a printing printer gets a colored dot.** Idle printers take a neutral dot. An earlier version gave both the same green and the two states became indistinguishable.
- **Filament swatches carry a 1 px border.** Without it the black spool vanishes on the dark card and the white one on the light card.
- **Thumbnails fit the model whole and fill the frame.** No cropping, and no empty band above and below. Both failure modes appeared in review.
- **Import and Launch monitor are outlined buttons, not text.** As plain text they stopped reading as controls.
- **The sample content in the frames is placeholder.** Nine projects, an X1 Carbon and a Prusa MK4 illustrate the states; they are not a spec for what to show.

---

## 4. Tokens

Every color, radius, font and control size comes from `resources/jusprin/ui/design-tokens.json`. Native code reads it through `ShellTheme` (`palette(dark)`, `metrics()`, `font(role)`) and dresses stock controls through `Shell/ShellRecipes.hpp`. A web page uses only the CSS variables (`--<group>-<name>`, `--radius-<name>`, `--font-<role>`, `--button-<recipe>-padding`). Layout spacing may stay literal if it is on the scale: 4, 8, 12, 16, 20, 24, 32, 40, 48.

| Surface element | Token |
|---|---|
| Page background | `surface.canvas` |
| Card background, printing-card fill | `surface.subtle` |
| Printer card background | `surface.raised` |
| Card and slot borders | `border.subtle` |
| Printing border, status text and dot | `status.success` |
| Idle printer dot | `text.secondary` |
| + New | `action.primary` / `action.primary.text` |
| Import, Launch monitor | `action.secondary` + `action.secondary.border` |
| Title | `text.primary` at role `bodyBold` |
| Status line | `text.secondary` at role `bodySmall` |
| Section labels | role `label`, uppercase, letterspaced |
| Card radius | `radius` standard scale; swatches use `component.swatch` (24, radius from the same entry) |

**Values with no token yet.** Design-system rule 2 says add the token before writing the number, and extend `tests/brand/test_brand_tokens.cpp` so it cannot silently move. This screen needs at least: project-card minimum and maximum width, thumbnail aspect ratio, footer height, grid gap if it is treated as a component rather than layout spacing, and progress-bar height. Add them under a new `component.projectCard` / `component.printerCard` section rather than typing 240, 320, 64.

**Known Figma/code gap:** the token file defines `surface.selected` (light `#EDE7F2`, dark `#4B3E57`) but that variable does not exist in the Figma file — its collection only carries `surface/canvas`, `subtle` and `raised`. The mockups therefore use `surface.subtle` for the highlighted card. Either add the variable to Figma or decide that `surface.subtle` is the right token here; do not introduce a third value.

---

## 5. Where the code goes — decision required

**How Home works today.** `MainFrame` creates a `WebViewPanel` and adds it as the Notebook page at `MainFrame::tpHome` (`src/slic3r/GUI/MainFrame.cpp:1312`), loading `resources/web/homepage/index.html`. OrcaSlicer pushes the recent-project list into that page with `SendRecentList`. In the JusPrin shell, the header's Home button toggles the Notebook between `tpHome` and `tp3DEditor` (`src/slic3r/GUI/JusPrin/Shell/StatusRow.cpp:418`). So JusPrin already navigates to Home; what it shows there is still OrcaSlicer's page.

Two ways to build the new screen:

**A. Native wxWidgets panel.** A fork-owned panel under `src/slic3r/GUI/JusPrin/Shell/`, registered in `JusPrin/sources.cmake`, shown by `ShellController` in place of the stock Home page. Consistent with the rest of the shell, reads `ShellTheme` directly, no new command surface. Cost: custom-drawn cards, hover states, a responsive column count, image scaling and a scrolling grid are all hand-written and are the slow part of wx work.

**B. Fork-owned web page in a WebView.** A local page swapped in where `tpHome` is shown, built like `JusPrin/AgentUI` (React/TypeScript, token CSS variables, vitest). Cost: it needs a typed command and event surface — open project, import, new project, launch monitor, add printer — and it must not own project state.

**Recommendation: B.** The screen is an image grid with responsive columns, hover and scrolling, which CSS does for free; `AgentUI` already has the toolchain, the token variables and the test setup; and Home is already a web page in OrcaSlicer, so the hosting pattern is proven. This is a recommendation, not a settled decision — confirm it before starting, since it adds a second WebView surface and a protocol.

Either way, **the swap must be an additive seam**: the shell chooses what is shown at Home; do not rewrite OrcaSlicer's `WebViewPanel` or its homepage assets. Read [fork-stewardship.md](fork-stewardship.md) first and record the rebase evidence it requires.

---

## 6. Data the screen needs

| Need | Source | Confidence |
|---|---|---|
| Recent projects (name, path, modified time) | `wxGetApp().app_config->get_recent_projects()`; `MainFrame::get_recent_projects(tree, images)` already assembles name/path/time/image for the stock page — reuse it rather than writing a second one | Verified in code |
| Project thumbnails | `MainFrame::FileHistory::LoadThumbnails()` reads them with `bbs_3mf_get_thumbnail`; `GetThumbnailUrl(i)` returns the URL | Verified in code |
| Configured printers | `PresetBundle::physical_printers` | Name verified; call site not traced |
| Live printer state, progress, connection | OrcaSlicer's device layer (`src/slic3r/GUI/DeviceManager.hpp`), as used by the Monitor tab | **Not verified — trace this before designing the data flow** |
| Filament colors per printer | Fork-owned `Workspace::SpoolStore` (`Spool.colour` is a `#RRGGBB` string, scoped per printer preset) | Verified in code |
| Per-project status (Draft / Sliced / Printing on X) | **No existing store.** Recent-file history knows nothing about slice state, and "printing on X1 Carbon" is a device fact joined to a project | Open design question |

That last row is the substantive gap. The status line is central to the design, and nothing in the codebase records it today. Decide where it lives — most likely fork-owned metadata beside `Agent/ProjectPersistence` — and raise it before implementing, rather than inventing a store inside the view.

---

## 7. Done means

1. Guards pass: `brand_tokens_tests`, `shell_theme_tests`, and `npm test` in `AgentUI` if the page is web.
2. No literal color, radius, font size or control size anywhere in the new code; new tokens are in the JSON and covered by a guard.
3. Checked in the running app at three window widths — 1280, 1512 and 1920 logical px — with the column count, card width and 4:3 thumbnail as specified in §2. The guards prove the code went through the tokens; only looking proves it is right.
4. Light and dark both checked, including that the black filament swatch is visible on dark and idle printers do not look like printing ones.
5. Long project names truncate rather than reflow the grid; a gallery of 30+ projects scrolls.
6. If the panel is native, capture it to a bitmap and look at the image. Unit tests passing has previously hidden real rendering defects in this fork.

---

## 8. Deliberately not designed yet

These are known holes, not oversights — raise them rather than inventing answers:

- **First run:** no printers, no projects. The most important onboarding moment; hardware discovery should lead.
- **Many projects:** search, sort, recent-versus-all. The design shows nine cards.
- **Printer fault state:** the design only covers printing and idle. Faults are meant to interrupt (product-definition §"Interruptive").
- **Project versions and backups:** listed as a Home responsibility in product-definition §3.1, absent from the design.
- **Agent entry on Home:** JusPrin is agent-piloted, but Home offers no way to start by describing what you want.
- **Extra card metadata:** target printer, material, last-edited time, estimated print time.
