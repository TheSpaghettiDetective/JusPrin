import '@testing-library/jest-dom/vitest';

// jsdom lays nothing out, so it has no scrolling; the Print history brings the
// entry a chat marker named into view.
Element.prototype.scrollIntoView ??= function () {};
