// The top of a page's own code (AGENTS.md, Error handling): Sentry's browser
// SDK catches what nothing else did -- an uncaught error, a rejected promise --
// and builds its event with the stack, the recent steps and the page's
// context. The event is not sent from here: it goes to the app as a
// report_error message, and the app asks the person, or not when automatic
// reports are on, and sends it with its own Sentry SDK (Support/SentryClient).
// The page holds no Sentry project and never reaches the network for this.
import {
  SDK_VERSION,
  createTransport,
  init,
  rewriteFramesIntegration,
  type ErrorEvent,
  type EventHint,
} from '@sentry/browser';

// What a page sends the app: the two lines the report dialog shows, and the
// event Sentry receives.
export interface PageErrorReport {
  error: string;
  where: string;
  event: ErrorEvent;
}

// Which build of the page this is: the commit the release workflow built
// (JUSPRIN_PAGE_BUILD, read by vite.config.ts), "local" elsewhere. Sentry
// files the page's source map under it (Packaging/sentry_page_maps.sh), as a
// release's "dist", so builds that share a version keep their own maps.
declare const __JUSPRIN_PAGE_BUILD__: string;

// The SDK runs only with a DSN. This one names no host that exists, and the
// transport below never sends: every event leaves through beforeSend.
const NOWHERE = 'https://page@jusprin.invalid/0';

let started = false;
// The page's bridge client, which holds a report until the app can take it.
let deliverTo: (report: PageErrorReport) => void = () => {};

export function reportOf(event: ErrorEvent, hint: EventHint): PageErrorReport {
  // What the SDK itself does to an event as it sends it, which here it never
  // does: names itself, and drops its own working data.
  event.sdk = { ...event.sdk, name: 'sentry.javascript.browser', version: SDK_VERSION };
  delete event.sdkProcessingMetadata;
  const original = hint.originalException;
  if (original instanceof Error) {
    return { error: `${original.name}: ${original.message}`, where: original.stack ?? '', event };
  }
  const exception = event.exception?.values?.at(-1);
  const error = exception ? `${exception.type ?? 'Error'}: ${exception.value ?? ''}` : String(original);
  return { error, where: '', event };
}

// Starts catching, once per page, with every integration the SDK has by
// default and the one that names frames below; `deliver`, the latest bridge
// client's, hands each report to the app.
export function startErrorReports(deliver: (report: PageErrorReport) => void): void {
  deliverTo = deliver;
  if (started) return;
  started = true;
  // The page loads from the app's resources folder (Support/PageScheme.hpp):
  // jusprin:///home/index.html on macOS and Linux, and on Windows a file://
  // address wherever the app is installed.
  // Sentry finds the page's source map by a frame's file name
  // (Packaging/sentry_page_maps.sh), so frames name the page's folder the same
  // way on every computer: app:///home/index.html.
  const folder = new URL('.', window.location.href);
  const name = folder.pathname.split('/').filter(Boolean).pop() ?? '';
  init({
    dsn: NOWHERE,
    dist: __JUSPRIN_PAGE_BUILD__,
    integrations: [rewriteFramesIntegration({ root: folder.href, prefix: name ? `app:///${name}/` : 'app:///' })],
    transport: (options) => createTransport(options, () => Promise.resolve({})),
    beforeSend(event, hint) {
      deliverTo(reportOf(event, hint));
      return null;
    },
  });
}
