import '@testing-library/jest-dom/vitest';

// jsdom lays nothing out, so it has neither of these; the page uses them only
// to re-measure the composer and to keep a highlighted menu row in view.
globalThis.ResizeObserver ??= class { observe() {} unobserve() {} disconnect() {} };
Element.prototype.scrollIntoView ??= function () {};
