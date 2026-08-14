import fs from "node:fs/promises";
import path from "node:path";
import { Presentation, PresentationFile } from "@oai/artifact-tool";

const ROOT = "/Users/kenneth/Documents/Codex/2026-08-13/referenced-chatgpt-conversation-this-is-an";
const WORK = path.join(ROOT, "work/ui-design-system-v2");
const OUT = path.join(ROOT, "outputs");
const LOGO = path.join(ROOT, "work/brand-kit/resources/images/JusPrin.png");
const FONT = "HarmonyOS Sans SC";
const MONO = "Courier New";
const W = 1280;
const H = 720;

const P = {
  orbit: "#3A2D64", orbitHover: "#4B3A77", orbitPressed: "#2B214F",
  violet: "#6F4F84", bloom: "#CE84B7", bloomHover: "#D99AC5", bloomPressed: "#B76CA0",
  ink: "#17131F", darkSurface: "#211B2A", darkRaised: "#2B2336", darkBorder: "#4B3E57", darkBorderStrong: "#7D6A8D",
  white: "#FFFFFF", mist: "#F6F2F7", trace: "#E8DFEB", lightMuted: "#5F5767",
  darkMuted: "#CFC5D3", darkSubtle: "#A99EAE", selection: "#EDE7F2",
  success: "#2D6A55", danger: "#B44757", warning: "#D49A2A",
  successDark: "#58B894", dangerDark: "#F07A8E", warningDark: "#E9B960",
};

const deck = Presentation.create({ slideSize: { width: W, height: H } });

async function bytes(file) {
  const b = await fs.readFile(file);
  return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
}
async function writeBlob(file, blob) { await fs.writeFile(file, new Uint8Array(await blob.arrayBuffer())); }
function shape(slide, name, x, y, w, h, fill = "none", opts = {}) {
  return slide.shapes.add({
    geometry: opts.geometry || "rect", name,
    position: { left: x, top: y, width: w, height: h, rotation: opts.rotation || 0 },
    fill, line: opts.line || { style: "solid", fill: "none", width: 0 },
    borderRadius: opts.radius, shadow: opts.shadow,
  });
}
function text(slide, name, value, x, y, w, h, opts = {}) {
  const s = shape(slide, name, x, y, w, h, "none", { geometry: "textbox" });
  s.text = value;
  s.text.style = {
    typeface: opts.typeface || FONT, fontSize: opts.size || 18, bold: !!opts.bold,
    color: opts.color || P.ink, alignment: opts.align || "left",
    verticalAlignment: opts.valign || "top", lineSpacing: opts.lineSpacing || 1.08,
    autoFit: opts.autoFit || "shrinkText", wrap: "square",
    insets: { top: opts.padT || 0, right: opts.padR || 0, bottom: opts.padB || 0, left: opts.padL || 0 },
  };
  return s;
}
async function image(slide, name, file, x, y, w, h) {
  return slide.images.add({ blob: await bytes(file), contentType: "image/png", alt: name, fit: "contain", position: { left: x, top: y, width: w, height: h } });
}
function header(slide, page, eyebrow, title, dark = false) {
  text(slide, `eyebrow-${page}`, eyebrow.toUpperCase(), 64, 46, 500, 23, { size: 14, bold: true, color: dark ? P.bloom : P.violet, typeface: MONO });
  text(slide, `title-${page}`, title, 64, 78, 1135, 70, { size: 44, bold: true, color: dark ? P.white : P.ink, lineSpacing: 0.98 });
}
function footer(slide, page, dark = false) {
  const c = dark ? P.darkMuted : P.lightMuted;
  text(slide, `footer-${page}`, "JusPrin UI design system · agent-ready specification", 64, 684, 650, 18, { size: 11, color: c });
  text(slide, `page-${page}`, String(page).padStart(2, "0"), 1150, 684, 65, 18, { size: 11, color: c, typeface: MONO, align: "right" });
  slide.speakerNotes.textFrame.setText("[Sources]\n- JusPrin repository: C++17, wxWidgets UI components and bundled fonts.\n- JusPrin logo: user-provided local repository asset.\n[/Sources]");
}
function chip(slide, x, y, w, label, fill, fg, line = "none") {
  shape(slide, `chip-${label}-${x}-${y}`, x, y, w, 34, fill, { radius: 10, line: { style: "solid", fill: line, width: line === "none" ? 0 : 1 } });
  text(slide, `chip-text-${label}-${x}-${y}`, label, x, y + 8, w, 18, { size: 13, bold: true, color: fg, align: "center" });
}
function tokenRow(slide, y, token, light, dark, purpose, index) {
  const x = 64;
  if (index % 2) shape(slide, `token-bg-${index}`, x, y - 7, 1120, 44, "#FBF9FC", { radius: 7 });
  text(slide, `token-name-${index}`, token, x + 12, y, 290, 22, { size: 14, bold: true, typeface: MONO });
  shape(slide, `token-light-swatch-${index}`, x + 318, y + 1, 20, 20, light, { radius: 5, line: { style: "solid", fill: P.trace, width: 1 } });
  text(slide, `token-light-${index}`, light, x + 348, y, 135, 22, { size: 13, typeface: MONO });
  shape(slide, `token-dark-swatch-${index}`, x + 500, y + 1, 20, 20, dark, { radius: 5, line: { style: "solid", fill: P.darkBorderStrong, width: 1 } });
  text(slide, `token-dark-${index}`, dark, x + 530, y, 135, 22, { size: 13, typeface: MONO });
  text(slide, `token-purpose-${index}`, purpose, x + 690, y, 410, 24, { size: 14, color: P.lightMuted });
}
function button(slide, x, y, w, h, label, fill, fg, radius, border = "none", icon = false) {
  shape(slide, `button-${label}-${x}-${y}`, x, y, w, h, fill, { radius, line: { style: "solid", fill: border, width: border === "none" ? 0 : 1 } });
  text(slide, `button-text-${label}-${x}-${y}`, icon ? "↗  " + label : label, x, y + (h - 20) / 2, w, 20, { size: 14, bold: true, color: fg, align: "center" });
}
function orbit(slide, x, y, size, color) {
  shape(slide, `orbit-${x}-${y}`, x, y, size, size, "none", { geometry: "ellipse", line: { style: "solid", fill: color, width: 3 } });
  shape(slide, `orbit-a-${x}-${y}`, x + size * .12, y + size * .47, size * .76, size * .07, color, { radius: 3, rotation: -38 });
  shape(slide, `orbit-b-${x}-${y}`, x + size * .12, y + size * .47, size * .76, size * .07, color, { radius: 3, rotation: 38 });
}

