import fs from "node:fs/promises";
import path from "node:path";
import { Presentation, PresentationFile } from "@oai/artifact-tool";

const ROOT = "/Users/kenneth/Documents/Codex/2026-08-13/referenced-chatgpt-conversation-this-is-an";
const WORK = path.join(ROOT, "work/brand-kit");
const OUTPUT = path.join(ROOT, "outputs");
const ASSET = path.join(WORK, "resources/images");
const LOGO_PNG = path.join(ASSET, "JusPrin_192px.png");
const LOGO_SVG = path.join(ASSET, "JusPrin.svg");
const LOCKUP_PNG = path.join(WORK, "resources/web/homepage/img/jusprin_full_logo_inkscape_h.png");

const C = {
  orbit: "#3A2D64",
  violet: "#6F4F84",
  bloom: "#CE84B7",
  ink: "#17131F",
  mist: "#F6F2F7",
  trace: "#E8DFEB",
  white: "#FFFFFF",
  muted: "#716979",
  success: "#2D6A55",
  danger: "#B44757",
};
const FONT = "Avenir Next";
const MONO = "Menlo";
const SLIDE_W = 1280;
const SLIDE_H = 720;
const presentation = Presentation.create({ slideSize: { width: SLIDE_W, height: SLIDE_H } });

async function bytes(file) {
  const b = await fs.readFile(file);
  return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
}

async function writeBlob(file, blob) {
  await fs.writeFile(file, new Uint8Array(await blob.arrayBuffer()));
}

function addShape(slide, name, x, y, w, h, fill, opts = {}) {
  return slide.shapes.add({
    geometry: opts.geometry || "rect",
    name,
    position: { left: x, top: y, width: w, height: h, rotation: opts.rotation || 0 },
    fill: fill ?? "none",
    line: opts.line || { style: "solid", fill: "none", width: 0 },
    borderRadius: opts.radius,
    shadow: opts.shadow,
  });
}

function addText(slide, name, text, x, y, w, h, opts = {}) {
  const s = addShape(slide, name, x, y, w, h, "none", { geometry: "textbox" });
  s.text = text;
  s.text.style = {
    typeface: opts.typeface || FONT,
    fontSize: opts.size || 22,
    bold: opts.bold || false,
    color: opts.color || C.ink,
    alignment: opts.align || "left",
    verticalAlignment: opts.valign || "top",
    lineSpacing: opts.lineSpacing || 1.08,
    autoFit: opts.autoFit || "shrinkText",
    wrap: opts.wrap || "square",
    insets: { top: opts.padT || 0, right: opts.padR || 0, bottom: opts.padB || 0, left: opts.padL || 0 },
  };
  if (opts.fill) s.text.fill = opts.fill;
  return s;
}

function addLine(slide, name, x, y, w, h, color = C.trace, width = 2, rotation = 0) {
  return addShape(slide, name, x, y, w, h, color, { radius: 4, rotation });
}

async function addImage(slide, name, file, x, y, w, h, opts = {}) {
  const ext = path.extname(file).toLowerCase();
  return slide.images.add({
    blob: await bytes(file),
    contentType: ext === ".svg" ? "image/svg+xml" : "image/png",
    alt: opts.alt || name,
    fit: opts.fit || "contain",
    position: { left: x, top: y, width: w, height: h },
    geometry: opts.geometry || "rect",
    borderRadius: opts.radius,
  });
}

function addFooter(slide, page, dark = false) {
  const col = dark ? "#CFC5D3" : C.muted;
  addText(slide, `footer-${page}`, "JusPrin visual brand kit", 64, 680, 350, 20, { size: 13, color: col });
  addText(slide, `page-${page}`, String(page).padStart(2, "0"), 1160, 680, 56, 20, { size: 13, color: col, align: "right", typeface: MONO });
  slide.speakerNotes.textFrame.setText("[Sources]\n- JusPrin logo: user-provided asset, restored from local repository revision 6eecb78b19.\n[/Sources]");
}

