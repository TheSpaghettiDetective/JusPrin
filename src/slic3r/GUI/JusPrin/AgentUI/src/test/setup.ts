import '@testing-library/jest-dom/vitest';
// Activates English, the source language the tests read (no ?lang= in jsdom).
import '../i18n';

// jsdom lays nothing out, so it has neither of these; the page uses them only
// to re-measure the composer and to keep a highlighted menu row in view.
globalThis.ResizeObserver ??= class { observe() {} unobserve() {} disconnect() {} };
Element.prototype.scrollIntoView ??= function () {};