await fs.mkdir(WORK, { recursive: true });
await fs.mkdir(OUT, { recursive: true });
await fs.mkdir(path.join(WORK, "renders"), { recursive: true });

// 01 Cover
{
  const s = deck.slides.add(); s.background.fill = P.mist;
  shape(s, "cover-orbit", 830, -120, 560, 560, "none", { geometry: "ellipse", line: { style: "solid", fill: P.trace, width: 4 } });
  shape(s, "cover-slash", 930, 150, 390, 44, P.trace, { radius: 8, rotation: -38 });
  text(s, "cover-kicker", "UI DESIGN SYSTEM / 2026", 66, 58, 500, 24, { size: 14, bold: true, color: P.violet, typeface: MONO });
  await image(s, "JusPrin mark", LOGO, 72, 174, 170, 170);
  text(s, "cover-name", "JusPrin", 260, 210, 560, 84, { size: 66, bold: true, color: P.orbit });
  text(s, "cover-title", "One executable visual language\nfor light and dark desktop UI.", 68, 392, 820, 128, { size: 40, bold: true, lineSpacing: 1.03 });
  text(s, "cover-sub", "Semantic tokens, native component rules, accessibility states, and design-agent instructions.", 68, 552, 800, 52, { size: 20, color: P.lightMuted, lineSpacing: 1.2 });
  chip(s, 956, 504, 190, "WINDOWS · MAC · LINUX", P.ink, P.white);
  footer(s, 1);
}

// 02 Operating contract
{
  const s = deck.slides.add(); s.background.fill = P.white;
  header(s, 2, "How to use this guide", "The brand palette inspires; semantic tokens decide.");
  text(s, "contract-intro", "A design agent may use primitives to communicate the JusPrin brand, but every product-interface decision must resolve to a named semantic or component token.", 64, 170, 1110, 72, { size: 23, lineSpacing: 1.25 });
  const cols = [64, 438, 812];
  const items = [
    ["01", "Start with mode", "Create paired light and dark frames. Never recolor a finished light design by eye."],
    ["02", "Name the intent", "Choose surface, text, action, border, status, or focus tokens - never a raw purple."],
    ["03", "Fit the native app", "Use the existing wxWidgets density, DIP scaling, keyboard focus, and 4/8/12 radii."],
  ];
  items.forEach((a, i) => {
    text(s, `contract-num-${i}`, a[0], cols[i], 300, 60, 28, { size: 15, bold: true, color: P.bloom, typeface: MONO });
    text(s, `contract-title-${i}`, a[1], cols[i], 344, 310, 38, { size: 25, bold: true, color: P.orbit });
    text(s, `contract-copy-${i}`, a[2], cols[i], 398, 310, 112, { size: 18, color: P.lightMuted, lineSpacing: 1.28 });
  });
  shape(s, "contract-rule", 64, 560, 1120, 2, P.trace);
  text(s, "contract-test", "Output rule: every high-fidelity screen must identify its mode, viewport, semantic tokens, component variants, focus state, and any deliberate exception.", 64, 590, 1120, 48, { size: 19, bold: true, color: P.violet });
  footer(s, 2);
}

// 03 platform constraints
{
  const s = deck.slides.add(); s.background.fill = P.ink;
  header(s, 3, "Product constraints", "Design for a native, dense, high-DPI desktop application.", true);
  const cards = [
    ["PLATFORMS", "Windows x64 / ARM64\nmacOS Intel / Apple Silicon\nLinux x86_64 / ARM64"],
    ["UI STACK", "C++17 · wxWidgets · OpenGL\nEmbedded WebView surfaces\nKeyboard-first desktop patterns"],
    ["SCALE", "All dimensions are DIP\nVerify 100%, 150%, and 200%\nSVG icons with PNG fallback"],
  ];
  cards.forEach((c, i) => {
    const x = 64 + i * 374;
    shape(s, `constraint-card-${i}`, x, 190, 336, 290, i === 1 ? P.darkRaised : P.darkSurface, { radius: 12, line: { style: "solid", fill: P.darkBorder, width: 1 } });
    text(s, `constraint-label-${i}`, c[0], x + 24, 220, 290, 22, { size: 13, bold: true, color: P.bloom, typeface: MONO });
    text(s, `constraint-copy-${i}`, c[1], x + 24, 270, 290, 150, { size: 21, color: P.white, lineSpacing: 1.35 });
  });
  text(s, "constraint-no", "Avoid web-only assumptions: oversized controls, glass effects, blur-heavy surfaces, fixed physical pixels, and platform-specific fonts.", 64, 540, 1110, 62, { size: 21, bold: true, color: P.darkMuted, lineSpacing: 1.22 });
  footer(s, 3, true);
}