function addHeader(slide, page, eyebrow, title, opts = {}) {
  const dark = opts.dark || false;
  addText(slide, `eyebrow-${page}`, eyebrow.toUpperCase(), 64, 48, 360, 24, { size: 14, bold: true, color: dark ? C.bloom : C.violet, typeface: MONO });
  addText(slide, `title-${page}`, title, 64, 78, 1110, 68, { size: 48, bold: true, color: dark ? C.white : C.ink, lineSpacing: 0.96 });
}

function orbitMotif(slide, x, y, size, color, opacityFill = "none") {
  addShape(slide, `orbit-${x}-${y}`, x, y, size, size, opacityFill, { geometry: "ellipse", line: { style: "solid", fill: color, width: 3 } });
  addLine(slide, `slash-a-${x}-${y}`, x + size * 0.12, y + size * 0.47, size * 0.76, size * 0.085, color, 0, -38);
  addLine(slide, `slash-b-${x}-${y}`, x + size * 0.12, y + size * 0.47, size * 0.76, size * 0.085, color, 0, 38);
}

function swatch(slide, x, y, w, h, color, name, hex, use, lightText = false) {
  addShape(slide, `swatch-${name}`, x, y, w, h, color, { radius: 18 });
  const tc = lightText ? C.white : C.ink;
  addText(slide, `swatch-name-${name}`, name, x + 18, y + 18, w - 36, 32, { size: 23, bold: true, color: tc });
  addText(slide, `swatch-hex-${name}`, hex, x + 18, y + 54, w - 36, 22, { size: 14, color: tc, typeface: MONO });
  addText(slide, `swatch-use-${name}`, use, x + 18, y + h - 50, w - 36, 34, { size: 15, color: tc });
}

await fs.mkdir(OUTPUT, { recursive: true });
await fs.mkdir(path.join(WORK, "renders"), { recursive: true });

// Prepare transparent, editable vector variants from the repository SVG.
let originalSvg = await fs.readFile(LOGO_SVG, "utf8");
const transparentSvg = originalSvg.replace(/\s*<rect\s+width="64"[\s\S]*?id="rect1"[\s\S]*?\/>/, "");
const whiteSvg = transparentSvg
  .replace(/fill:url\([^)]*\)/g, "fill:#ffffff")
  .replace(/stroke:url\([^)]*\)/g, "stroke:#ffffff");
const inkSvg = transparentSvg
  .replace(/fill:url\([^)]*\)/g, `fill:${C.ink}`)
  .replace(/stroke:url\([^)]*\)/g, `stroke:${C.ink}`);
const gradientSvgPath = path.join(OUTPUT, "JusPrin-mark.svg");
const whiteSvgPath = path.join(WORK, "JusPrin-mark-white.svg");
const inkSvgPath = path.join(WORK, "JusPrin-mark-ink.svg");
await fs.writeFile(gradientSvgPath, transparentSvg);
await fs.writeFile(whiteSvgPath, whiteSvg);
await fs.writeFile(inkSvgPath, inkSvg);
await fs.copyFile(LOGO_PNG, path.join(OUTPUT, "JusPrin-mark.png"));
await fs.copyFile(LOCKUP_PNG, path.join(OUTPUT, "JusPrin-horizontal-lockup.png"));

// 01 - Cover
{
  const s = presentation.slides.add();
  s.background.fill = C.mist;
  addShape(s, "cover-orbit", 836, -95, 520, 520, "none", { geometry: "ellipse", line: { style: "solid", fill: C.trace, width: 4 } });
  addLine(s, "cover-slash", 895, 120, 430, 48, C.trace, 0, -38);
  addText(s, "cover-kicker", "VISUAL BRAND KIT / 2026", 68, 58, 500, 24, { size: 14, bold: true, color: C.violet, typeface: MONO });
  await addImage(s, "JusPrin primary horizontal lockup", LOCKUP_PNG, 64, 176, 700, 220, { fit: "contain" });
  addText(s, "cover-title", "A focused identity for a\nfrictionless print experience.", 68, 440, 740, 120, { size: 42, bold: true, color: C.ink, lineSpacing: 1.02 });
  addText(s, "cover-subtitle", "Logo, color, type, spacing, shape language, usage rules, and applications.", 68, 586, 760, 42, { size: 20, color: C.muted });
  await addImage(s, "JusPrin logo mark", LOGO_PNG, 910, 290, 245, 245, { fit: "contain" });
  addFooter(s, 1);
}

