#include "PrintHistory.hpp"

#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/CloudAccount.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <set>
#include <stdexcept>
#include <string>

#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

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
    if (record.report)
        out["report"] = json{{"sentAt", record.report->sent_at}, {"credits", record.report->credits}};
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

bool report_due(const std::vector<GcodeRecord>& records, const std::string& finished_since, const std::vector<LivePrint>& live)
{
    const auto latest = std::max_element(records.begin(), records.end(),
                                         [](const GcodeRecord& a, const GcodeRecord& b) { return a.seq < b.seq; });
    if (latest == records.end() || latest->report)
        return false;
    bool finished = false;
    for (const GcodePrint& print : latest->prints) {
        if (live_print(print, live) != nullptr)
            return false;
        finished = finished || (!print.finished_at.empty() && print.finished_at >= finished_since);
    }
    return finished;
}

std::optional<ExportedFile> exported_file(const std::filesystem::path& path)
{
    // A drive that is not plugged in reads as a path that is not there; any
    // other failure, such as a folder the person may not read, is not "gone".
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
        return std::nullopt;
    if (error)
        throw std::filesystem::filesystem_error("Reading an exported G-code file", path, error);
    return ExportedFile{size, std::filesystem::last_write_time(path)};
}

namespace {

// The server's limit on one report photo: JUSPRIN_V2_REPORT_MAX_PHOTO_BYTES in
// tsd-enterprise's settings_ent.py. The app uploads the file as it is on disk.
constexpr std::uint64_t kReportPhotoMaxBytes = 20u * 1024 * 1024;

std::string photo_too_large()
{
    // TRN The print report form, about a photo over the server's limit of 20 MB.
    return translated(L("That photo is larger than 20 MB. Choose a smaller photo, then send the report again."));
}

} // namespace

std::string report_photo_problem(std::uint64_t size_bytes)
{
    return size_bytes > kReportPhotoMaxBytes ? photo_too_large() : std::string();
}

PrintReportAnswer print_report_answer(unsigned status, const std::string& body)
{
    PrintReportAnswer answer;
    const json parsed = json::parse(body, nullptr, false);
    const std::string code = parsed.is_object() && parsed.contains("error") && parsed["error"].is_object() ?
                                 parsed["error"].value("code", std::string()) : std::string();
    if (status == 201) {
        // Stored; an answer the app cannot read is our server's fault.
        if (!parsed.is_object() || !parsed.contains("credits") || !parsed["credits"].is_number_integer())
            throw std::runtime_error("The print report answered HTTP 201: " + body);
        answer.state   = PrintReportAnswer::State::Sent;
        answer.credits = parsed["credits"].get<int>();
    } else if (status == 409 && code == "already_reported") {
        // Sent from this account before, from another computer or another copy
        // of the project: the entry says sent, with nothing to add.
        answer.state = PrintReportAnswer::State::AlreadySent;
    } else if (const std::optional<Agent::AgentError> outdated = Agent::update_required(status, body)) {
        answer.update_message = outdated->message;
    } else if (code == "unauthorized") {
        answer.message = Agent::sign_in_ended();
    } else if (code == "photo_unreadable") {
        // The server's sentence for this refusal (errors.py EXPECTED), in the app's language.
        answer.message = translated(L("That photo could not be read."));
    } else if (code == "request_too_large") {
        // The form refuses such a photo when it is chosen; this covers one
        // that gets through.
        answer.message = photo_too_large();
    } else if (status != 0) {
        // No answer keeps the form's own sentence. Every other answer is
        // unexpected, including a short text or a missing photo: the form
        // sends neither.
        throw std::runtime_error("The print report answered HTTP " + std::to_string(status) + ": " + body);
    }
    return answer;
}

json report_facts(const std::string& project_name, const GcodeRecord& record, const HistoryFacts& facts)
{
    json out{{"project", project_name},
             {"plate", record.plate_name},
             {"manufacturing_input_hash", record.manufacturing_input_hash},
             {"print_time_seconds", record.statistics.print_time_seconds},
             {"material_grams", record.statistics.material_grams},
             {"layer_count", record.statistics.layer_count}};
    set_if_present(out, "printer", facts.printer_label(record.printer));
    set_if_present(out, "material", facts.material_label(record.material));
    set_if_present(out, "process", facts.process_label(record.process));
    return out;
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