// 04 primitives
{
  const s = deck.slides.add(); s.background.fill = P.mist;
  header(s, 4, "Color primitives", "A compact palette supplies both themes.");
  const swatches = [
    ["Orbit", P.orbit, "brand.orbit", P.white], ["Violet", P.violet, "brand.violet", P.white], ["Bloom", P.bloom, "brand.bloom", P.ink],
    ["Ink", P.ink, "neutral.ink", P.white], ["Mist", P.mist, "neutral.mist", P.ink], ["Trace", P.trace, "neutral.trace", P.ink],
    ["Success", P.success, "status.success", P.white], ["Danger", P.danger, "status.danger", P.white], ["Warning", P.warning, "status.warning", P.ink],
  ];
  swatches.forEach((a, i) => {
    const x = 64 + (i % 3) * 374;
    const y = 176 + Math.floor(i / 3) * 146;
    shape(s, `primitive-${i}`, x, y, 336, 116, a[1], { radius: 12, line: { style: "solid", fill: a[1] === P.mist ? P.trace : "none", width: a[1] === P.mist ? 1 : 0 } });
    text(s, `primitive-name-${i}`, a[0], x + 18, y + 17, 145, 30, { size: 22, bold: true, color: a[3] });
    text(s, `primitive-hex-${i}`, a[1], x + 178, y + 21, 140, 22, { size: 13, color: a[3], typeface: MONO, align: "right" });
    text(s, `primitive-token-${i}`, a[2], x + 18, y + 76, 300, 20, { size: 13, color: a[3], typeface: MONO });
  });
  footer(s, 4);
}

// 05 semantic mapping
{
  const s = deck.slides.add(); s.background.fill = P.white;
  header(s, 5, "Semantic color API", "One token name carries the same intent in both modes.");
  text(s, "semantic-head-a", "TOKEN", 76, 166, 250, 20, { size: 12, bold: true, color: P.violet, typeface: MONO });
  text(s, "semantic-head-b", "LIGHT", 382, 166, 130, 20, { size: 12, bold: true, color: P.violet, typeface: MONO });
  text(s, "semantic-head-c", "DARK", 566, 166, 130, 20, { size: 12, bold: true, color: P.violet, typeface: MONO });
  text(s, "semantic-head-d", "PURPOSE", 754, 166, 320, 20, { size: 12, bold: true, color: P.violet, typeface: MONO });
  const rows = [
    ["surface.canvas", P.white, P.ink, "Primary application background"],
    ["surface.subtle", P.mist, P.darkSurface, "Sidebars and grouped settings"],
    ["surface.raised", P.white, P.darkRaised, "Dialogs, menus, elevated panels"],
    ["text.primary", P.ink, P.mist, "Headings, values, body copy"],
    ["text.secondary", P.lightMuted, P.darkMuted, "Descriptions and secondary labels"],
    ["border.subtle", P.trace, P.darkBorder, "Dividers and quiet structure"],
    ["border.strong", P.violet, P.darkBorderStrong, "Focus and essential boundaries"],
    ["action.primary", P.orbit, P.bloom, "Decisive primary action"],
    ["action.primary.hover", P.orbitHover, P.bloomHover, "Pointer hover"],
    ["action.primary.pressed", P.orbitPressed, P.bloomPressed, "Pressed / active"],
  ];
  rows.forEach((r, i) => tokenRow(s, 205 + i * 42, ...r, i));
  footer(s, 5);
}

// 06 paired modes
{
  const s = deck.slides.add(); s.background.fill = P.mist;
  header(s, 6, "Light and dark modes", "Dark mode is a remapping, not an inversion.");
  shape(s, "light-frame", 64, 182, 536, 414, P.white, { radius: 12, line: { style: "solid", fill: P.trace, width: 1 }, shadow: "shadow-sm" });
  shape(s, "dark-frame", 632, 182, 536, 414, P.ink, { radius: 12, line: { style: "solid", fill: P.darkBorder, width: 1 }, shadow: "shadow-sm" });
  text(s, "light-label", "LIGHT", 88, 205, 100, 20, { size: 12, bold: true, color: P.violet, typeface: MONO });
  text(s, "dark-label", "DARK", 656, 205, 100, 20, { size: 12, bold: true, color: P.bloom, typeface: MONO });
  [0, 1].forEach(i => {
    const ox = i ? 632 : 64; const dark = !!i;
    shape(s, `mode-sidebar-${i}`, ox, 242, 150, 354, dark ? P.darkSurface : P.mist);
    text(s, `mode-nav-${i}`, "PREPARE\n\nPREVIEW\n\nDEVICE", ox + 24, 272, 110, 160, { size: 14, bold: true, color: dark ? P.darkMuted : P.lightMuted, lineSpacing: 1.3 });
    text(s, `mode-title-${i}`, "Print settings", ox + 178, 260, 300, 38, { size: 25, bold: true, color: dark ? P.mist : P.ink });
    text(s, `mode-copy-${i}`, "Quality\n0.20 mm Standard\n\nMaterial\nPLA / Violet", ox + 178, 320, 260, 130, { size: 16, color: dark ? P.darkMuted : P.lightMuted, lineSpacing: 1.35 });
    button(s, ox + 318, 510, 172, 42, "Slice & print", dark ? P.bloom : P.orbit, dark ? P.ink : P.white, 8);
  });
  text(s, "mode-note", "Use lighter brand color on dark surfaces; preserve hierarchy, density, and component geometry.", 64, 626, 1100, 32, { size: 18, bold: true, color: P.violet });
  footer(s, 6);
}