// 02 - Positioning and personality
{
  const s = presentation.slides.add();
  s.background.fill = C.white;
  addHeader(s, 2, "Brand foundation", "Make advanced printing feel immediate.");
  addText(s, "positioning", "JusPrin is the confident path from model to machine - a focused 3D-printing experience that removes detours without hiding control.", 64, 180, 780, 110, { size: 28, color: C.ink, lineSpacing: 1.22 });
  addShape(s, "position-rule", 64, 322, 1138, 2, C.trace);
  const traits = [
    ["Focused", "Every element earns its place."],
    ["Assured", "Calm guidance, decisive actions."],
    ["Inventive", "Technical, never intimidating."],
    ["Approachable", "Plain language over jargon."],
  ];
  traits.forEach((t, i) => {
    const x = 64 + i * 284;
    addText(s, `trait-num-${i}`, `0${i + 1}`, x, 365, 44, 24, { size: 14, color: C.bloom, typeface: MONO, bold: true });
    addText(s, `trait-${i}`, t[0], x, 400, 245, 34, { size: 26, bold: true });
    addText(s, `trait-desc-${i}`, t[1], x, 447, 245, 56, { size: 18, color: C.muted, lineSpacing: 1.2 });
  });
  addText(s, "voice", "Voice: direct, capable, optimistic. Never cute, cryptic, or over-engineered.", 64, 584, 940, 36, { size: 20, color: C.violet, bold: true });
  addFooter(s, 2);
}

// 03 - Logo anchor
{
  const s = presentation.slides.add();
  s.background.fill = C.mist;
  addHeader(s, 3, "Logo", "The mark is the system's visual anchor.");
  addShape(s, "logo-stage", 64, 176, 470, 420, C.white, { radius: 26, shadow: "shadow-sm" });
  await addImage(s, "JusPrin logo mark hero", LOGO_PNG, 159, 256, 280, 280, { fit: "contain" });
  addText(s, "interpretation-heading", "Design interpretation", 600, 192, 500, 34, { size: 26, bold: true, color: C.violet });
  const points = [
    ["Orbit", "A continuous loop suggests an end-to-end print journey."],
    ["Crossing diagonals", "Precision and momentum converge at the center."],
    ["Open circle", "Control stays flexible, not closed or prescriptive."],
    ["Purple gradient", "Technical confidence softened by warmth."],
  ];
  points.forEach((p, i) => {
    const y = 255 + i * 86;
    addShape(s, `dot-${i}`, 602, y + 7, 12, 12, i % 2 ? C.bloom : C.orbit, { geometry: "ellipse" });
    addText(s, `logo-point-${i}`, p[0], 632, y, 240, 26, { size: 20, bold: true });
    addText(s, `logo-point-desc-${i}`, p[1], 632, y + 28, 500, 44, { size: 17, color: C.muted, lineSpacing: 1.17 });
  });
  addFooter(s, 3);
}

