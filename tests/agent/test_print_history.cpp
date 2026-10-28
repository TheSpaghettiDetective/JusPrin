// The Print history screen's entries and the counts the Home cards and the
// project pane show: one table of G-codes, filtered by project or printer.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/PrintHistory.hpp"

#include <nlohmann/json.hpp>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Home;
using Agent::GcodeDestination;
using Agent::GcodePrint;
using Agent::GcodeRecord;

namespace {

GcodeRecord gcode(const std::string& hash, const std::string& created_at, const std::string& printer = "Bambu X1C preset")
{
    GcodeRecord record;
    record.hash       = hash;
    record.created_at = created_at;
    record.plate_name = "Plate 1";
    record.printer    = printer;
    return record;
}

GcodeDestination& add_export(GcodeRecord& record, const std::string& path)
{
    GcodeDestination exported;
    exported.id   = "d-" + std::to_string(record.destinations.size() + 1);
    exported.seq  = record.destinations.size() + 1;
    exported.kind = GcodeDestination::Kind::Export;
    exported.at   = record.created_at;
    exported.path = path;
    record.destinations.push_back(exported);
    return record.destinations.back();
}

GcodeDestination& add_send(GcodeRecord& record, const std::string& printer_id, const std::string& printer_name)
{
    GcodeDestination send;
    send.id              = "d-" + std::to_string(record.destinations.size() + 1);
    send.seq             = record.destinations.size() + 1;
    send.kind            = GcodeDestination::Kind::Send;
    send.at              = record.created_at;
    send.printer_id      = printer_id;
    send.printer_name    = printer_name;
    send.transport       = "bambu_print";
    send.start_requested = true;
    send.delivered_at    = record.created_at;
    record.destinations.push_back(send);
    return record.destinations.back();
}

void add_print(GcodeRecord& record, const std::string& printer_id, const std::string& job, const std::string& finished = {})
{
    GcodePrint print;
    print.id           = "p-" + std::to_string(record.prints.size() + 1);
    print.printer_id   = printer_id;
    print.printer_name = "Bambu X1C";
    print.job_id       = job;
    print.started_at   = record.created_at;
    print.finished_at  = finished;
    record.prints.push_back(print);
}

// The X1C is a named printer standing for device 01S; the A1 is a device no
// named printer stands for.
HistoryFacts facts()
{
    HistoryFacts facts;
    facts.printer_label  = [](const std::string& preset) { return preset == "Bambu X1C preset" ? "X1 Carbon" : preset; };
    facts.material_label = [](const std::string& preset) { return preset; };
    facts.process_label  = [](const std::string& preset) { return preset; };
    facts.card_for_preset = [](const std::string& preset) {
        return preset == "Bambu X1C preset" ? std::string("named:Bambu X1C") : std::string();
    };
    facts.card_for_printer = [](const std::string& id, const std::string&) {
        return id == "01S" ? std::string("named:Bambu X1C") : id.empty() ? std::string() : "device:" + id;
    };
    facts.check_export = [](const GcodeDestination& exported) {
        return exported.path.find("gone") != std::string::npos ? std::string("missing") : std::string("matches");
    };
    facts.kept = [](const std::string&, const std::string& hash) { return hash != "old"; };
    facts.print_again = [](const std::string&, const GcodeRecord& record) {
        return record.hash == "old" ? std::string("not_kept") : std::string("ready");
    };
    return facts;
}

} // namespace

TEST_CASE("a project's summary counts what went out and what printed", "[home][history]")
{
    GcodeRecord first = gcode("aaaa", "2026-10-20T10:00:00Z");
    add_export(first, "C:/Prints/a.gcode");
    add_send(first, "01S", "Bambu X1C");
    add_print(first, "01S", "job-1", "2026-10-20T12:00:00Z");
    GcodeRecord second = gcode("bbbb", "2026-10-21T10:00:00Z");
    add_send(second, "01S", "Bambu X1C");
    add_print(second, "01S", "job-2");

    const HistorySummary summary = summarize({first, second}, {{"01S", "job-2", 38, 5700}});
    CHECK(summary.gcodes == 2);
    CHECK(summary.exports == 1);
    CHECK(summary.sends == 2);
    CHECK(summary.prints == 2);
    CHECK(summary.printing_now == 1);
    CHECK(summary.last_finished_at == "2026-10-20T12:00:00Z");
}

TEST_CASE("an unfinished print is printing only while its printer runs that job", "[home][history]")
{
    GcodeRecord record = gcode("aaaa", "2026-10-20T10:00:00Z");
    add_send(record, "01S", "Bambu X1C");
    add_print(record, "01S", "job-1");
    // The printer moved on to another job: the print stopped without a
    // finish, which is not a failure the history records.
    CHECK(summarize({record}, {{"01S", "job-9"}}).printing_now == 0);
    CHECK(summarize({record}, {}).printing_now == 0);
    CHECK(summarize({record}, {{"01S", "job-1"}}).printing_now == 1);
}

TEST_CASE("a finished print is never live, whatever the printer reports", "[home][history]")
{
    GcodePrint print;
    print.printer_id  = "01S";
    print.job_id      = "job-1";
    print.finished_at = "2026-10-20T12:00:00Z";
    CHECK(live_print(print, {{"01S", "job-1"}}) == nullptr);
}

