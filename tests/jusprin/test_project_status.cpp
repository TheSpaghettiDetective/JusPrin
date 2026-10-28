// The rule behind a Home project card's status line: which of a project's
// sends and finished prints it reports.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/ProjectStatus.hpp"

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Home;

namespace {

Agent::GcodeRecord a_gcode(const std::string& printer, const std::string& material)
{
    Agent::GcodeRecord record;
    record.hash     = printer + material;
    record.printer  = printer;
    record.material = material;
    return record;
}

void send_at(Agent::GcodeRecord& record, const std::string& at)
{
    Agent::GcodeDestination send;
    send.id   = "d-" + std::to_string(record.destinations.size() + 1);
    send.kind = Agent::GcodeDestination::Kind::Send;
    send.at   = at;
    record.destinations.push_back(send);
}

void print_at(Agent::GcodeRecord& record, const std::string& started, const std::string& finished)
{
    Agent::GcodePrint print;
    print.started_at  = started;
    print.finished_at = finished;
    record.prints.push_back(print);
}

} // namespace

TEST_CASE("a project nothing was sent from is a draft", "[home]")
{
    const LedgerStatus status = ledger_status({});
    CHECK(status.kind == ProjectStatusKind::Draft);
    CHECK(status.printer.empty());
    CHECK(status.material.empty());
    CHECK(status.at.empty());
}

TEST_CASE("an export alone leaves the project a draft", "[home]")
{
    Agent::GcodeRecord record = a_gcode("Bambu Lab X1 Carbon 0.4 nozzle", "Bambu PLA Basic @BBL X1C");
    Agent::GcodeDestination exported;
    exported.kind = Agent::GcodeDestination::Kind::Export;
    exported.at   = "2026-10-20T10:00:00Z";
    record.destinations.push_back(exported);
    CHECK(ledger_status({record}).kind == ProjectStatusKind::Draft);
}

TEST_CASE("a sent plate reads Sent, with when and to which printer", "[home]")
{
    Agent::GcodeRecord record = a_gcode("Bambu Lab X1 Carbon 0.4 nozzle", "Bambu PLA Basic @BBL X1C");
    send_at(record, "2026-10-20T10:00:00Z");
    const LedgerStatus status = ledger_status({record});
    CHECK(status.kind == ProjectStatusKind::Sent);
    CHECK(status.at == "2026-10-20T10:00:00Z");
    CHECK(status.printer == "Bambu Lab X1 Carbon 0.4 nozzle");
    CHECK(status.material == "Bambu PLA Basic @BBL X1C");
}

TEST_CASE("the newest send names the line", "[home]")
{
    Agent::GcodeRecord older = a_gcode("Bambu Lab X1 Carbon 0.4 nozzle", "PLA");
    send_at(older, "2026-10-20T10:00:00Z");
    Agent::GcodeRecord newer = a_gcode("Bambu Lab A1 mini 0.4 nozzle", "PLA");
    send_at(newer, "2026-10-21T09:00:00Z");
    // Listed newest first, to show the order of the list is not what decides.
    const LedgerStatus status = ledger_status({newer, older});
    CHECK(status.printer == "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(status.at == "2026-10-21T09:00:00Z");
}

TEST_CASE("a finished print makes the project printed", "[home]")
{
    Agent::GcodeRecord record = a_gcode("Bambu Lab A1 0.4 nozzle", "Bambu PETG HF @BBL A1");
    send_at(record, "2026-10-20T10:00:00Z");
    print_at(record, "2026-10-20T10:05:00Z", "2026-10-20T11:30:00Z");
    const LedgerStatus status = ledger_status({record});
    CHECK(status.kind == ProjectStatusKind::Completed);
    CHECK(status.at == "2026-10-20T11:30:00Z");
    CHECK(status.printer == "Bambu Lab A1 0.4 nozzle");
    CHECK(status.material == "Bambu PETG HF @BBL A1");
}

TEST_CASE("a print that never finished leaves the project as sent", "[home]")
{
    Agent::GcodeRecord record = a_gcode("Bambu Lab A1 0.4 nozzle", "PLA");
    send_at(record, "2026-10-20T10:00:00Z");
    print_at(record, "2026-10-20T10:05:00Z", "");
    CHECK(ledger_status({record}).kind == ProjectStatusKind::Sent);
}

TEST_CASE("sending again after a print reads Sent", "[home]")
{
    Agent::GcodeRecord record = a_gcode("Bambu Lab A1 0.4 nozzle", "PLA");
    send_at(record, "2026-10-20T10:00:00Z");
    print_at(record, "2026-10-20T10:05:00Z", "2026-10-20T11:30:00Z");
    send_at(record, "2026-10-22T08:00:00Z");
    const LedgerStatus status = ledger_status({record});
    CHECK(status.kind == ProjectStatusKind::Sent);
    CHECK(status.at == "2026-10-22T08:00:00Z");
}
