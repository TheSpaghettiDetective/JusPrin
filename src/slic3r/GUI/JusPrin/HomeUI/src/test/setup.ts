import '@testing-library/jest-dom/vitest';
// Activates English, the source language the tests read (no ?lang= in jsdom).
import '../../../AgentUI/src/i18n';

// jsdom lays nothing out, so it has no scrolling; the Print history brings the
// entry a chat marker named into view.
Element.prototype.scrollIntoView ??= function () {};
