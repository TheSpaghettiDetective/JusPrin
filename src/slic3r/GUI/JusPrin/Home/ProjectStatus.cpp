#include "ProjectStatus.hpp"

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

LedgerStatus ledger_status(const std::vector<Agent::SentRecord>&          sent,
                           const std::vector<Agent::PhysicalPrintRecord>& prints)
{
    const Agent::SentRecord* latest_sent = nullptr;
    for (const Agent::SentRecord& event : sent)
        if (latest_sent == nullptr || event.seq > latest_sent->seq)
            latest_sent = &event;

    const Agent::PhysicalPrintRecord* latest_print = nullptr;
    for (const Agent::PhysicalPrintRecord& print : prints)
        if (print.outcome == "completed" && (latest_print == nullptr || print.seq > latest_print->seq))
            latest_print = &print;

    if (latest_print != nullptr && (latest_sent == nullptr || latest_print->seq > latest_sent->seq))
        return {ProjectStatusKind::Completed, latest_print->printer, latest_print->material, latest_print->ended_at};
    if (latest_sent != nullptr)
        return {ProjectStatusKind::Sent, latest_sent->printer, latest_sent->material, latest_sent->sent_at};
    return {};
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
