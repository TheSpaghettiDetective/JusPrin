#include "ErrorReport.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Shell/ErrorReportDialog.hpp"
#include "slic3r/GUI/JusPrin/Support/SentryClient.hpp"
#include "slic3r/GUI/JusPrin/Support/ThrowStack.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"

#include <boost/core/demangle.hpp>
#include <boost/log/trivial.hpp>

#include <wx/thread.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

std::string os_label()
{
    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);
#if defined(__APPLE__)
    const std::string name = "macOS";
#elif defined(_WIN32)
    const std::string name = "Windows";
#else
    const std::string name = "Linux";
#endif
    return name + " " + std::to_string(major) + "." + std::to_string(minor) + (micro > 0 ? "." + std::to_string(micro) : "");
}

bool reports_automatically()
{
    return wxGetApp().app_config != nullptr && wxGetApp().app_config->get_bool(kReportErrorsKey);
}

// The reports waiting for the person, one dialog at a time. A failure that
// repeats while its dialog is open or waiting adds nothing: the person has one
// thing to answer. Nor does one they already answered or sent in this run: a
// failure that comes back each time a window is in front again would otherwise
// reopen the dialog as soon as it closes, since closing it puts the window in
// front, and with reports sent automatically would be sent each time.
std::deque<ErrorReport>  g_waiting;
std::vector<ErrorReport> g_answered;
bool                     g_showing = false;

bool same_failure(const ErrorReport& a, const ErrorReport& b) { return a.doing == b.doing && a.error == b.error; }

void show_next()
{
    if (g_showing || g_waiting.empty())
        return;
    g_showing = true;
    const ErrorReport report = g_waiting.front();
    const ErrorReportDialog::Answer answer = ErrorReportDialog::ask(report);
    g_waiting.pop_front();
    g_answered.push_back(report);
    g_showing = false;
    if (answer.automatically) {
        wxGetApp().app_config->set_bool(kReportErrorsKey, true);
        wxGetApp().app_config->save();
        sentry_follow_setting(true);
    }
    if (answer.report)
        sentry_send(report, answer.automatically);
    // Once reports go automatically, the ones still waiting go too.
    if (answer.automatically) {
        for (const ErrorReport& waiting : g_waiting)
            sentry_send(waiting, true);
        g_answered.insert(g_answered.end(), g_waiting.begin(), g_waiting.end());
        g_waiting.clear();
    }
    if (!g_waiting.empty())
        wxGetApp().CallAfter([] { show_next(); });
}

} // namespace

ErrorReport make_error_report(std::string doing, std::string error, std::string where, std::string page_event)
{
    return ErrorReport{Updates::version_label(), os_label(), std::move(doing), std::move(error), std::move(where), std::move(page_event)};
}

namespace {

void report_unexpected(std::string doing, std::string error, std::string where, std::string page_event, std::vector<void*> stack)
{
    // An entry point on another thread (an HTTP callback, a worker) reports
    // on the main thread, where the waiting reports and the dialog live.
    if (!wxThread::IsMain()) {
        wxGetApp().CallAfter([doing = std::move(doing), error = std::move(error), where = std::move(where), page_event = std::move(page_event),
                              stack = std::move(stack)]() mutable {
            report_unexpected(std::move(doing), std::move(error), std::move(where), std::move(page_event), std::move(stack));
        });
        return;
    }
    ErrorReport report = make_error_report(std::move(doing), std::move(error), std::move(where), std::move(page_event));
    report.stack = std::move(stack);
    BOOST_LOG_TRIVIAL(error) << "Unexpected error while " << report.doing << ": " << report.error;
    const auto same = [&report](const ErrorReport& asked) { return same_failure(asked, report); };
    if (std::any_of(g_waiting.begin(), g_waiting.end(), same) || std::any_of(g_answered.begin(), g_answered.end(), same))
        return;
    if (reports_automatically()) {
        sentry_send(report, true);
        g_answered.push_back(std::move(report));
        return;
    }
    g_waiting.push_back(std::move(report));
    // After the event that failed: the dialog's own loop never runs inside a
    // page's message handler or the main loop's exception hook.
    wxGetApp().CallAfter([] { show_next(); });
}

void report_if_automatic(std::string doing, std::string error, std::vector<void*> stack)
{
    // From another thread (an HTTP callback), on the main thread, where the
    // settings it reads are written.
    if (!wxThread::IsMain()) {
        wxGetApp().CallAfter([doing = std::move(doing), error = std::move(error), stack = std::move(stack)]() mutable {
            report_if_automatic(std::move(doing), std::move(error), std::move(stack));
        });
        return;
    }
    BOOST_LOG_TRIVIAL(error) << "Unexpected error while " << doing << ": " << error;
    if (reports_automatically()) {
        ErrorReport report = make_error_report(std::move(doing), std::move(error), {});
        report.stack = std::move(stack);
        sentry_send(report, true);
    }
}

} // namespace

// Both take the failure's stack here, in the code that reports it.
void report_unexpected_error(std::string doing, std::string error, std::string where, std::string page_event)
{
    report_unexpected(std::move(doing), std::move(error), std::move(where), std::move(page_event), failure_stack());
}

void report_if_automatic(std::string doing, std::string error)
{
    report_if_automatic(std::move(doing), std::move(error), failure_stack());
}

std::string describe_current_exception()
{
    try {
        throw;
    } catch (const std::filesystem::filesystem_error& error) {
        // Its paths name the person's files and projects, which a report
        // never holds: each becomes "<path>". Where a path is not found as
        // written (another encoding), only the error's code is kept.
        std::string text = error.what();
        for (const std::filesystem::path* path : {&error.path1(), &error.path2()}) {
            const std::string named = path->u8string();
            if (named.empty())
                continue;
            if (text.find(named) == std::string::npos) {
                text = error.code().message();
                break;
            }
            for (std::size_t at = text.find(named); at != std::string::npos; at = text.find(named, at + 6))
                text.replace(at, named.size(), "<path>");
        }
        return boost::core::demangle(typeid(error).name()) + ": " + text;
    } catch (const std::exception& error) {
        return boost::core::demangle(typeid(error).name()) + ": " + error.what();
    } catch (...) {
        return "an exception that is not a std::exception";
    }
}

} // namespace Slic3r::GUI::JusPrin