// 04 - Lockups and wordmark
{
  const s = presentation.slides.add();
  s.background.fill = C.white;
  addHeader(s, 4, "Logo system", "Use one name, three controlled expressions.");
  addText(s, "primary-label", "PRIMARY / HORIZONTAL LOCKUP", 64, 176, 380, 22, { size: 13, bold: true, color: C.violet, typeface: MONO });
  addShape(s, "lockup-stage", 64, 210, 706, 180, C.mist, { radius: 20 });
  await addImage(s, "JusPrin horizontal lockup", LOCKUP_PNG, 98, 234, 635, 130, { fit: "contain" });
  addText(s, "compact-label", "COMPACT / PRODUCT ICON", 836, 176, 350, 22, { size: 13, bold: true, color: C.violet, typeface: MONO });
  addShape(s, "icon-stage", 836, 210, 344, 180, C.ink, { radius: 20 });
  await addImage(s, "JusPrin compact mark", whiteSvgPath, 932, 244, 150, 112, { fit: "contain" });
  addText(s, "wordmark-label", "WORDMARK TREATMENT", 64, 438, 360, 22, { size: 13, bold: true, color: C.violet, typeface: MONO });
  addText(s, "wordmark-sample", "JusPrin", 64, 470, 390, 84, { size: 72, bold: true, fill: { type: "gradient", gradientKind: "linear", angleDeg: 0, stops: [{ offset: 0, color: C.orbit }, { offset: 100000, color: C.bloom }] } });
  addText(s, "wordmark-rules", "Avenir Next Heavy\nCapital J + capital P\nNo space, no all-caps\nTight optical tracking (-1% to -2%)", 560, 458, 520, 126, { size: 20, color: C.ink, lineSpacing: 1.28 });
  addText(s, "plain-text-rule", "In body copy, write JusPrin in the surrounding text color - never force the gradient at small sizes.", 64, 602, 1050, 38, { size: 18, color: C.muted });
  addFooter(s, 4);
}

// 05 - Color
{
  const s = presentation.slides.add();
  s.background.fill = C.mist;
  addHeader(s, 5, "Color", "A narrow purple spectrum carries the identity.");
  swatch(s, 64, 182, 262, 188, C.orbit, "Orbit", "#3A2D64", "Primary / accessible on white", true);
  swatch(s, 350, 182, 262, 188, C.violet, "Violet", "#6F4F84", "UI actions / headings", true);
  swatch(s, 636, 182, 262, 188, C.bloom, "Bloom", "#CE84B7", "Expressive accent only", false);
  swatch(s, 922, 182, 262, 188, C.ink, "Ink", "#17131F", "Text / dark surfaces", true);
  swatch(s, 64, 404, 262, 160, C.white, "White", "#FFFFFF", "Primary canvas", false);
  swatch(s, 350, 404, 262, 160, C.mist, "Mist", "#F6F2F7", "Soft canvas / panels", false);
  swatch(s, 636, 404, 262, 160, C.trace, "Trace", "#E8DFEB", "Dividers / hover fills", false);
  addShape(s, "gradient-card", 922, 404, 262, 160, { type: "gradient", gradientKind: "linear", angleDeg: 0, stops: [{ offset: 0, color: C.orbit }, { offset: 100000, color: C.bloom }] }, { radius: 18 });
  addText(s, "gradient-title", "Signature gradient", 940, 422, 225, 30, { size: 22, bold: true, color: C.white });
  addText(s, "gradient-code", "#3A2D64 -> #CE84B7", 940, 460, 225, 22, { size: 13, color: C.white, typeface: MONO });
  addText(s, "gradient-use", "Logo and rare hero moments", 940, 520, 225, 32, { size: 15, color: C.white });
  addText(s, "contrast-note", "Accessibility: Orbit and Violet pass AA for normal text on white; Bloom is reserved for large type, graphics, and accents.", 64, 604, 1110, 42, { size: 17, color: C.muted });
  addFooter(s, 5);
}