// 07 contrast
{
  const s = deck.slides.add(); s.background.fill = P.white;
  header(s, 7, "Accessibility", "Contrast and visible states are part of the specification.");
  const metrics = [
    ["12.07:1", "Orbit / White", "Primary light button"], ["6.62:1", "Bloom / Ink", "Primary dark button"], ["6.89:1", "Secondary / White", "Light secondary text"], ["10.95:1", "Dark secondary / Ink", "Dark secondary text"],
  ];
  metrics.forEach((m, i) => {
    const x = 64 + i * 280;
    text(s, `metric-${i}`, m[0], x, 196, 230, 52, { size: 38, bold: true, color: i % 2 ? P.bloomPressed : P.orbit });
    text(s, `metric-label-${i}`, m[1], x, 260, 230, 24, { size: 16, bold: true });
    text(s, `metric-use-${i}`, m[2], x, 292, 230, 46, { size: 15, color: P.lightMuted });
  });
  shape(s, "access-rule", 64, 370, 1120, 2, P.trace);
  const rules = [
    ["Text", "4.5:1 minimum for normal text; 3:1 only for large text."],
    ["Controls", "3:1 for essential boundaries and state indicators."],
    ["Focus", "2 DIP visible ring; never communicate focus with color shift alone."],
    ["Status", "Pair color with an icon and plain-language label."],
  ];
  rules.forEach((r, i) => {
    const x = 64 + (i % 2) * 560; const y = 410 + Math.floor(i / 2) * 100;
    text(s, `access-title-${i}`, r[0], x, y, 120, 30, { size: 22, bold: true, color: P.violet });
    text(s, `access-copy-${i}`, r[1], x + 126, y, 400, 58, { size: 17, color: P.lightMuted, lineSpacing: 1.22 });
  });
  footer(s, 7);
}

// 08 typography
{
  const s = deck.slides.add(); s.background.fill = P.mist;
  header(s, 8, "Typography", "Use the fonts JusPrin already ships on every platform.");
  text(s, "font-name", "HarmonyOS Sans SC", 64, 174, 660, 74, { size: 52, bold: true, color: P.orbit });
  text(s, "font-desc", "Regular for UI and body · Bold for hierarchy and actions\nKorean: NanumGothic · Thai: Sarabun · fallback: system GUI font", 66, 256, 700, 70, { size: 18, color: P.lightMuted, lineSpacing: 1.3 });
  text(s, "mono-name", "System teletype  0.20 mm  215 C", 760, 196, 430, 36, { size: 20, color: P.violet, typeface: MONO });
  text(s, "mono-desc", "Technical values only. Never require Menlo or another platform-specific face.", 760, 252, 400, 60, { size: 17, color: P.lightMuted, lineSpacing: 1.2 });
  shape(s, "type-rule", 64, 350, 1120, 2, P.trace);
  const specs = [
    ["Page title", "24 / 30", "Bold", "Prepare"], ["Section", "18 / 24", "Bold", "Print settings"], ["Body / control", "14 / 20", "Regular", "0.20 mm Standard"], ["Label", "12 / 16", "Bold", "QUALITY"], ["Dense metadata", "10 / 14", "Regular", "Last updated 14:32"],
  ];
  specs.forEach((p, i) => {
    const y = 382 + i * 49;
    text(s, `type-role-${i}`, p[0], 64, y, 190, 25, { size: 15, bold: true });
    text(s, `type-size-${i}`, p[1], 270, y, 110, 25, { size: 13, color: P.violet, typeface: MONO });
    text(s, `type-weight-${i}`, p[2], 400, y, 110, 25, { size: 15, color: P.lightMuted });
    text(s, `type-sample-${i}`, p[3], 540, y - 2, 600, 30, { size: i === 0 ? 23 : i === 1 ? 18 : i === 4 ? 12 : 14, bold: i < 2, color: P.ink });
  });
  footer(s, 8);
}