TEST_CASE("a printer's summary takes the G-codes made for it and the prints it ran", "[home][history]")
{
    // Made for the X1C and sent to the A1: it belongs to both cards, and its
    // print is the A1's.
    GcodeRecord moved = gcode("aaaa", "2026-10-20T10:00:00Z");
    add_send(moved, "02A", "Bambu A1");
    add_print(moved, "02A", "job-1", "2026-10-20T12:00:00Z");
    // Made for the X1C and only exported.
    GcodeRecord exported = gcode("bbbb", "2026-10-21T10:00:00Z");
    add_export(exported, "C:/Prints/b.gcode");
    // Another printer's profile entirely.
    GcodeRecord other = gcode("cccc", "2026-10-22T10:00:00Z", "Prusa MK4");
    add_export(other, "C:/Prints/c.gcode");
    const std::vector<HistoryProject> projects{{"p1", "Bracket", {moved, exported}}, {"p2", "Vent", {other}}};

    const HistorySummary x1c = summarize_printer(projects, "named:Bambu X1C", facts(), {});
    CHECK(x1c.gcodes == 2);
    CHECK(x1c.prints == 0);
    CHECK(x1c.sends == 0);
    const HistorySummary a1 = summarize_printer(projects, "device:02A", facts(), {});
    CHECK(a1.gcodes == 1);
    CHECK(a1.sends == 1);
    CHECK(a1.prints == 1);
    CHECK(a1.last_finished_at == "2026-10-20T12:00:00Z");
}

TEST_CASE("the screen lists every project's G-codes newest first", "[home][history]")
{
    GcodeRecord older = gcode("aaaa", "2026-10-20T10:00:00Z");
    add_export(older, "C:/Prints/bracket.gcode");
    GcodeRecord newer = gcode("bbbb", "2026-10-22T10:00:00Z", "Prusa MK4");
    add_send(newer, "02A", "Bambu A1");
    const nlohmann::json entries =
        history_entries({{"p1", "Bracket", {older}}, {"p2", "Vent", {newer}}}, facts(), {});

    REQUIRE(entries.size() == 2);
    CHECK(entries[0]["hash"] == "bbbb");
    CHECK(entries[0]["projectId"] == "p2");
    CHECK(entries[0]["projectName"] == "Vent");
    CHECK(entries[1]["hash"] == "aaaa");
    // The name is the exported file, else the plate.
    CHECK(entries[1]["name"] == "bracket.gcode");
    CHECK(entries[0]["name"] == "Plate 1");
}

TEST_CASE("an entry carries its rows, its cards and where it went out in the chat", "[home][history]")
{
    GcodeRecord record = gcode("aaaa", "2026-10-20T10:00:00Z");
    record.statistics.print_time_seconds = 9240;
    record.statistics.material_grams     = 69;
    record.statistics.layer_count        = 181;
    GcodeDestination& exported = add_export(record, "C:/Prints/bracket.gcode");
    exported.conversation_id   = "chat-1";
    add_export(record, "E:/gone.gcode");
    GcodeDestination& sent = add_send(record, "01S", "Bambu X1C");
    sent.delivered_at.clear();
    add_print(record, "01S", "job-1");
    const nlohmann::json entry = history_entries({{"p1", "Bracket", {record}}}, facts(), {{"01S", "job-1", 38, 5700}})[0];

    CHECK(entry["printer"] == "X1 Carbon");
    CHECK(entry["printTimeSeconds"] == 9240);
    CHECK(entry["grams"] == 69);
    CHECK(entry["layers"] == 181);
    CHECK(entry["kept"] == true);
    CHECK(entry["printAgain"] == "ready");
    CHECK(entry["conversationId"] == "chat-1");
    CHECK(entry["markerId"] == "d-1");
    CHECK(entry["printerIds"] == nlohmann::json::array({"named:Bambu X1C"}));

    const nlohmann::json& rows = entry["destinations"];
    REQUIRE(rows.size() == 3);
    CHECK(rows[0]["kind"] == "export");
    CHECK(rows[0]["check"] == "matches");
    CHECK(rows[1]["check"] == "missing");
    CHECK(rows[2]["kind"] == "send");
    CHECK(rows[2]["delivered"] == false);
    CHECK(rows[2]["startRequested"] == true);
    CHECK(rows[2]["printerId"] == "named:Bambu X1C");

    const nlohmann::json& prints = entry["prints"];
    REQUIRE(prints.size() == 1);
    CHECK_FALSE(prints[0].contains("finishedAt"));
    CHECK(prints[0]["live"]["percent"] == 38);
    CHECK(prints[0]["live"]["remainingSeconds"] == 5700);
}

TEST_CASE("an entry whose copy is gone says so, and one with no chat has no link", "[home][history]")
{
    GcodeRecord record = gcode("old", "2026-10-20T10:00:00Z");
    add_export(record, "C:/Prints/old.gcode");
    const nlohmann::json entry = history_entries({{"p1", "Bracket", {record}}}, facts(), {})[0];
    CHECK(entry["kept"] == false);
    CHECK(entry["printAgain"] == "not_kept");
    CHECK_FALSE(entry.contains("conversationId"));
    CHECK_FALSE(entry.contains("markerId"));
}