// 06 - Typography
{
  const s = presentation.slides.add();
  s.background.fill = C.white;
  addHeader(s, 6, "Typography", "Modern geometry. Clear hierarchy.");
  addText(s, "type-primary", "Avenir Next", 64, 174, 550, 80, { size: 64, bold: true, color: C.orbit });
  addText(s, "type-primary-desc", "Primary family / display, product UI, and marketing\nHeavy for headlines · Demi Bold for actions · Regular for body", 66, 260, 590, 70, { size: 19, color: C.muted, lineSpacing: 1.25 });
  addText(s, "mono-sample", "Menlo  0.20 mm  215 C", 710, 190, 470, 40, { size: 24, color: C.violet, typeface: MONO });
  addText(s, "mono-desc", "Technical companion / measurements, temperatures, filenames, and machine status. Use sparingly.", 710, 250, 450, 80, { size: 18, color: C.muted, lineSpacing: 1.25 });
  addShape(s, "type-rule", 64, 360, 1118, 2, C.trace);
  const specs = [
    ["Display", "48 / 52", "Heavy", "From model to making."],
    ["Heading 1", "36 / 42", "Demi Bold", "Prepare with confidence"],
    ["Heading 2", "24 / 30", "Demi Bold", "Print settings"],
    ["Body", "16 / 24", "Regular", "Clear instructions keep the workflow moving."],
    ["Label", "13 / 18", "Demi Bold", "SLICE & PRINT"],
  ];
  specs.forEach((p, i) => {
    const y = 388 + i * 50;
    addText(s, `spec-name-${i}`, p[0], 64, y, 140, 28, { size: 16, bold: true });
    addText(s, `spec-size-${i}`, p[1], 220, y, 120, 28, { size: 14, typeface: MONO, color: C.violet });
    addText(s, `spec-weight-${i}`, p[2], 360, y, 150, 28, { size: 16, color: C.muted });
    addText(s, `spec-copy-${i}`, p[3], 550, y - 3, 610, 34, { size: 19, bold: i < 3, color: C.ink });
  });
  addFooter(s, 6);
}

// 07 - Clear space and sizing
{
  const s = presentation.slides.add();
  s.background.fill = C.mist;
  addHeader(s, 7, "Logo geometry", "Give the mark room to move.");
  addText(s, "clear-space-label", "CLEAR SPACE", 64, 168, 300, 22, { size: 13, bold: true, color: C.violet, typeface: MONO });
  addShape(s, "clear-outer", 84, 208, 410, 410, "none", { line: { style: "dash", fill: C.bloom, width: 2 } });
  addShape(s, "clear-inner", 166, 290, 246, 246, C.white, { radius: 20 });
  await addImage(s, "JusPrin mark clear space", LOGO_PNG, 190, 314, 198, 198, { fit: "contain" });
  addText(s, "x-top", "X", 270, 222, 40, 30, { size: 18, bold: true, color: C.bloom, align: "center", typeface: MONO });
  addText(s, "x-left", "X", 105, 392, 40, 30, { size: 18, bold: true, color: C.bloom, align: "center", typeface: MONO });
  addText(s, "clear-rule", "X = 1/4 of the mark width. Keep at least X clear on every side; use more when the surrounding layout is visually active.", 552, 206, 600, 92, { size: 23, lineSpacing: 1.25 });
  addShape(s, "size-rule", 552, 330, 610, 2, C.trace);
  addText(s, "minimum-label", "MINIMUM SIZE", 552, 366, 300, 22, { size: 13, bold: true, color: C.violet, typeface: MONO });
  const minItems = [
    ["24 px", "Digital mark"],
    ["120 px", "Horizontal lockup"],
    ["8 mm", "Printed mark"],
    ["32 mm", "Printed lockup"],
  ];
  minItems.forEach((m, i) => {
    const x = 552 + (i % 2) * 300;
    const y = 404 + Math.floor(i / 2) * 90;
    addText(s, `min-val-${i}`, m[0], x, y, 130, 38, { size: 30, bold: true, color: C.orbit });
    addText(s, `min-desc-${i}`, m[1], x + 138, y + 8, 145, 28, { size: 16, color: C.muted });
  });
  addText(s, "size-caveat", "Below these sizes, use the mark only and verify the orbit arcs remain distinct.", 552, 590, 600, 34, { size: 17, color: C.muted });
  addFooter(s, 7);
}

