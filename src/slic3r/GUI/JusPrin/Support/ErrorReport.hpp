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
// (AgentWebView, HomeWebView), the pumps that read an AI reply or title and a
// key check (AgentHost::pump_stream, AgentHost::pump_setup, reporting through
// the handler AgentWebView gives them), every place JusPrin's own code starts
// running by itself -- a CallAfter, a timer, an HTTP callback, a thread --
// through reported() below, and the pages' own top level, which sends a
// report_error message (WebShared/bridgeClient.ts). A tool call's failure goes
// to the AI model instead; one in the app's own work is also sent through
// report_if_automatic, when automatic reports are on. An exception that leaves
// any other native event is OrcaSlicer's to handle, exactly as upstream
// handles it (GUI_App::OnExceptionInMainLoop).

#include <string>
#include <utility>

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

// Reports a tool call that failed in the app's own work, whose error already
// went to the AI model, or a start report the server answered with a fault
// (LaunchReport): sent only when automatic reports are on, and no dialog
// opens. Any thread.
void report_if_automatic(std::string doing, std::string error);

// The exception being handled, as a report names it: its type and what().
// Call inside a catch block.
std::string describe_current_exception();

// The top handler of an entry point JusPrin's own code starts: wraps the
// body of a CallAfter, a timer, an HTTP callback or a thread, so that an
// exception leaving it is reported, saying the app was `doing` this. Any
// thread.
template<class Body> auto reported(const char* doing, Body body)
{
    return [doing, body = std::move(body)](auto&&... arguments) mutable {
        try {
            body(std::forward<decltype(arguments)>(arguments)...);
        } catch (...) {
            report_unexpected_error(doing, describe_current_exception());
        }
    };
}

// The report as it would be sent now, for the dialog and the tests.
ErrorReport make_error_report(std::string doing, std::string error, std::string where);
// The JSON body the server receives (web_ent/jusprin/v2/app_errors.py).
std::string error_report_json(const ErrorReport& report);
// Posts the report to JusPrin's server, in the background.
void send_error_report(const ErrorReport& report);

} // namespace Slic3r::GUI::JusPrin
