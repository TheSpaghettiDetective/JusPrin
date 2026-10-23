#include "UpdateCheck.hpp"

#include "UpdateRecord.hpp"
#include "Version.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/log/trivial.hpp>

#include <wx/timer.h>

namespace Slic3r::GUI::JusPrin::Updates {

namespace {

constexpr char kReleasesUrl[] = "https://api.github.com/repos/TheSpaghettiDetective/JusPrin/releases?per_page=30";
constexpr int  kFirstTickMs   = 10 * 1000;
// Hourly, so a JusPrin left open for days still checks once a day.
constexpr int kTickMs = 60 * 60 * 1000;

AppConfig& config() { return *wxGetApp().app_config; }

std::time_t now() { return std::time(nullptr); }

} // namespace

UpdateCheck::State UpdateCheck::state() const
{
    const UpdateRecord record = load_record(config(), kVersion);
    State              state;
    state.version      = kVersion;
    state.available    = record.available;
    state.last_success = record.last_success;
    state.auto_check   = auto_check_enabled(config());
    if (is_running_in_msix())
        state.status = Status::Store;
    else if (m_checking)
        state.status = Status::Checking;
    else if (record.available)
        state.status = Status::Available;
    else if (record.last_failed)
        state.status = Status::Failed;
    else if (record.last_success != 0)
        state.status = Status::UpToDate;
    return state;
}

void UpdateCheck::attach(MainFrame& frame)
{
    // Recreating the GUI builds the new frame before destroying the old one,
    // so the schedule moves to the new frame here and the old frame's
    // destruction leaves it alone.
    m_timer        = std::make_unique<wxTimer>();
    wxTimer* timer = m_timer.get();
    timer->Bind(wxEVT_TIMER, [this, timer](wxTimerEvent&) {
        const bool at_startup = !m_started;
        m_started             = true;
        if (at_startup)
            timer->Start(kTickMs);
        tick(at_startup);
    });
    timer->Start(m_started ? kTickMs : kFirstTickMs, m_started ? wxTIMER_CONTINUOUS : wxTIMER_ONE_SHOT);
    frame.Bind(wxEVT_DESTROY, [this, &frame, timer](wxWindowDestroyEvent& event) {
        if (event.GetEventObject() == &frame && m_timer.get() == timer)
            m_timer.reset();
        event.Skip();
    });
}

void UpdateCheck::tick(bool at_startup)
{
    // The stealth mode the person turned on, not get_stealth_mode(), which
    // also reports it until OrcaSlicer's setup guide is finished: JusPrin's
    // own setup never finishes that guide.
    if (!auto_check_enabled(config()) || config().get_bool("stealth_mode"))
        return;
    if (check_due(load_record(config(), kVersion), now(), at_startup))
        check_now();
}

void UpdateCheck::check_now()
{
    if (m_checking || is_running_in_msix())
        return;
    m_checking = true;
    notify();

    const std::string feed = config().get(kFeedUrlKey);
    // Nothing about the person goes with the request: no install ID and no
    // system details, only what any web request carries.
    Http::get(feed.empty() ? kReleasesUrl : feed)
        .header("Accept", "application/vnd.github+json")
        .header("User-Agent", std::string("JusPrin/") + kVersion)
        .timeout_connect(5)
        .timeout_max(10)
        .on_complete([this](std::string body, unsigned status) {
            wxGetApp().CallAfter([this, body = std::move(body), status] {
                if (status == 200) {
                    finish(now(), body);
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "JusPrin's update check got HTTP " << status;
                    finish(now(), std::nullopt);
                }
            });
        })
        .on_error([this](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(warning) << "JusPrin's update check failed: HTTP " << status << ", " << error;
            wxGetApp().CallAfter([this] { finish(now(), std::nullopt); });
        })
        .perform();
}

void UpdateCheck::finish(std::time_t at, const std::optional<std::string>& body)
{
    m_checking = false;
    if (!body) {
        save_failure(config(), at);
        notify();
        return;
    }
    std::optional<Release> newer;
    try {
        newer = newer_release(*body, kVersion);
    } catch (const FeedError& error) {
        // Something other than GitHub's release list came back, such as a
        // captive portal's page: the check failed, the same as no answer.
        BOOST_LOG_TRIVIAL(warning) << "JusPrin's update check could not read the release list: " << error.what();
        save_failure(config(), at);
        notify();
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "JusPrin's update check: " << (newer ? "found " + newer->version : std::string("up to date"));
    save_success(config(), at, newer);
    notify();
}

void UpdateCheck::skip(const std::string& version)
{
    skip_version(config(), version);
    notify();
}

int UpdateCheck::subscribe(std::function<void()> listener)
{
    m_listeners.emplace(++m_next_token, std::move(listener));
    return m_next_token;
}

void UpdateCheck::unsubscribe(int token) { m_listeners.erase(token); }

void UpdateCheck::notify()
{
    // A listener may unsubscribe while it runs.
    const auto listeners = m_listeners;
    for (const auto& [token, listener] : listeners)
        listener();
}

UpdateCheck& update_check()
{
    static UpdateCheck check;
    return check;
}

} // namespace Slic3r::GUI::JusPrin::Updates
