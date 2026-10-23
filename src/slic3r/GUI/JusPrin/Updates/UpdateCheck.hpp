#pragma once

// JusPrin's own update check. It reads JusPrin's release list on GitHub at
// startup and then once a day, and when the person asks with Check now. It
// only finds out: the release page is opened by the person, and nothing is
// downloaded or installed.

#include "ReleaseFeed.hpp"

#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

class wxTimer;

namespace Slic3r::GUI {
class MainFrame;
}

namespace Slic3r::GUI::JusPrin::Updates {

class UpdateCheck
{
public:
    enum class Status {
        NotChecked,
        Checking,
        UpToDate,
        Available, // a newer release was found, at the last check or an earlier one
        Failed,    // the last check could not read the release list
        Store,     // a Microsoft Store install: the Store updates it, JusPrin does not check
    };

    struct State
    {
        Status                 status{Status::NotChecked};
        std::string            version; // the running JusPrin
        std::optional<Release> available;
        std::time_t            last_success{0}; // 0: never
        bool                   auto_check{true};
    };

    State state() const;

    // Runs the daily schedule while `frame` exists, starting a few seconds
    // from now.
    void attach(MainFrame& frame);
    // Checks at once, whether or not the daily check is on. Does nothing
    // while a check is under way or for a Store install.
    void check_now();
    // "Skip this version" on the release the page showed: it is no longer
    // offered, and neither is anything older.
    void skip(const std::string& version);

    // Calls `listener` on the UI thread whenever state() changes.
    int  subscribe(std::function<void()> listener);
    void unsubscribe(int token);

private:
    void tick(bool at_startup);
    void finish(std::time_t now, const std::optional<std::string>& body);
    void notify();

    std::unique_ptr<wxTimer>              m_timer;
    bool                                  m_checking{false};
    bool                                  m_started{false}; // the first tick of this run has passed
    std::map<int, std::function<void()>>  m_listeners;
    int                                   m_next_token{0};
};

// The one update check of this run.
UpdateCheck& update_check();

} // namespace Slic3r::GUI::JusPrin::Updates
