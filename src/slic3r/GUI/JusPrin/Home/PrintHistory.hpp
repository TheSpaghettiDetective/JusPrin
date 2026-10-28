#pragma once

// The Print history screen and the lines that lead to it, from the G-code
// records every project keeps. Pure: the records, what the printers are doing
// now and the few facts only the application can name go in, the page's JSON
// and the counts come out. Printer and project histories are one table: a
// filter over the same entries, so the two views cannot disagree.
//
// The page phrases every line; nothing here is wording.

#include "HomeSnapshot.hpp"
#include "slic3r/GUI/JusPrin/Agent/ManufacturingHistory.hpp"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

// A job a printer is running now, as it reports it.
struct LivePrint
{
    std::string printer_id; // a Bambu device id
    std::string job_key;    // as GcodePrint::job_id records it
    int         percent{-1};
    int         remaining_seconds{0};
};

// One project's records.
struct HistoryProject
{
    std::string                     id;
    std::string                     name;
    std::vector<Agent::GcodeRecord> records;
};

// The job `print` ran as, when its printer is running it now.
const LivePrint* live_print(const Agent::GcodePrint& print, const std::vector<LivePrint>& live);

// A project's history.
HistorySummary summarize(const std::vector<Agent::GcodeRecord>& records, const std::vector<LivePrint>& live);

// What the application knows that the records do not.
struct HistoryFacts
{
    // A preset as the person sees it: "X1 Carbon", "Bambu PLA Basic".
    std::function<std::string(const std::string& preset)> printer_label, material_label, process_label;
    // The Home printer card a record's printer preset, or a send or print's
    // printer, stands for; empty when no card does.
    std::function<std::string(const std::string& printer_preset)>                      card_for_preset;
    std::function<std::string(const std::string& printer_id, const std::string& name)> card_for_printer;
    // Whether an exported file still holds what was written: "matches",
    // "changed" or "missing".
    std::function<std::string(const Agent::GcodeDestination& exported)> check_export;
    // Whether the project still keeps an intact copy of the G-code.
    std::function<bool(const std::string& project_id, const std::string& hash)> kept;
    // Whether the G-code can be printed again on the printer selected now:
    // "ready", "not_kept", "not_for_this_printer", "no_printer" or "changed".
    std::function<std::string(const std::string& project_id, const Agent::GcodeRecord& record)> print_again;
};

// One printer card's part of the history: the G-codes made for it or sent to
// it, and the prints it ran.
HistorySummary summarize_printer(const std::vector<HistoryProject>& projects, const std::string& card_id,
                                 const HistoryFacts& facts, const std::vector<LivePrint>& live);

// The screen's entries, one per G-code, newest first. Each carries the cards
// it belongs to (`printerIds`), so the page filters by project or printer
// without asking again.
nlohmann::json history_entries(const std::vector<HistoryProject>& projects, const HistoryFacts& facts,
                               const std::vector<LivePrint>& live);

}}}} // namespace Slic3r::GUI::JusPrin::Home
