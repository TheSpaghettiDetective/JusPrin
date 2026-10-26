// The rule behind a Home project card's status line: which of a project's Sent
// and Printed events it reports.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/ProjectStatus.hpp"

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Home;

namespace {

Agent::SentRecord a_sent(const std::string& id, std::uint64_t seq, const std::string& sent_at)
{
    Agent::SentRecord sent;
    sent.id       = id;
    sent.seq      = seq;
    sent.sent_at  = sent_at;
    sent.printer  = "Bambu Lab X1 Carbon 0.4 nozzle";
    sent.material = "Bambu PLA Basic @BBL X1C";
    return sent;
}

Agent::PhysicalPrintRecord a_print(const std::string& sent_id, std::uint64_t seq, const std::string& outcome)
{
    Agent::PhysicalPrintRecord print;
    print.sent_id  = sent_id;
    print.seq      = seq;
    print.outcome  = outcome;
    print.ended_at = "2026-10-20T11:30:00Z";
    print.printer  = "Bambu Lab A1 0.4 nozzle";
    print.material = "Bambu PETG HF @BBL A1";
    return print;
}

} // namespace

TEST_CASE("a project nothing was sent from is a draft", "[home]")
{
    const LedgerStatus status = ledger_status({}, {});
    CHECK(status.kind == ProjectStatusKind::Draft);
    CHECK(status.printer.empty());
    CHECK(status.material.empty());
    CHECK(status.at.empty());
}

TEST_CASE("a sent plate reads Sent, with when and to which printer", "[home]")
{
    const LedgerStatus status = ledger_status({a_sent("s-1", 1, "2026-10-20T10:00:00Z")}, {});
    CHECK(status.kind == ProjectStatusKind::Sent);
    CHECK(status.at == "2026-10-20T10:00:00Z");
    CHECK(status.printer == "Bambu Lab X1 Carbon 0.4 nozzle");
    CHECK(status.material == "Bambu PLA Basic @BBL X1C");
}

TEST_CASE("the newest Sent event names the line", "[home]")
{
    Agent::SentRecord older = a_sent("s-1", 1, "2026-10-20T10:00:00Z");
    Agent::SentRecord newer = a_sent("s-2", 2, "2026-10-21T09:00:00Z");
    newer.printer           = "Bambu Lab A1 mini 0.4 nozzle";
    // Listed newest first, to show the order of the list is not what decides.
    const LedgerStatus status = ledger_status({newer, older}, {});
    CHECK(status.printer == "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(status.at == "2026-10-21T09:00:00Z");
}

TEST_CASE("a completed print makes the project printed, as the printer ran it", "[home]")
{
    const LedgerStatus status =
        ledger_status({a_sent("s-1", 1, "2026-10-20T10:00:00Z")}, {a_print("s-1", 2, "completed")});
    CHECK(status.kind == ProjectStatusKind::Completed);
    CHECK(status.at == "2026-10-20T11:30:00Z");
    CHECK(status.printer == "Bambu Lab A1 0.4 nozzle");
    CHECK(status.material == "Bambu PETG HF @BBL A1");
}

TEST_CASE("a failed or cancelled print leaves the project as sent", "[home]")
{
    const std::string outcome = GENERATE("failed", "cancelled");
    INFO(outcome);
    const LedgerStatus status =
        ledger_status({a_sent("s-1", 1, "2026-10-20T10:00:00Z")}, {a_print("s-1", 2, outcome)});
    CHECK(status.kind == ProjectStatusKind::Sent);
}

TEST_CASE("sending again after a print reads Sent", "[home]")
{
    const LedgerStatus status = ledger_status(
        {a_sent("s-1", 1, "2026-10-20T10:00:00Z"), a_sent("s-2", 3, "2026-10-22T08:00:00Z")},
        {a_print("s-1", 2, "completed")});
    CHECK(status.kind == ProjectStatusKind::Sent);
    CHECK(status.at == "2026-10-22T08:00:00Z");
}