// 08 - Shape and background language
{
  const s = presentation.slides.add();
  s.background.fill = C.ink;
  addHeader(s, 8, "Visual language", "Orbit. Direction. Calm containment.", { dark: true });
  const xs = [64, 444, 824];
  const titles = ["Orbit arcs", "Confident diagonals", "Rounded surfaces"];
  const descs = ["Use partial rings to signal progress, connection, or an active workflow.", "Use one directional stroke to add momentum - never a tangle of competing angles.", "Use 16-24 px radii for panels and app tiles; pair with quiet, generous spacing."];
  orbitMotif(s, 104, 210, 200, C.bloom);
  addShape(s, "diagonal-stage", 482, 232, 228, 156, C.orbit, { radius: 24 });
  addLine(s, "diagonal-a", 493, 294, 210, 20, C.bloom, 0, -28);
  addShape(s, "rounded-one", 856, 224, 258, 176, C.mist, { radius: 26 });
  addShape(s, "rounded-two", 900, 268, 258, 176, C.violet, { radius: 26 });
  titles.forEach((t, i) => {
    addText(s, `visual-title-${i}`, t, xs[i], 452, 315, 32, { size: 25, bold: true, color: C.white });
    addText(s, `visual-desc-${i}`, descs[i], xs[i], 496, 315, 82, { size: 17, color: "#CFC5D3", lineSpacing: 1.23 });
  });
  addText(s, "background-rule", "Background rule: prefer White, Mist, or Ink. On photography, place the logo on a quiet solid field - never directly over visual noise.", 64, 610, 1110, 42, { size: 18, color: C.bloom, bold: true });
  addFooter(s, 8, true);
}

// 09 - Do and don't
{
  const s = presentation.slides.add();
  s.background.fill = C.white;
  addHeader(s, 9, "Usage", "Protect recognition through consistency.");
  addShape(s, "do-side", 64, 170, 544, 450, C.mist, { radius: 24 });
  addShape(s, "dont-side", 636, 170, 544, 450, "#FBF3F4", { radius: 24 });
  addText(s, "do-label", "DO", 92, 194, 100, 34, { size: 26, bold: true, color: C.success });
  addText(s, "dont-label", "DON'T", 664, 194, 140, 34, { size: 26, bold: true, color: C.danger });
  await addImage(s, "Correct JusPrin logo", LOGO_PNG, 108, 248, 150, 150, { fit: "contain" });
  addText(s, "do-copy", "Use the original artwork\nPreserve aspect ratio\nMaintain clear space\nUse approved backgrounds\nUse monochrome only when required", 290, 246, 282, 180, { size: 19, color: C.ink, lineSpacing: 1.48 });
  addShape(s, "busy-a", 674, 252, 214, 16, C.bloom, { rotation: 18 });
  addShape(s, "busy-b", 674, 296, 214, 16, C.orbit, { rotation: -18 });
  addShape(s, "busy-c", 674, 340, 214, 16, C.violet, { rotation: 18 });
  await addImage(s, "Incorrect stretched logo", LOGO_PNG, 710, 267, 218, 104, { fit: "fill" });
  addText(s, "dont-copy", "Do not stretch or rotate\nDo not recolor arbitrarily\nDo not add shadows or outlines\nDo not place over busy imagery\nDo not separate mark components", 934, 246, 220, 188, { size: 18, color: C.ink, lineSpacing: 1.48 });
  addLine(s, "dont-strike", 690, 309, 250, 5, C.danger, 0, -20);
  addText(s, "test-rule", "Quick test: if the central crossing or orbit arcs lose clarity, increase size, contrast, or surrounding space.", 92, 544, 1010, 42, { size: 18, color: C.muted });
  addFooter(s, 9);
}

