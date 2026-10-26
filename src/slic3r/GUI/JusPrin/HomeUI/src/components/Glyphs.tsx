// The masks in styles.css use the same pinned Lucide SVGs as the native shell.
// Keeping them in CSS lets Vite inline the artwork into the offline WebView.
export function PrinterGlyph() {
  return <span className="glyph glyph-printer" aria-hidden="true" />;
}

export function MonitorGlyph() {
  return <span className="glyph glyph-monitor" aria-hidden="true" />;
}

// Large on the drop targets, where it stands alone above the text.
export function UploadGlyph({ large }: { large?: boolean } = {}) {
  return <span className={large ? 'glyph glyph-large glyph-upload' : 'glyph glyph-upload'} aria-hidden="true" />;
}

export function SearchGlyph() {
  return <span className="glyph glyph-search" aria-hidden="true" />;
}

export function ChevronDownGlyph() {
  return <span className="glyph glyph-chevron-down" aria-hidden="true" />;
}

export function ChevronRightGlyph() {
  return <span className="glyph glyph-chevron-right" aria-hidden="true" />;
}

export function MoreGlyph() {
  return <span className="glyph glyph-more" aria-hidden="true" />;
}

export function SettingsGlyph() {
  return <span className="glyph glyph-settings" aria-hidden="true" />;
}

export function PlusGlyph() {
  return <span className="glyph glyph-plus" aria-hidden="true" />;
}