// 09 geometry
{
  const s = deck.slides.add(); s.background.fill = P.white;
  header(s, 9, "Layout and geometry", "A small DIP scale keeps the interface consistent at every density.");
  text(s, "dip-rule", "DIP, not physical pixels", 64, 174, 500, 34, { size: 25, bold: true, color: P.orbit });
  text(s, "dip-copy", "Use wxWidgets device-independent dimensions. The platform renderer handles physical scaling.", 64, 218, 520, 60, { size: 18, color: P.lightMuted, lineSpacing: 1.25 });
  const spaces = [["4",4],["8",8],["12",12],["16",16],["20",20],["24",24],["32",32],["40",40],["48",48]];
  spaces.forEach((a, i) => {
    const x = 64 + i * 119;
    shape(s, `space-${i}`, x, 320, Math.max(8, a[1] * 1.5), 22, P.violet, { radius: 4 });
    text(s, `space-label-${i}`, a[0], x, 354, 70, 20, { size: 13, typeface: MONO, color: P.lightMuted });
  });
  shape(s, "geometry-rule", 64, 406, 1120, 2, P.trace);
  const radii = [["4 DIP", "Inputs, parameter, choice, icon"], ["8 DIP", "Compact and branded actions"], ["12 DIP", "Window actions and large dialogs"]];
  radii.forEach((r, i) => {
    const x = 64 + i * 374;
    shape(s, `radius-${i}`, x, 446, 110, 84, i === 1 ? P.violet : P.mist, { radius: [4,8,12][i], line: { style: "solid", fill: P.trace, width: 1 } });
    text(s, `radius-name-${i}`, r[0], x + 136, 448, 190, 28, { size: 21, bold: true, color: P.orbit });
    text(s, `radius-use-${i}`, r[1], x + 136, 486, 190, 60, { size: 15, color: P.lightMuted, lineSpacing: 1.2 });
  });
  text(s, "geometry-no", "16-24 DIP radii are reserved for onboarding and marketing surfaces, not the standard application shell.", 64, 594, 1100, 38, { size: 18, bold: true, color: P.violet });
  footer(s, 9);
}

// 10 icons
{
  const s = deck.slides.add(); s.background.fill = P.ink;
  header(s, 10, "Icons and brand shapes", "Functional clarity comes before logo-derived decoration.", true);
  orbit(s, 104, 196, 180, P.bloom);
  shape(s, "icon-diagonal-stage", 438, 220, 220, 138, P.orbit, { radius: 12 });
  shape(s, "icon-diagonal", 454, 279, 190, 18, P.bloom, { radius: 5, rotation: -28 });
  shape(s, "icon-size-stage", 816, 196, 324, 180, P.darkSurface, { radius: 12, line: { style: "solid", fill: P.darkBorder, width: 1 } });
  [16,20,24].forEach((n,i) => {
    shape(s, `icon-size-${n}`, 850 + i*92, 250 + (24-n)/2, n*2, n*2, i===1 ? P.bloom : P.darkBorderStrong, { radius: 4 });
    text(s, `icon-size-label-${n}`, `${n} DIP`, 836+i*96, 318, 74, 18, { size: 12, color: P.darkMuted, typeface: MONO, align: "center" });
  });
  const cols=[64,438,816]; const titles=["Orbit motif","Directional accent","Product icons"];
  const desc=["Progress, connection, active workflow. Keep strokes visible at output size.","One diagonal for momentum. Avoid competing angles and decorative clutter.","Use the established SVG library at 16, 20, or 24 DIP. Supply PNG fallback."];
  titles.forEach((t,i)=>{ text(s,`icon-title-${i}`,t,cols[i],430,300,30,{size:24,bold:true,color:P.white}); text(s,`icon-copy-${i}`,desc[i],cols[i],474,320,90,{size:17,color:P.darkMuted,lineSpacing:1.25}); });
  footer(s, 10, true);
}

// 11 buttons
{
  const s = deck.slides.add(); s.background.fill = P.mist;
  header(s, 11, "Buttons", "Apply the brand through existing semantic variants and states.");
  text(s, "button-light-label", "LIGHT", 64, 170, 120, 20, { size: 12, bold: true, color: P.violet, typeface: MONO });
  button(s, 64, 210, 180, 40, "Slice & print", P.orbit, P.white, 8);
  button(s, 264, 210, 150, 40, "Cancel", P.white, P.ink, 4, P.trace);
  button(s, 434, 210, 150, 40, "Delete", P.white, P.danger, 4, P.danger);
  button(s, 604, 210, 140, 40, "Disabled", "#E4E0E6", "#8A838F", 4);
  shape(s, "focus-outer-light", 762, 204, 190, 52, "none", { radius: 10, line: { style: "solid", fill: P.violet, width: 2 } });
  button(s, 768, 210, 178, 40, "Focused", P.orbit, P.white, 8);
  text(s, "button-dark-label", "DARK", 64, 296, 120, 20, { size: 12, bold: true, color: P.bloom, typeface: MONO });
  shape(s, "button-dark-stage", 64, 328, 888, 82, P.ink, { radius: 12 });
  button(s, 88, 349, 180, 40, "Slice & print", P.bloom, P.ink, 8);
  button(s, 288, 349, 150, 40, "Cancel", P.darkSurface, P.mist, 4, P.darkBorderStrong);
  button(s, 458, 349, 150, 40, "Delete", P.darkSurface, P.dangerDark, 4, P.dangerDark);
  shape(s, "focus-outer-dark", 630, 343, 190, 52, "none", { radius: 10, line: { style: "solid", fill: P.darkBorderStrong, width: 2 } });
  button(s, 636, 349, 178, 40, "Focused", P.bloom, P.ink, 8);
  shape(s, "button-rule", 64, 452, 1120, 2, P.trace);
  const specs = [["Compact","padding 8×3 · radius 8 · body 10"],["Window","58×24 min · radius 12 · body 12"],["Choice","100×32 min · padding 12×8 · radius 4"],["Parameter","120×26 · radius 4 · body 14"],["Icon","26×26 · icon 16 · radius 4"],["Expanded","32 high min · padding 12×8 · radius 4"]];
  specs.forEach((a,i)=>{const x=64+(i%3)*374;const y=490+Math.floor(i/3)*74;text(s,`button-spec-title-${i}`,a[0],x,y,120,24,{size:17,bold:true,color:P.orbit});text(s,`button-spec-copy-${i}`,a[1],x+124,y,200,44,{size:13,color:P.lightMuted,typeface:MONO,lineSpacing:1.2});});
  footer(s, 11);
}

