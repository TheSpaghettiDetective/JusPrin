// The masks in styles.css use the same pinned Lucide SVGs as the native shell.
// Keeping them in CSS lets Vite inline the artwork into the offline WebView.
export function PrinterGlyph() {
  return <span className="glyph glyph-printer" aria-hidden="true" />;
}

export function MonitorGlyph() {
  return <span className="glyph glyph-monitor" aria-hidden="true" />;
}

export function UploadGlyph() {
  return <span className="glyph glyph-upload" aria-hidden="true" />;
}

export function MoreGlyph() {
  return <span className="glyph glyph-more" aria-hidden="true" />;
}

export function PlusGlyph() {
  return <span className="glyph glyph-plus" aria-hidden="true" />;
}
