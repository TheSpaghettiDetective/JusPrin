#include "WorkFeed.hpp"

#include "libslic3r/PrintBase.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/JusPrin/Workspace/PlaterProjectState.hpp"

#include <algorithm>
#include <set>

namespace Slic3r::GUI::JusPrin::PrintIssues {

namespace {

std::string first_line(const std::string& text) { return text.substr(0, text.find('\n')); }

// What kind of entry a "still running" notification is.
const char* work_kind(int notice_kind)
{
    switch (NotificationType(notice_kind)) {
    case NotificationType::ExportOngoing:   return "export";
    case NotificationType::PrintHostUpload: return "upload";
    case NotificationType::URLDownload:     return "download";
    default:                                return "job";
    }
}

// The Plater's job worker (arrange, orient, fill the plate, the text tools)
// reports through this one notification and can be asked to stop. OrcaSlicer's
// own Cancel beside it does the same.
bool runs_on_job_worker(int notice_kind) { return notice_kind == int(NotificationType::ProgressIndicator); }

// OrcaSlicer holds several uploads and downloads at once and tells them apart
// by their words; of every other kind it holds one.
std::string notice_key(const Notice& notice)
{
    const bool several = notice.kind == int(NotificationType::PrintHostUpload) || notice.kind == int(NotificationType::URLDownload);
    return notice.kind_name + (several ? "|" + notice.text : std::string());
}

} // namespace

WorkFeed::WorkFeed(Plater& plater, Sink sink) : m_plater(plater), m_sink(std::move(sink))
{
    wxASSERT(wxIsMainThread());
    m_plater.Bind(EVT_SLICING_UPDATE, &WorkFeed::on_slicing_update, this);
    m_plater.Bind(EVT_PROCESS_COMPLETED, &WorkFeed::on_process_completed, this);
}

WorkFeed::~WorkFeed()
{
    wxASSERT(wxIsMainThread());
    m_plater.Unbind(EVT_SLICING_UPDATE, &WorkFeed::on_slicing_update, this);
    m_plater.Unbind(EVT_PROCESS_COMPLETED, &WorkFeed::on_process_completed, this);
}

void WorkFeed::on_slicing_update(SlicingStatusEvent& event)
{
    event.Skip();
    // Warnings travel on the same event and say nothing about progress.
    if (event.status.percent < 0 || !m_plater.is_background_process_slicing())
        return;
    if (m_slice.id.empty()) {
        m_slice       = {};
        m_slice.title = format(_u8L("Slicing plate %1%"), m_plater.get_partplate_list().get_curr_plate_index() + 1);
        m_slice.id    = m_sink.begin("slice", m_slice.title, true);
    }
    m_sink.update(m_slice.id, std::clamp(event.status.percent, 0, 100), 100, first_line(event.status.text));
}

void WorkFeed::on_process_completed(SlicingProcessCompletedEvent& event)
{
    event.Skip();
    if (m_slice.id.empty())
        return; // an export after the slice ends on the same event
    if (event.cancelled())
        end_slice("cancelled", {});
    else if (event.error())
        end_slice("failed", first_line(event.format_error_message().first));
    else
        end_slice("done", {});
}

void WorkFeed::end_slice(const std::string& state, const std::string& detail)
{
    const std::string id = std::move(m_slice.id);
    m_slice = {};
    m_sink.finish(id, state, detail);
}

void WorkFeed::sync(const PrintIssueSnapshot& snapshot)
{
    // OrcaSlicer stopped a slice without reporting an end: an edit replaced it.
    if (!m_slice.id.empty() && !m_plater.is_background_process_slicing())
        end_slice("cancelled", {});

    std::set<std::string> held;
    for (const Notice& notice : snapshot.notices) {
        if (notice.route != NoticeRoute::Progress || notice.text.empty())
            continue;
        const std::string key = notice_key(notice);
        held.insert(key);
        auto running = m_noticed.find(key);
        if (running == m_noticed.end()) {
            Running started;
            started.title = first_line(notice.text);
            started.id    = m_sink.begin(work_kind(notice.kind), started.title, runs_on_job_worker(notice.kind));
            m_noticed.emplace(key, std::move(started));
        } else if (first_line(notice.text) != running->second.title) {
            // The same notification with new words: the step the work is on.
            m_sink.update(running->second.id, 0, 0, first_line(notice.text));
        }
    }
    for (auto running = m_noticed.begin(); running != m_noticed.end();) {
        if (held.count(running->first) != 0) {
            ++running;
            continue;
        }
        m_sink.finish(running->second.id, running->second.cancel_pressed ? "cancelled" : "done", {});
        running = m_noticed.erase(running);
    }
}

void WorkFeed::cancel(const std::string& id)
{
    if (id == m_slice.id && !id.empty()) {
        m_slice.cancel_pressed = true;
        m_plater.cancel_slicing(); // what the header's own Cancel calls
        return;
    }
    for (auto& running : m_noticed)
        if (running.second.id == id) {
            running.second.cancel_pressed = true;
            m_plater.get_ui_job_worker().cancel();
        }
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