// 12 other components
{
  const s = deck.slides.add(); s.background.fill = P.white;
  header(s, 12, "Core components", "Build high fidelity from a small, explicit native component set.");
  shape(s, "component-light", 64, 180, 536, 420, P.mist, { radius: 12, line: { style: "solid", fill: P.trace, width: 1 } });
  shape(s, "component-dark", 632, 180, 536, 420, P.ink, { radius: 12, line: { style: "solid", fill: P.darkBorder, width: 1 } });
  [0,1].forEach(i=>{
    const ox=i?632:64; const dark=!!i; const fg=dark?P.mist:P.ink; const muted=dark?P.darkMuted:P.lightMuted; const surf=dark?P.darkSurface:P.white; const border=dark?P.darkBorderStrong:P.trace;
    text(s,`component-mode-${i}`,dark?"DARK":"LIGHT",ox+24,204,80,18,{size:12,bold:true,color:dark?P.bloom:P.violet,typeface:MONO});
    text(s,`input-label-${i}`,"Printer name",ox+24,246,160,20,{size:13,bold:true,color:fg});
    shape(s,`input-${i}`,ox+24,272,300,38,surf,{radius:4,line:{style:"solid",fill:border,width:1}});
    text(s,`input-value-${i}`,"JusPrin Studio",ox+36,282,270,20,{size:14,color:fg});
    text(s,`select-label-${i}`,"Quality",ox+24,330,160,20,{size:13,bold:true,color:fg});
    shape(s,`select-${i}`,ox+24,356,300,38,surf,{radius:4,line:{style:"solid",fill:border,width:1}});
    text(s,`select-value-${i}`,"0.20 mm Standard                 ▾",ox+36,366,270,20,{size:14,color:fg});
    chip(s,ox+24,422,126,"READY",dark?P.successDark:P.success,dark?P.ink:P.white);
    chip(s,ox+166,422,126,"WARNING",dark?P.warningDark:P.warning,P.ink);
    chip(s,ox+308,422,126,"ERROR",dark?P.dangerDark:P.danger,dark?P.ink:P.white);
    shape(s,`panel-${i}`,ox+24,484,462,82,surf,{radius:8,line:{style:"solid",fill:border,width:1}});
    text(s,`panel-title-${i}`,"Support",ox+42,502,160,22,{size:15,bold:true,color:fg});
    text(s,`panel-copy-${i}`,"Build plate only",ox+42,532,220,20,{size:14,color:muted});
    shape(s,`toggle-track-${i}`,ox+394,510,56,28,dark?P.bloom:P.orbit,{radius:14});
    shape(s,`toggle-thumb-${i}`,ox+424,514,20,20,dark?P.ink:P.white,{geometry:"ellipse"});
  });
  footer(s, 12);
}

// 13 agent handoff
{
  const s = deck.slides.add(); s.background.fill = P.orbit;
  header(s, 13, "Design-agent handoff", "A valid JusPrin UI concept is traceable, paired, and implementation-aware.", true);
  const checks=[
    ["01","Mode pair","Light and dark frames use the same semantic token names."],
    ["02","Native density","Components use DIP, 4/8/12 radii, and existing control dimensions."],
    ["03","Typography","HarmonyOS Sans SC and system teletype only."],
    ["04","State coverage","Normal, hover, pressed, disabled, focus, error, and success are shown."],
    ["05","Real content","Test long filenames, localized labels, dense settings, and empty states."],
    ["06","Asset discipline","Original mark; existing SVG icons; gradient only in brand moments."],
  ];
  checks.forEach((a,i)=>{const y=174+i*76;text(s,`check-num-${i}`,a[0],68,y,50,24,{size:14,bold:true,color:P.bloom,typeface:MONO});text(s,`check-title-${i}`,a[1],142,y-2,230,30,{size:21,bold:true,color:P.white});text(s,`check-copy-${i}`,a[2],410,y,700,34,{size:17,color:"#DDD4E1"});if(i<5)shape(s,`check-rule-${i}`,142,y+49,970,1,"#654F78");});
  text(s,"handoff-rule","Use JusPrin-UI-design-tokens.json as the source of truth. If a proposed value is not tokenized, label it as an exception and explain why.",68,626,1070,38,{size:18,bold:true,color:P.white});
  footer(s, 13, true);
}

