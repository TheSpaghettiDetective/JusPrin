#pragma once

// What one error report (ErrorReport.hpp) becomes in Sentry. No wx and no
// network, so the tests can read what would be sent; SentryClient.cpp starts
// the SDK and sends.

#include "ErrorReport.hpp"

#include <sentry.h>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

// A DSN, "https://<public key>@<host>/<project>", as an upload address and key.
// The key can only send new events, so it may ship inside the app.
struct SentryDsn
{
    std::string envelope_url; // "https://<host>/api/<project>/envelope/"
    std::string public_key;
};

// nullopt for an empty DSN or one not in that shape.
std::optional<SentryDsn> parse_sentry_dsn(const std::string& dsn);

// The X-Sentry-Auth header value for `client`, as "jusprin.app/2.0.0".
std::string sentry_auth_header(const SentryDsn& dsn, const std::string& client);

// What Sentry groups by: what the app was doing and the error, without the ids
// that differ every time one failure happens (the app's own "(request <turn>
// #<n>, its request id <id>)" and a provider body's "request_id"), so two
// reports of one failure are one Sentry issue. The ids stay in the message.
std::vector<std::string> sentry_fingerprint(const ErrorReport& report);

// The event for `report`, owned by the caller: the page's own event when a
// page sent one, otherwise a message event holding the error. Either way it
// carries the release (the app line), the tags (what the app was doing, the
// system), the report's lines as extra, and the grouping above. The SDK adds
// the rest when it is captured: the stack, the loaded modules, the machine,
// the breadcrumbs.
sentry_value_t sentry_event(const ErrorReport& report);

} // namespace Slic3r::GUI::JusPrin
