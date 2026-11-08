#include "ProjectStatus.hpp"

#include <algorithm>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

LedgerStatus ledger_status(const std::vector<Agent::GcodeRecord>& records)
{
    // The records write UTC with a trailing Z in one format, so their times
    // order as text.
    LedgerStatus latest_send, latest_finish, latest_export;
    for (const Agent::GcodeRecord& record : records) {
        const bool sent = std::any_of(record.destinations.begin(), record.destinations.end(), [](const Agent::GcodeDestination& destination) {
            return destination.kind == Agent::GcodeDestination::Kind::Send;
        });
        for (const Agent::GcodeDestination& destination : record.destinations) {
            if (destination.kind == Agent::GcodeDestination::Kind::Send && destination.at > latest_send.at)
                latest_send = {ProjectStatusKind::Sent, record.printer, record.material, destination.at};
            else if (!sent && destination.kind == Agent::GcodeDestination::Kind::Export && destination.at > latest_export.at)
                latest_export = {ProjectStatusKind::Exported, record.printer, record.material, destination.at};
        }
        for (const Agent::GcodePrint& print : record.prints)
            if (!print.finished_at.empty() && print.finished_at > latest_finish.at)
                latest_finish = {ProjectStatusKind::Completed, record.printer, record.material, print.finished_at};
    }
    // A kind left Draft found nothing. On equal times the earlier candidate
    // stays: a send beats a print that ended the same second.
    LedgerStatus newest;
    for (const LedgerStatus* found : {&latest_send, &latest_finish, &latest_export})
        if (found->kind != ProjectStatusKind::Draft && (newest.kind == ProjectStatusKind::Draft || found->at > newest.at))
            newest = *found;
    return newest;
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
