#pragma once

// The one way JusPrin reports an unexpected error (AGENTS.md, Error
// handling): a failure no person decided an outcome for travels up unchanged
// to the handler at the top of its entry point, and that handler, and only
// it, calls report_unexpected_error. The person is told "Something went wrong
// on our side" in ErrorReportDialog and nothing more specific; the report
// goes to JusPrin's server, which passes it to Sentry, once they press
// "Report to JusPrin team" or after they turned automatic reports on.
//
// The top handlers today: the Agent and Home pages' message handlers
// (AgentWebView, HomeWebView), the file picker's callback, which reads the
// chosen files after the page's message was handled (AgentWebView), and the
// pages' own top level, which sends a report_error message
// (WebShared/bridgeClient.ts). An exception that leaves any other native
// event is OrcaSlicer's to handle, exactly as upstream handles it
// (GUI_App::OnExceptionInMainLoop).

#include <string>

namespace Slic3r::GUI::JusPrin {

// What a report holds: the lines "See what the report contains" lists, and
// all the server receives. No chat, file, project name or account.
struct ErrorReport
{
    std::string app;   // "JusPrin 2.0.0 (OrcaSlicer 2.5.0)"
    std::string os;    // "macOS 26.3"
    std::string doing; // what the app was doing, said by the top handler
    std::string error; // the error as it reached the top, unchanged
    std::string where; // the stack, where one exists (the pages' for now)
};

// The AppConfig key of "report errors automatically", off until the person
// turns it on in the dialog or in Preferences › Updates.
inline constexpr const char* kReportErrorsKey = "jusprin_report_errors_automatically";

// Reports one unexpected error: sends it at once when automatic reports are
// on, otherwise opens the dialog after the current event, over the window in
// front. Only top handlers call this.
void report_unexpected_error(std::string doing, std::string error, std::string where = {});

// The exception being handled, as a report names it: its type and what().
// Call inside a catch block.
std::string describe_current_exception();

// The report as it would be sent now, for the dialog and the tests.
ErrorReport make_error_report(std::string doing, std::string error, std::string where);
// The JSON body the server receives (web_ent/jusprin/v2/app_errors.py).
std::string error_report_json(const ErrorReport& report);
// Posts the report to JusPrin's server, in the background.
void send_error_report(const ErrorReport& report);

} // namespace Slic3r::GUI::JusPrin
