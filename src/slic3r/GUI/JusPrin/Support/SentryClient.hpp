#pragma once

// Sentry's native SDK in the app: it catches crashes and sends the error
// reports ErrorReport.cpp decides to send. What each event holds is
// SentryEvent.hpp's.
//
// Nothing leaves without the person's consent (the SDK's
// require_user_consent). With automatic reports on, consent is given, and
// reports and crashes go as they happen. With them off, consent is given for
// the one report the person pressed "Report to JusPrin team" for, and taken
// back once it is captured; a crash is written to disk by crashpad and never
// sent.

#include "ErrorReport.hpp"

#include <string>

namespace Slic3r::GUI::JusPrin {

// Starts the SDK, once, with its files in `data_dir`. A build with no Sentry
// project (no JUSPRIN_SENTRY_DSN) starts nothing and sends nothing.
void start_sentry(const std::string& data_dir, bool reports_automatically);

// Gives or takes back consent as the person's setting now stands.
void sentry_follow_setting(bool reports_automatically);

// Sends one report. `reports_automatically` false means the person asked for
// this one: consent holds for it alone.
void sentry_send(const ErrorReport& report, bool reports_automatically);

// One step of what the app was doing, kept with the next report: `category`
// as "page" or "tool", `message` a message type or a tool name, never what
// the person wrote or a file's name.
void sentry_breadcrumb(const char* category, const std::string& message);

} // namespace Slic3r::GUI::JusPrin
