// Runs `action` once what the page just rendered is on screen. A host message
// that does long work on the UI thread blocks the window too, so a busy label
// set in the same turn never shows. The web page draws its frame in its own
// process, but the app's UI thread puts that frame on screen, and it does so
// only on a later turn. One frame and a zero timeout send the message before
// that turn comes: the import then ran with "Import" still on screen. Waiting
// for the next frame and 50 ms more lets the UI thread show it first (seen in
// the built app on macOS, 2026-11-05). A hidden view gets no frames, hence
// the fallback.

const SETTLE_MS = 50;
const FALLBACK_MS = 500;

export function afterPaint(action: () => void): void {
  let done = false;
  const run = () => {
    if (done) return;
    done = true;
    action();
  };
  requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(run, SETTLE_MS)));
  setTimeout(run, FALLBACK_MS);
}