const tokens = {
  $schema: "https://design-tokens.github.io/community-group/format/",
  meta: { name: "JusPrin UI Design System", version: "1.0.0", units: "DIP", platforms: ["Windows", "macOS", "Linux"] },
  primitive: {
    brand: { orbit: P.orbit, orbitHover: P.orbitHover, orbitPressed: P.orbitPressed, violet: P.violet, bloom: P.bloom, bloomHover: P.bloomHover, bloomPressed: P.bloomPressed },
    neutral: { ink: P.ink, white: P.white, mist: P.mist, trace: P.trace, darkSurface: P.darkSurface, darkRaised: P.darkRaised, darkBorder: P.darkBorder, darkBorderStrong: P.darkBorderStrong },
    status: { success: P.success, danger: P.danger, warning: P.warning, successDark: P.successDark, dangerDark: P.dangerDark, warningDark: P.warningDark },
  },
  semantic: {
    light: {
      surface: { canvas: P.white, subtle: P.mist, raised: P.white, selected: P.selection },
      text: { primary: P.ink, secondary: P.lightMuted, onAction: P.white },
      border: { subtle: P.trace, strong: P.violet, focus: P.violet },
      action: {
        primary: P.orbit, primaryHover: P.orbitHover, primaryPressed: P.orbitPressed, primaryText: P.white,
        secondary: P.white, secondaryHover: P.mist, secondaryPressed: P.selection, secondaryText: P.ink, secondaryBorder: P.trace,
        danger: P.white, dangerText: P.danger, dangerBorder: P.danger,
        disabled: "#E4E0E6", disabledText: "#8A838F",
      },
      status: { success: P.success, danger: P.danger, warning: P.warning },
    },
    dark: {
      surface: { canvas: P.ink, subtle: P.darkSurface, raised: P.darkRaised, selected: P.darkBorder },
      text: { primary: P.mist, secondary: P.darkMuted, tertiary: P.darkSubtle, onAction: P.ink },
      border: { subtle: P.darkBorder, strong: P.darkBorderStrong, focus: P.darkBorderStrong },
      action: {
        primary: P.bloom, primaryHover: P.bloomHover, primaryPressed: P.bloomPressed, primaryText: P.ink,
        secondary: P.darkSurface, secondaryHover: P.darkRaised, secondaryPressed: "#3A3047", secondaryText: P.mist, secondaryBorder: P.darkBorderStrong,
        danger: P.darkSurface, dangerText: P.dangerDark, dangerBorder: P.dangerDark,
        disabled: "#342C3B", disabledText: "#857B8C",
      },
      status: { success: P.successDark, danger: P.dangerDark, warning: P.warningDark },
    },
  },
  typography: {
    ui: { family: "HarmonyOS Sans SC", weights: { regular: 400, bold: 700 }, fallback: "system GUI font" },
    technical: { family: "system teletype", usage: "measurements, temperatures, filenames, machine status" },
    roles: { pageTitle: { size: 24, lineHeight: 30, weight: 700 }, section: { size: 18, lineHeight: 24, weight: 700 }, body: { size: 14, lineHeight: 20, weight: 400 }, label: { size: 12, lineHeight: 16, weight: 700 }, metadata: { size: 10, lineHeight: 14, weight: 400 } },
  },
  dimension: { space: { 1: 4, 2: 8, 3: 12, 4: 16, 5: 20, 6: 24, 8: 32, 10: 40, 12: 48 }, radius: { standard: 4, compact: 8, window: 12 } },
  component: {
    button: {
      compact: { paddingX: 8, paddingY: 3, radius: 8, textRole: "metadata" },
      window: { minWidth: 58, height: 24, radius: 12, textSize: 12 },
      choice: { minWidth: 100, height: 32, paddingX: 12, paddingY: 8, radius: 4, textSize: 14 },
      parameter: { width: 120, height: 26, radius: 4, textSize: 14 },
      icon: { width: 26, height: 26, iconSize: 16, radius: 4 },
      expanded: { minHeight: 32, paddingX: 12, paddingY: 8, radius: 4, textSize: 14 },
    },
    icon: { sizes: [16, 20, 24], preferredFormat: "SVG", fallbackFormat: "PNG" },
    focus: { width: 2, offset: 2, rule: "Must remain visible without relying on fill-color change alone" },
  },
  rules: {
    gradient: "Logo, splash, onboarding, and rare marketing moments only",
    measurement: "All product UI dimensions are DIP",
    modePair: "Every high-fidelity product frame must be delivered in light and dark mode",
    rawColor: "Do not use primitive colors directly in product UI; resolve through semantic tokens",
  },
};