// 10 - UI application
{
  const s = presentation.slides.add();
  s.background.fill = C.mist;
  addHeader(s, 10, "Application / Product UI", "Brand guides the workflow.");
  addShape(s, "ui-window", 64, 170, 1118, 458, C.white, { radius: 22, shadow: "shadow-sm", line: { style: "solid", fill: C.trace, width: 1 } });
  addShape(s, "ui-topbar", 64, 170, 1118, 58, C.ink, { radius: 22 });
  addShape(s, "ui-topbar-mask", 64, 206, 1118, 22, C.ink);
  await addImage(s, "JusPrin UI logo", whiteSvgPath, 86, 180, 38, 38, { fit: "contain" });
  addText(s, "ui-name", "JusPrin", 136, 184, 150, 28, { size: 19, bold: true, color: C.white });
  addText(s, "ui-tabs", "PREPARE     PREVIEW     DEVICE", 430, 187, 420, 24, { size: 13, bold: true, color: "#CFC5D3", typeface: MONO, align: "center" });
  addShape(s, "ui-sidebar", 64, 228, 224, 400, C.mist);
  addText(s, "ui-project", "PROJECT", 90, 256, 140, 20, { size: 12, bold: true, color: C.violet, typeface: MONO });
  addText(s, "ui-file", "bracket_v4.stl", 90, 292, 160, 28, { size: 17, bold: true });
  addText(s, "ui-settings", "QUALITY\n0.20 mm Standard\n\nMATERIAL\nPLA / Violet\n\nSUPPORT\nBuild plate only", 90, 348, 170, 190, { size: 15, color: C.muted, lineSpacing: 1.35 });
  addShape(s, "build-plate", 350, 274, 492, 280, C.mist, { radius: 20, line: { style: "solid", fill: C.trace, width: 1 } });
  orbitMotif(s, 492, 314, 192, C.trace);
  addShape(s, "model", 520, 364, 150, 110, C.violet, { geometry: "hexagon", rotation: 0 });
  addText(s, "ui-ready", "READY TO PRINT", 902, 272, 220, 20, { size: 12, bold: true, color: C.success, typeface: MONO });
  addText(s, "ui-time", "1h 42m", 900, 314, 220, 52, { size: 42, bold: true, color: C.ink });
  addText(s, "ui-meta", "32.4 g material\n0.18 mm first layer", 902, 376, 210, 60, { size: 16, color: C.muted, lineSpacing: 1.3 });
  addShape(s, "ui-cta", 900, 478, 220, 62, C.orbit, { radius: 16 });
  addText(s, "ui-cta-text", "Slice & print", 900, 494, 220, 28, { size: 20, bold: true, color: C.white, align: "center" });
  addText(s, "ui-guidance", "Use Orbit for decisive actions, Mist for working surfaces, and Bloom for small moments of emphasis.", 350, 578, 772, 32, { size: 16, color: C.muted });
  addFooter(s, 10);
}

// 11 - Marketing applications
{
  const s = presentation.slides.add();
  s.background.fill = C.white;
  addHeader(s, 11, "Application / Marketing", "Clarity leads. The mark creates motion.");
  addShape(s, "landing", 64, 172, 706, 446, C.mist, { radius: 22, shadow: "shadow-sm" });
  await addImage(s, "JusPrin landing lockup", LOCKUP_PNG, 94, 194, 270, 82, { fit: "contain" });
  addText(s, "landing-headline", "From model to\nfirst layer, faster.", 96, 300, 500, 112, { size: 46, bold: true, color: C.ink, lineSpacing: 1.0 });
  addText(s, "landing-body", "A focused printing workflow that keeps advanced controls close and the next action obvious.", 98, 438, 500, 72, { size: 19, color: C.muted, lineSpacing: 1.25 });
  addShape(s, "landing-cta", 98, 542, 188, 52, C.orbit, { radius: 14 });
  addText(s, "landing-cta-text", "Start printing", 98, 556, 188, 25, { size: 17, bold: true, color: C.white, align: "center" });
  orbitMotif(s, 556, 370, 170, C.trace);
  addShape(s, "social", 812, 172, 368, 446, C.ink, { radius: 22 });
  await addImage(s, "JusPrin social mark", whiteSvgPath, 844, 202, 62, 62, { fit: "contain" });
  addText(s, "social-name", "JusPrin", 920, 216, 190, 30, { size: 22, bold: true, color: C.white });
  addText(s, "social-kicker", "PRINT WITH INTENT", 844, 314, 280, 22, { size: 13, bold: true, color: C.bloom, typeface: MONO });
  addText(s, "social-headline", "Fewer detours.\nMore making.", 844, 352, 290, 110, { size: 38, bold: true, color: C.white, lineSpacing: 1.03 });
  addText(s, "social-body", "A clearer way to move from setup to a confident first layer.", 844, 492, 278, 66, { size: 17, color: "#CFC5D3", lineSpacing: 1.25 });
  addLine(s, "social-accent", 844, 580, 118, 8, C.bloom, 0, 0);
  addFooter(s, 11);
}

