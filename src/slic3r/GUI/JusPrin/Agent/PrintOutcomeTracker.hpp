#pragma once

// Turns what printers report into the two print facts a G-code record keeps:
// a print started from one of its sends, and that print finished. Nothing
// else is written: a printer reports a person's stop and a failure the same
// way, so a print that ends any other way is simply no longer followed.
//
// GUI-free. The shell polls the printers and applies the writes; the tracker
// only decides which report belongs to which send.

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class PrintOutcomeTracker
{
public:
    using Clock = std::chrono::steady_clock;

    enum class PrinterState { Printing, Finished, Other };

    struct PrinterReport
    {
        std::string  printer_id;
        PrinterState state{PrinterState::Other};
        // What tells one job from the next on this printer: the printer's job
        // id where it reports one, else the file it is printing.
        std::string  job_key;
    };

    // A send that asked the printer to start, waiting for it to start.
    struct AwaitedSend
    {
        std::string project_id;
        std::string hash;
        std::string destination_id;
        std::string printer_id;
        std::string printer_name;
        // The printer's job when the send was made: a job still printing then
        // is not this send's.
        PrinterState state_at_send{PrinterState::Other};
        std::string  job_key_at_send;
    };

    // A print the record holds, not yet finished.
    struct FollowedPrint
    {
        std::string project_id;
        std::string hash;
        std::string print_id;
        std::string printer_id;
        std::string job_key;
    };

    struct Writer
    {
        // Records the print and returns its id.
        std::function<std::string(const AwaitedSend&, const std::string& job_key)> start;
        std::function<void(const FollowedPrint&)>                                  finish;
    };

    // A send no printer started within this long is no longer awaited.
    static constexpr Clock::duration kStartWindow = std::chrono::minutes(30);

    void await(AwaitedSend send, Clock::time_point now);
    void follow(FollowedPrint print);
    // One poll of every printer that reported this time.
    void observe(const std::vector<PrinterReport>& reports, const Writer& writer, Clock::time_point now);

    bool idle() const { return m_awaited.empty() && m_followed.empty(); }

private:
    struct Awaited
    {
        AwaitedSend       send;
        Clock::time_point since;
    };
    std::vector<Awaited>       m_awaited;
    std::vector<FollowedPrint> m_followed;
};

} // namespace Slic3r::GUI::JusPrin::Agent
