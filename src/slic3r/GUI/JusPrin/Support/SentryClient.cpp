#include "SentryClient.hpp"

#include "slic3r/GUI/JusPrin/Support/ErrorReport.hpp"
#include "slic3r/GUI/JusPrin/Support/SentryEvent.hpp"
#include "slic3r/GUI/JusPrin/Support/ThrowStack.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/log/trivial.hpp>

#include <sentry.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

// The Sentry project reports go to. A release build gets it from the release
// workflow (JusPrin/sources.cmake), so the repository names no production
// host; JUSPRIN_SENTRY_DSN in the environment takes its place, which is how a
// test points the app at a stand-in. A build with neither sends no report.
#ifndef JUSPRIN_SENTRY_DSN
#define JUSPRIN_SENTRY_DSN ""
#endif
std::string sentry_dsn()
{
    if (const char* chosen = std::getenv("JUSPRIN_SENTRY_DSN"); chosen != nullptr && *chosen != '\0')
        return chosen;
    return JUSPRIN_SENTRY_DSN;
}

bool g_started = false;
// Who reports and crashes name: the account signed in, or else the install.
std::string g_install_id;
std::string g_account_id;

// The SDK hands each envelope to OrcaSlicer's own HTTP client, which knows
// the system's certificates and proxy on every platform; the SDK's own curl
// transport would need both set up a second time. Anonymous: no token or
// cookie goes with it.
struct Uploads
{
    SentryDsn               dsn;
    std::mutex              mutex;
    std::condition_variable idle;
    int                     pending = 0;
};

void upload_done(Uploads& uploads)
{
    std::lock_guard<std::mutex> lock(uploads.mutex);
    --uploads.pending;
    uploads.idle.notify_all();
}

void send_envelope(sentry_envelope_t* envelope, void* state)
{
    Uploads&    uploads = *static_cast<Uploads*>(state);
    std::size_t size    = 0;
    char*       body    = sentry_envelope_serialize(envelope, &size);
    sentry_envelope_free(envelope);
    std::string payload(body, size);
    sentry_free(body);
    {
        std::lock_guard<std::mutex> lock(uploads.mutex);
        ++uploads.pending;
    }
    Slic3r::Http::post(uploads.dsn.envelope_url)
        .header("User-Agent", Updates::user_agent())
        .header("Content-Type", "application/x-sentry-envelope")
        .header("X-Sentry-Auth", sentry_auth_header(uploads.dsn, std::string("jusprin.app/") + Updates::kVersion))
        .set_post_body(payload)
        .tls_verify(true)
        .timeout_connect(10)
        .timeout_max(30)
        .on_complete([&uploads](std::string, unsigned) { upload_done(uploads); })
        .on_error([&uploads](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "JusPrin could not send an error report to " << uploads.dsn.envelope_url << " (" << status
                                     << "): " << error << " " << body;
            upload_done(uploads);
        })
        .perform();
}

// Waits for the uploads under way, at most `timeout` milliseconds; 0 when
// none is left.
int wait_for_uploads(uint64_t timeout, void* state)
{
    Uploads&                     uploads = *static_cast<Uploads*>(state);
    std::unique_lock<std::mutex> lock(uploads.mutex);
    return uploads.idle.wait_for(lock, std::chrono::milliseconds(timeout), [&uploads] { return uploads.pending == 0; }) ? 0 : 1;
}

// This run's log file (GUI_App's set_log_path_and_level names it after the
// process id); none when the system writes no log file.
std::optional<std::filesystem::path> log_file(const std::string& data_dir)
{
    const std::filesystem::path folder = std::filesystem::u8path(data_dir) / "log";
    if (!std::filesystem::is_directory(folder))
        return std::nullopt;
    const std::string ending = "_" + std::to_string(get_current_pid()) + ".log.0";
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > ending.size() && name.compare(name.size() - ending.size(), ending.size(), ending) == 0)
            return entry.path();
    }
    return std::nullopt;
}

// The person is the signed-in account, or else this install. The install
// is a tag as well, so one person's reports from before and after signing in
// can be matched, and matched to the server's record of the install's starts.
void name_person()
{
    if (!g_install_id.empty())
        sentry_set_tag("install_id", g_install_id.c_str());
    const std::string& id = g_account_id.empty() ? g_install_id : g_account_id;
    if (id.empty()) {
        sentry_remove_user();
        return;
    }
    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_string(id.c_str()));
    sentry_set_user(user);
}

} // namespace

