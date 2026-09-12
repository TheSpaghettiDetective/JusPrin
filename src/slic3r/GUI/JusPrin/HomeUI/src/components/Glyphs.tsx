// Inline glyphs, as the Agent page does: the bundle is a single file, so an
// icon is markup rather than a fetched asset. Each takes the size and colour
// of the text it sits beside -- stroke follows currentColor, and the box is
// the smallest size on the icon scale.
export function PrinterGlyph() {
  return (
    <svg className="glyph" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.3" aria-hidden="true">
      <path d="M4.5 6V2.5h7V6" strokeLinecap="round" strokeLinejoin="round" />
      <rect x="1.8" y="6" width="12.4" height="5.2" rx="1.2" />
      <path d="M4.5 9.5h7V14h-7z" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

export function MonitorGlyph() {
  return (
    <svg className="glyph" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.3" aria-hidden="true">
      <rect x="1.8" y="2.8" width="12.4" height="8.4" rx="1.2" />
      <path d="M6 13.8h4" strokeLinecap="round" />
      <path d="M8 11.2v2.6" strokeLinecap="round" />
    </svg>
  );
}
