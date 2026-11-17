import '@testing-library/jest-dom/vitest';
// Activates English, the source language the tests read (no ?lang= in jsdom).
import '../i18n';

// jsdom lays nothing out, so it has neither of these; the page uses them only
// to re-measure the composer and to keep a highlighted menu row in view.
globalThis.ResizeObserver ??= class { observe() {} unobserve() {} disconnect() {} };
// A test that runs in Node (@vitest-environment node) has no Element.
if (typeof Element !== 'undefined') Element.prototype.scrollIntoView ??= function () {};