void start_sentry(const std::string& data_dir, bool reports_automatically, const std::string& install_id)
{
    if (g_started)
        return;
    const std::optional<SentryDsn> dsn = parse_sentry_dsn(sentry_dsn());
    if (!dsn) {
        BOOST_LOG_TRIVIAL(warning) << "JusPrin sends no error reports or crashes: this build has no Sentry project to send them to";
        return;
    }
    // Lives as long as the app: the SDK may send until the process ends.
    auto* uploads = new Uploads{*dsn};
    sentry_transport_t* transport = sentry_transport_new(send_envelope);
    sentry_transport_set_state(transport, uploads);
    sentry_transport_set_flush_func(transport, wait_for_uploads);
    sentry_transport_set_shutdown_func(transport, wait_for_uploads);

    // crashpad_handler ships beside the app's executable
    // (JusPrin/CMakeLists.txt).
    const std::filesystem::path handler = std::filesystem::path(boost::dll::program_location().native()).parent_path() /
#ifdef _WIN32
                                          "crashpad_handler.exe";
#else
                                          "crashpad_handler";
#endif
    const std::filesystem::path database = std::filesystem::u8path(data_dir) / "jusprin-sentry";

    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, sentry_dsn().c_str());
    sentry_options_set_release(options, Updates::version_label().c_str());
    sentry_options_set_transport(options, transport);
    sentry_options_set_require_user_consent(options, 1);
#ifdef _WIN32
    sentry_options_set_handler_pathw(options, handler.wstring().c_str());
    sentry_options_set_database_pathw(options, database.wstring().c_str());
#else
    sentry_options_set_handler_path(options, handler.string().c_str());
    sentry_options_set_database_path(options, database.string().c_str());
#endif
    // Every report and crash carries this run's log.
    if (const std::optional<std::filesystem::path> log = log_file(data_dir)) {
#ifdef _WIN32
        sentry_options_add_attachmentw(options, log->wstring().c_str());
#else
        sentry_options_add_attachment(options, log->string().c_str());
#endif
    }
    if (sentry_init(options) != 0) {
        BOOST_LOG_TRIVIAL(error) << "JusPrin could not start Sentry's SDK; it sends no error reports or crashes";
        return;
    }
    g_started = true;
    g_install_id = install_id;
    name_person();
    start_recording_throws();
    sentry_follow_setting(reports_automatically);
}

void stop_sentry()
{
    if (!g_started)
        return;
    g_started = false;
    sentry_close();
}

void sentry_follow_setting(bool reports_automatically)
{
    if (!g_started)
        return;
    if (reports_automatically)
        sentry_user_consent_give();
    else
        sentry_user_consent_revoke();
}

void sentry_send(const ErrorReport& report, bool reports_automatically)
{
    if (!g_started) {
        BOOST_LOG_TRIVIAL(error) << "JusPrin could not send an error report: Sentry's SDK is not running";
        return;
    }
    sentry_value_t event = sentry_event(report);
    // Where the app found the failure: where it was thrown, or the code that
    // reported it; a page's event has the page's own stack.
    if (report.page_event.empty()) {
        std::vector<void*> frames = report.stack;
        sentry_event_value_add_stacktrace(event, frames.empty() ? nullptr : frames.data(), frames.size());
    }
    if (reports_automatically) {
        sentry_capture_event(event);
        return;
    }
    // The SDK decides when it captures, so consent given now covers this
    // event only.
    sentry_user_consent_give();
    sentry_capture_event(event);
    sentry_user_consent_revoke();
}

void sentry_set_account(const std::string& account_id)
{
    g_account_id = account_id;
    if (g_started)
        name_person();
}

void sentry_breadcrumb(const char* category, const std::string& message)
{
    if (!g_started)
        return;
    sentry_value_t crumb = sentry_value_new_breadcrumb("default", message.c_str());
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string(category));
    sentry_add_breadcrumb(crumb);
}

} // namespace Slic3r::GUI::JusPrin
