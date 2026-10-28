#include "PrintHistory.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

using nlohmann::json;
using Agent::GcodeDestination;
using Agent::GcodePrint;
using Agent::GcodeRecord;

const LivePrint* live_print(const GcodePrint& print, const std::vector<LivePrint>& live)
{
    if (!print.finished_at.empty())
        return nullptr;
    const auto found = std::find_if(live.begin(), live.end(), [&print](const LivePrint& job) {
        return job.printer_id == print.printer_id && job.job_key == print.job_id;
    });
    return found == live.end() ? nullptr : &*found;
}

namespace {

void count_print(HistorySummary& summary, const GcodePrint& print, const std::vector<LivePrint>& live)
{
    ++summary.prints;
    if (live_print(print, live) != nullptr)
        ++summary.printing_now;
    // The records write UTC with a trailing Z in one format, so their times
    // order as text.
    if (print.finished_at > summary.last_finished_at)
        summary.last_finished_at = print.finished_at;
}

void count_destinations(HistorySummary& summary, const GcodeRecord& record)
{
    for (const GcodeDestination& destination : record.destinations)
        ++(destination.kind == GcodeDestination::Kind::Export ? summary.exports : summary.sends);
}

// Every printer card the record belongs to: the one its printer preset stands
// for, and each one it was sent to or printed on.
std::set<std::string> cards_of(const GcodeRecord& record, const HistoryFacts& facts)
{
    std::set<std::string> cards;
    const auto add = [&cards](std::string card) {
        if (!card.empty())
            cards.insert(std::move(card));
    };
    add(facts.card_for_preset(record.printer));
    for (const GcodeDestination& destination : record.destinations)
        if (destination.kind == GcodeDestination::Kind::Send)
            add(facts.card_for_printer(destination.printer_id, destination.printer_name));
    for (const GcodePrint& print : record.prints)
        add(facts.card_for_printer(print.printer_id, print.printer_name));
    return cards;
}

void set_if_present(json& target, const char* key, const std::string& value)
{
    if (!value.empty())
        target[key] = value;
}

json destination_json(const GcodeDestination& destination, const HistoryFacts& facts)
{
    json out{{"id", destination.id}, {"at", destination.at}};
    if (destination.kind == GcodeDestination::Kind::Export) {
        out["kind"]  = "export";
        out["path"]  = destination.path;
        out["check"] = facts.check_export(destination);
        return out;
    }
    out["kind"]           = "send";
    out["printerName"]    = destination.printer_name;
    out["delivered"]      = !destination.delivered_at.empty();
    out["startRequested"] = destination.start_requested;
    set_if_present(out, "printerId", facts.card_for_printer(destination.printer_id, destination.printer_name));
    return out;
}

json print_json(const GcodePrint& print, const HistoryFacts& facts, const std::vector<LivePrint>& live)
{
    json out{{"id", print.id}, {"printerName", print.printer_name}, {"startedAt", print.started_at}};
    set_if_present(out, "printerId", facts.card_for_printer(print.printer_id, print.printer_name));
    set_if_present(out, "finishedAt", print.finished_at);
    if (const LivePrint* job = live_print(print, live)) {
        json now = json::object();
        if (job->percent >= 0)
            now["percent"] = job->percent;
        if (job->remaining_seconds > 0)
            now["remainingSeconds"] = job->remaining_seconds;
        out["live"] = std::move(now);
    }
    return out;
}

json entry_json(const HistoryProject& project, const GcodeRecord& record, const HistoryFacts& facts,
                const std::vector<LivePrint>& live)
{
    json out{{"projectId", project.id},
             {"projectName", project.name},
             {"hash", record.hash},
             {"name", Agent::gcode_display_name(record)},
             {"plateName", record.plate_name},
             {"createdAt", record.created_at},
             {"kept", facts.kept(project.id, record.hash)},
             {"printAgain", facts.print_again(project.id, record)}};
    const Agent::SliceStatistics& statistics = record.statistics;
    if (statistics.print_time_seconds > 0.0)
        out["printTimeSeconds"] = statistics.print_time_seconds;
    if (statistics.material_grams > 0.0)
        out["grams"] = statistics.material_grams;
    if (statistics.layer_count > 0)
        out["layers"] = statistics.layer_count;
    set_if_present(out, "printer", facts.printer_label(record.printer));
    set_if_present(out, "material", facts.material_label(record.material));
    set_if_present(out, "process", facts.process_label(record.process));

    // Open in chat goes to the marker of where the G-code first went out.
    const auto first = std::min_element(record.destinations.begin(), record.destinations.end(),
                                        [](const GcodeDestination& a, const GcodeDestination& b) { return a.seq < b.seq; });
    if (first != record.destinations.end() && !first->conversation_id.empty()) {
        out["conversationId"] = first->conversation_id;
        out["markerId"]       = first->id;
    }

    json cards = json::array();
    for (const std::string& card : cards_of(record, facts))
        cards.push_back(card);
    out["printerIds"] = std::move(cards);

    json destinations = json::array();
    for (const GcodeDestination& destination : record.destinations)
        destinations.push_back(destination_json(destination, facts));
    out["destinations"] = std::move(destinations);
    json prints = json::array();
    for (const GcodePrint& print : record.prints)
        prints.push_back(print_json(print, facts, live));
    out["prints"] = std::move(prints);
    return out;
}

} // namespace

HistorySummary summarize(const std::vector<GcodeRecord>& records, const std::vector<LivePrint>& live)
{
    HistorySummary summary;
    summary.gcodes = records.size();
    for (const GcodeRecord& record : records) {
        count_destinations(summary, record);
        for (const GcodePrint& print : record.prints)
            count_print(summary, print, live);
    }
    return summary;
}

HistorySummary summarize_printer(const std::vector<HistoryProject>& projects, const std::string& card_id,
                                 const HistoryFacts& facts, const std::vector<LivePrint>& live)
{
    HistorySummary summary;
    for (const HistoryProject& project : projects)
        for (const GcodeRecord& record : project.records) {
            if (cards_of(record, facts).count(card_id) == 0)
                continue;
            ++summary.gcodes;
            for (const GcodeDestination& destination : record.destinations)
                if (destination.kind == GcodeDestination::Kind::Send &&
                    facts.card_for_printer(destination.printer_id, destination.printer_name) == card_id)
                    ++summary.sends;
            for (const GcodePrint& print : record.prints)
                if (facts.card_for_printer(print.printer_id, print.printer_name) == card_id)
                    count_print(summary, print, live);
        }
    return summary;
}

json history_entries(const std::vector<HistoryProject>& projects, const HistoryFacts& facts, const std::vector<LivePrint>& live)
{
    std::vector<std::pair<const HistoryProject*, const GcodeRecord*>> all;
    for (const HistoryProject& project : projects)
        for (const GcodeRecord& record : project.records)
            all.emplace_back(&project, &record);
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second->created_at > b.second->created_at; });
    json entries = json::array();
    for (const auto& [project, record] : all)
        entries.push_back(entry_json(*project, *record, facts, live));
    return entries;
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
