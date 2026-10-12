#pragma once

// The work OrcaSlicer runs outside the chat, as it starts, moves on and ends:
// a slice, and whatever OrcaSlicer reports only as a "still running"
// notification (an arrange, an export, an upload). Each becomes one entry the
// chat keeps at the place it started.
//
// A slice is followed through OrcaSlicer's own events, which carry how far it
// is and how it ended. The rest is read from the notification list by the
// monitor: such a notification says that the work is running and nothing
// about how it ended, so one that goes is "done" unless its Cancel was
// pressed here.
//
// GUI thread only.

#include "PrintIssue.hpp"

#include <wx/event.h>

#include <functional>
#include <map>
#include <string>

namespace Slic3r {
class SlicingStatusEvent;
class SlicingProcessCompletedEvent;
namespace GUI {
class Plater;
}
} // namespace Slic3r

namespace Slic3r::GUI::JusPrin::PrintIssues {

class WorkFeed final
{
public:
    // Where the entries go: the chat host's three calls.
    struct Sink
    {
        std::function<std::string(const std::string& kind, const std::string& title, bool cancellable)> begin;
        std::function<void(const std::string& id, int current, int total, const std::string& detail)>   update;
        std::function<void(const std::string& id, const std::string& state, const std::string& detail)> finish;
    };

    WorkFeed(Plater& plater, Sink sink);
    ~WorkFeed();

    WorkFeed(const WorkFeed&) = delete;
    WorkFeed& operator=(const WorkFeed&) = delete;

    // The monitor read OrcaSlicer's state again.
    void sync(const PrintIssueSnapshot& snapshot);
    // The Cancel button of one entry.
    void cancel(const std::string& id);

private:
    struct Running
    {
        std::string id, title;
        bool        cancel_pressed{false};
    };

    void on_slicing_update(SlicingStatusEvent& event);
    void on_process_completed(SlicingProcessCompletedEvent& event);
    void end_slice(const std::string& state, const std::string& detail);

    Plater& m_plater;
    Sink    m_sink;
    // The one slice OrcaSlicer runs at a time.
    Running m_slice;
    // By notification kind, and by its words for the kinds OrcaSlicer holds
    // several of (uploads, downloads).
    std::map<std::string, Running> m_noticed;
};

} // namespace Slic3r::GUI::JusPrin::PrintIssues