const css = `:root {\n  --jp-surface-canvas: ${P.white};\n  --jp-surface-subtle: ${P.mist};\n  --jp-surface-raised: ${P.white};\n  --jp-text-primary: ${P.ink};\n  --jp-text-secondary: ${P.lightMuted};\n  --jp-border-subtle: ${P.trace};\n  --jp-border-strong: ${P.violet};\n  --jp-action-primary: ${P.orbit};\n  --jp-action-primary-hover: ${P.orbitHover};\n  --jp-action-primary-pressed: ${P.orbitPressed};\n  --jp-action-primary-text: ${P.white};\n  --jp-action-secondary: ${P.white};\n  --jp-action-secondary-hover: ${P.mist};\n  --jp-action-secondary-pressed: ${P.selection};\n  --jp-action-secondary-text: ${P.ink};\n  --jp-action-secondary-border: ${P.trace};\n  --jp-action-danger: ${P.white};\n  --jp-action-danger-text: ${P.danger};\n  --jp-action-danger-border: ${P.danger};\n  --jp-action-disabled: #E4E0E6;\n  --jp-action-disabled-text: #8A838F;\n  --jp-status-success: ${P.success};\n  --jp-status-danger: ${P.danger};\n  --jp-status-warning: ${P.warning};\n}\n[data-theme="dark"] {\n  --jp-surface-canvas: ${P.ink};\n  --jp-surface-subtle: ${P.darkSurface};\n  --jp-surface-raised: ${P.darkRaised};\n  --jp-text-primary: ${P.mist};\n  --jp-text-secondary: ${P.darkMuted};\n  --jp-border-subtle: ${P.darkBorder};\n  --jp-border-strong: ${P.darkBorderStrong};\n  --jp-action-primary: ${P.bloom};\n  --jp-action-primary-hover: ${P.bloomHover};\n  --jp-action-primary-pressed: ${P.bloomPressed};\n  --jp-action-primary-text: ${P.ink};\n  --jp-action-secondary: ${P.darkSurface};\n  --jp-action-secondary-hover: ${P.darkRaised};\n  --jp-action-secondary-pressed: #3A3047;\n  --jp-action-secondary-text: ${P.mist};\n  --jp-action-secondary-border: ${P.darkBorderStrong};\n  --jp-action-danger: ${P.darkSurface};\n  --jp-action-danger-text: ${P.dangerDark};\n  --jp-action-danger-border: ${P.dangerDark};\n  --jp-action-disabled: #342C3B;\n  --jp-action-disabled-text: #857B8C;\n  --jp-status-success: ${P.successDark};\n  --jp-status-danger: ${P.dangerDark};\n  --jp-status-warning: ${P.warningDark};\n}\n`;

const md = `# JusPrin UI Design System\n\nThis is the implementation-facing companion to the visual brand kit. The brand palette defines identity; these semantic tokens define product UI.\n\n## Design-agent contract\n\n1. Produce paired light and dark frames.\n2. Use semantic token names rather than raw hex values.\n3. Use HarmonyOS Sans SC for UI and the platform system teletype font for technical values.\n4. Specify all dimensions in DIP.\n5. Reuse the existing native component density and the 4/8/12 DIP radius scale.\n6. Show normal, hover, pressed, disabled, focused, success, warning, and error states where relevant.\n7. Use the original logo and established SVG icon library. Reserve the gradient for brand surfaces.\n\n## Semantic color tokens\n\n| Token | Light | Dark | Use |\n|---|---:|---:|---|\n| surface.canvas | ${P.white} | ${P.ink} | Application background |\n| surface.subtle | ${P.mist} | ${P.darkSurface} | Sidebars and groups |\n| surface.raised | ${P.white} | ${P.darkRaised} | Dialogs and menus |\n| text.primary | ${P.ink} | ${P.mist} | Primary text |\n| text.secondary | ${P.lightMuted} | ${P.darkMuted} | Secondary text |\n| border.subtle | ${P.trace} | ${P.darkBorder} | Dividers |\n| border.strong | ${P.violet} | ${P.darkBorderStrong} | Focus and essential boundaries |\n| action.primary | ${P.orbit} | ${P.bloom} | Primary action |\n| action.primary.hover | ${P.orbitHover} | ${P.bloomHover} | Hover |\n| action.primary.pressed | ${P.orbitPressed} | ${P.bloomPressed} | Pressed |\n\n## Typography\n\n- Page title: 24/30 DIP, Bold\n- Section: 18/24 DIP, Bold\n- Body and controls: 14/20 DIP, Regular\n- Label: 12/16 DIP, Bold\n- Dense metadata: 10/14 DIP, Regular\n\n## Geometry\n\nSpacing: 4, 8, 12, 16, 20, 24, 32, 40, 48 DIP. Standard radii: 4 DIP; compact/branded: 8 DIP; window actions: 12 DIP. Larger radii are limited to onboarding and marketing.\n\n## High-fidelity delivery checklist\n\nInclude mode, viewport, token references, component variants, keyboard focus, long/localized content, status states, and any explicit exceptions.\n`;

await fs.writeFile(path.join(OUT, "JusPrin-UI-design-tokens.json"), JSON.stringify(tokens, null, 2) + "\n");
await fs.writeFile(path.join(OUT, "JusPrin-UI-design-tokens.css"), css);
await fs.writeFile(path.join(OUT, "JusPrin-UI-Design-System.md"), md);

for (const [i, slide] of deck.slides.items.entries()) {
  const stem = `slide-${String(i + 1).padStart(2, "0")}`;
  await writeBlob(path.join(WORK, "renders", `${stem}.png`), await deck.export({ slide, format: "png", scale: 1 }));
  await fs.writeFile(path.join(WORK, "renders", `${stem}.layout.json`), await (await slide.export({ format: "layout" })).text());
}
await writeBlob(path.join(WORK, "montage.webp"), await deck.export({ format: "webp", montage: true, scale: 1 }));
await fs.writeFile(path.join(WORK, "inspection.ndjson"), (await deck.inspect({ kind: "slide,textbox,shape,image", maxChars: 80000 })).ndjson);
const pptx = await PresentationFile.exportPptx(deck);
await pptx.save(path.join(OUT, "JusPrin-UI-Design-System.pptx"));
console.log(`Built ${deck.slides.items.length} slides.`);
