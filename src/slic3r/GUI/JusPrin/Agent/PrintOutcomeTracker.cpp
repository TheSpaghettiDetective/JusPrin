#include "PrintOutcomeTracker.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::Agent {

void PrintOutcomeTracker::await(AwaitedSend send, Clock::time_point now)
{
    // A newer send to the same printer is the one its next job runs.
    m_awaited.erase(std::remove_if(m_awaited.begin(), m_awaited.end(),
                                   [&send](const Awaited& awaited) { return awaited.send.printer_id == send.printer_id; }),
                    m_awaited.end());
    m_awaited.push_back({std::move(send), now});
}

void PrintOutcomeTracker::follow(FollowedPrint print)
{
    if (std::none_of(m_followed.begin(), m_followed.end(), [&print](const FollowedPrint& followed) {
            return followed.hash == print.hash && followed.print_id == print.print_id;
        }))
        m_followed.push_back(std::move(print));
}

void PrintOutcomeTracker::observe(const std::vector<PrinterReport>& reports, const Writer& writer, Clock::time_point now)
{
    for (const PrinterReport& report : reports) {
        // A followed print finishes, or stops being this printer's job.
        for (auto followed = m_followed.begin(); followed != m_followed.end();) {
            if (followed->printer_id != report.printer_id) {
                ++followed;
                continue;
            }
            const bool same_job = report.job_key == followed->job_key;
            if (same_job && report.state == PrinterState::Printing) {
                ++followed;
                continue;
            }
            if (same_job && report.state == PrinterState::Finished)
                writer.finish(*followed);
            // Any other report (stopped, failed, idle, another job) ends it
            // without a word: there is nothing reliable to record.
            followed = m_followed.erase(followed);
        }

        // An awaited send starts when its printer begins a job it was not
        // already running when the send was made.
        for (auto awaited = m_awaited.begin(); awaited != m_awaited.end();) {
            const AwaitedSend& send = awaited->send;
            if (send.printer_id != report.printer_id || report.state != PrinterState::Printing ||
                (send.state_at_send == PrinterState::Printing && report.job_key == send.job_key_at_send)) {
                ++awaited;
                continue;
            }
            FollowedPrint print{send.project_id, send.hash, writer.start(send, report.job_key), send.printer_id, report.job_key};
            awaited = m_awaited.erase(awaited);
            follow(std::move(print));
        }
    }
    m_awaited.erase(std::remove_if(m_awaited.begin(), m_awaited.end(),
                                   [now](const Awaited& awaited) { return now - awaited.since > kStartWindow; }),
                    m_awaited.end());
}

} // namespace Slic3r::GUI::JusPrin::Agent
