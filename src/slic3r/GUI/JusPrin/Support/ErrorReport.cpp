#include "ErrorReport.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Shell/ErrorReportDialog.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/core/demangle.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <wx/utils.h>
#include <wx/window.h>

#include <deque>
#include <filesystem>
#include <typeinfo>
#include <utility>

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
// thing to answer.
std::deque<ErrorReport> g_waiting;
bool                    g_showing = false;

bool same_failure(const ErrorReport& a, const ErrorReport& b) { return a.doing == b.doing && a.error == b.error; }

void show_next()
{
    if (g_showing || g_waiting.empty())
        return;
    g_showing = true;
    const ErrorReport report = g_waiting.front();
    const ErrorReportDialog::Answer answer = ErrorReportDialog::ask(report);
    g_waiting.pop_front();
    g_showing = false;
    if (answer.automatically) {
        wxGetApp().app_config->set_bool(kReportErrorsKey, true);
        wxGetApp().app_config->save();
    }
    if (answer.report)
        send_error_report(report);
    // Once reports go automatically, the ones still waiting go too.
    if (answer.automatically) {
        for (const ErrorReport& waiting : g_waiting)
            send_error_report(waiting);
        g_waiting.clear();
    }
    if (!g_waiting.empty())
        wxGetApp().CallAfter([] { show_next(); });
}

} // namespace

ErrorReport make_error_report(std::string doing, std::string error, std::string where)
{
    // A path in the error names the person's home folder, which names them;
    // the report says "~" instead.
    const std::string home = wxGetHomeDir().ToUTF8().data();
    const auto without_home = [&home](std::string text) {
        if (home.size() > 1)
            for (std::size_t at = text.find(home); at != std::string::npos; at = text.find(home, at + 1))
                text.replace(at, home.size(), "~");
        return text;
    };
    // The server refuses a field over its limit (web_ent/jusprin/v2/app_errors.py
    // FIELDS), counted in characters; cutting at as many bytes, on a
    // character boundary, keeps every field within it.
    const auto within = [](std::string text, std::size_t limit) {
        if (text.size() > limit) {
            std::size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                --end;
            text.resize(end);
        }
        return text;
    };
    return ErrorReport{Updates::version_label(), os_label(), within(std::move(doing), 500), within(without_home(std::move(error)), 8000),
                       within(without_home(std::move(where)), 32000)};
}

std::string error_report_json(const ErrorReport& report)
{
    return nlohmann::json{{"app", report.app}, {"os", report.os}, {"doing", report.doing}, {"error", report.error}, {"where", report.where}}
        .dump();
}

void send_error_report(const ErrorReport& report)
{
    const std::string url = Agent::cloud_error_reports_url(wxGetApp().app_config);
    // Anonymous: no token or cookie goes with it.
    Slic3r::Http::post(url)
        .header("User-Agent", Updates::user_agent())
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .set_post_body(error_report_json(report))
        .tls_verify(true)
        .timeout_connect(10)
        .timeout_max(30)
        // A report that cannot be sent has nowhere to be reported to.
        .on_error([url](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "JusPrin could not send an error report to " << url << " (" << status << "): " << error << " " << body;
        })
        .perform();
}

void report_unexpected_error(std::string doing, std::string error, std::string where)
{
    ErrorReport report = make_error_report(std::move(doing), std::move(error), std::move(where));
    BOOST_LOG_TRIVIAL(error) << "Unexpected error while " << report.doing << ": " << report.error;
    if (reports_automatically()) {
        send_error_report(report);
        return;
    }
    for (const ErrorReport& waiting : g_waiting)
        if (same_failure(waiting, report))
            return;
    g_waiting.push_back(std::move(report));
    // After the event that failed: the dialog's own loop never runs inside a
    // page's message handler or the main loop's exception hook.
    wxGetApp().CallAfter([] { show_next(); });
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
