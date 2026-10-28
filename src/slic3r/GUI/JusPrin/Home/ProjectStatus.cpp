#include "ProjectStatus.hpp"

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

LedgerStatus ledger_status(const std::vector<Agent::GcodeRecord>& records)
{
    // The records write UTC with a trailing Z in one format, so their times
    // order as text.
    LedgerStatus latest_send, latest_finish;
    for (const Agent::GcodeRecord& record : records) {
        for (const Agent::GcodeDestination& destination : record.destinations)
            if (destination.kind == Agent::GcodeDestination::Kind::Send && destination.at > latest_send.at)
                latest_send = {ProjectStatusKind::Sent, record.printer, record.material, destination.at};
        for (const Agent::GcodePrint& print : record.prints)
            if (!print.finished_at.empty() && print.finished_at > latest_finish.at)
                latest_finish = {ProjectStatusKind::Completed, record.printer, record.material, print.finished_at};
    }
    if (latest_finish.kind == ProjectStatusKind::Completed &&
        (latest_send.kind != ProjectStatusKind::Sent || latest_finish.at > latest_send.at))
        return latest_finish;
    if (latest_send.kind == ProjectStatusKind::Sent)
        return latest_send;
    return {};
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