// 12 - Quick reference
{
  const s = presentation.slides.add();
  s.background.fill = C.orbit;
  addHeader(s, 12, "Quick reference", "Keep JusPrin focused and unmistakable.", { dark: true });
  const rules = [
    ["01", "Anchor with the original mark", "Never redraw or separate its parts."],
    ["02", "Use the purple spectrum with restraint", "Orbit leads; Bloom accents."],
    ["03", "Write JusPrin exactly this way", "Capital J and P, no space."],
    ["04", "Protect space and contrast", "Clarity wins over visual novelty."],
    ["05", "Make the next action obvious", "Brand behavior should feel as focused as the visuals."],
  ];
  rules.forEach((r, i) => {
    const y = 186 + i * 88;
    addText(s, `rule-num-${i}`, r[0], 68, y + 2, 55, 28, { size: 16, bold: true, color: C.bloom, typeface: MONO });
    addText(s, `rule-title-${i}`, r[1], 148, y, 460, 32, { size: 23, bold: true, color: C.white });
    addText(s, `rule-desc-${i}`, r[2], 650, y + 2, 440, 30, { size: 18, color: "#D9CFDD" });
    if (i < rules.length - 1) addShape(s, `rule-line-${i}`, 148, y + 57, 940, 1, "#654F78");
  });
  await addImage(s, "JusPrin closing mark", whiteSvgPath, 1050, 494, 130, 130, { fit: "contain" });
  addFooter(s, 12, true);
}

// Editable design tokens.
const tokens = {
  brand: "JusPrin",
  color: {
    orbit: C.orbit,
    violet: C.violet,
    bloom: C.bloom,
    ink: C.ink,
    mist: C.mist,
    trace: C.trace,
    white: C.white,
  },
  gradient: { from: C.orbit, to: C.bloom, angle: 0 },
  typography: {
    primary: "Avenir Next",
    technical: "Menlo",
    fallback: "Arial, sans-serif",
  },
  radius: { control: 12, panel: 20, feature: 24 },
  logo: { clearSpace: "25% of mark width", minDigitalMarkPx: 24, minDigitalLockupPx: 120 },
};
await fs.writeFile(path.join(OUTPUT, "JusPrin-brand-tokens.json"), JSON.stringify(tokens, null, 2) + "\n");
await fs.writeFile(path.join(OUTPUT, "JusPrin-brand-tokens.css"), `:root {\n  --jusprin-orbit: ${C.orbit};\n  --jusprin-violet: ${C.violet};\n  --jusprin-bloom: ${C.bloom};\n  --jusprin-ink: ${C.ink};\n  --jusprin-mist: ${C.mist};\n  --jusprin-trace: ${C.trace};\n  --jusprin-gradient: linear-gradient(90deg, ${C.orbit}, ${C.bloom});\n  --jusprin-font-sans: "Avenir Next", Arial, sans-serif;\n  --jusprin-font-mono: Menlo, monospace;\n  --jusprin-radius-control: 12px;\n  --jusprin-radius-panel: 20px;\n}\n`);

// QA previews and editable deck.
for (const [i, slide] of presentation.slides.items.entries()) {
  const stem = `slide-${String(i + 1).padStart(2, "0")}`;
  await writeBlob(path.join(WORK, "renders", `${stem}.png`), await presentation.export({ slide, format: "png", scale: 1 }));
  await fs.writeFile(path.join(WORK, "renders", `${stem}.layout.json`), await (await slide.export({ format: "layout" })).text());
}
await writeBlob(path.join(WORK, "montage.webp"), await presentation.export({ format: "webp", montage: true, scale: 1 }));
await fs.writeFile(path.join(WORK, "inspection.ndjson"), (await presentation.inspect({ kind: "slide,textbox,shape,image", maxChars: 50000 })).ndjson);
const pptx = await PresentationFile.exportPptx(presentation);
await pptx.save(path.join(OUTPUT, "JusPrin-Visual-Brand-Kit.pptx"));

console.log(`Built ${presentation.slides.items.length} slides.`);
