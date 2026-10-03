#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Shell/ProjectPaneModel.hpp"

using namespace Slic3r::GUI::JusPrin;
using Slic3r::GUI::JusPrin::Agent::PhysicalPrintRecord;
using Slic3r::GUI::JusPrin::Workspace::ProjectAttachment;
using Slic3r::GUI::JusPrin::Workspace::ProjectDetails;

namespace {

PhysicalPrintRecord print(std::string id, std::string outcome, std::string started = "", std::string ended = "")
{
    PhysicalPrintRecord record;
    record.id         = std::move(id);
    record.outcome    = std::move(outcome);
    record.started_at = std::move(started);
    record.ended_at   = std::move(ended);
    return record;
}

} // namespace

TEST_CASE("A project with no model metadata has an empty details view", "[projectpane]")
{
    ProjectDetails details;
    details.backup_current = false; // unrelated state never counts as metadata
    CHECK(build_details_view(details).empty());
    CHECK(build_details_view(details).model.empty());
}

TEST_CASE("Only the metadata the file carries is present, and the title is never invented", "[projectpane]")
{
    ProjectDetails details;
    details.designer = "Ada";
    const ProjectDetailsView view = build_details_view(details);
    CHECK_FALSE(view.empty());
    CHECK(view.model.designer == "Ada");
    CHECK(view.model.title.empty());
    CHECK(view.model.license.empty());
}

TEST_CASE("Attachments group by Orca's category in the order they arrive", "[projectpane]")
{
    ProjectDetails details;
    details.attachments = {{"Metadata/a.png", "Model Pictures", 10}, {"Metadata/bom.csv", "Bill of Materials", 4},
                           {"Metadata/b.png", "Model Pictures", 12}};
    details.attachments_truncated = true;
    const ProjectDetailsView view = build_details_view(details);
    REQUIRE(view.attachments.size() == 2);
    CHECK(view.attachments[0].folder == "Model Pictures");
    CHECK(view.attachments[0].files.size() == 2);
    CHECK(view.attachments[1].folder == "Bill of Materials");
    CHECK(view.attachments_truncated);
    CHECK_FALSE(view.empty());
}

TEST_CASE("A duration needs two valid timestamps in order", "[projectpane]")
{
    CHECK(seconds_between("2026-08-30T00:00:00Z", "2026-08-30T01:02:03Z") == 3723);
    CHECK(seconds_between("2026-08-30T23:00:00Z", "2026-08-31T01:00:00Z") == 7200);
    CHECK(seconds_between("2026-02-28T12:00:00Z", "2026-03-01T12:00:00Z") == 86400);
    CHECK_FALSE(seconds_between("", "2026-08-30T01:02:03Z").has_value());
    CHECK_FALSE(seconds_between("2026-08-30T00:00:00Z", "").has_value());
    CHECK_FALSE(seconds_between("not a time", "2026-08-30T01:02:03Z").has_value());
    CHECK_FALSE(seconds_between("2026-08-30T01:02:03Z", "2026-08-30T00:00:00Z").has_value());
    CHECK_FALSE(seconds_between("2026-13-30T00:00:00Z", "2026-14-30T00:00:00Z").has_value());
    CHECK_FALSE(seconds_between("2026-02-30T00:00:00Z", "2026-03-01T00:00:00Z").has_value());
    CHECK_FALSE(seconds_between("2026-08-30T01:02:03Zjunk", "2026-08-30T01:02:04Z").has_value());
}

TEST_CASE("Print history keeps every outcome apart and lists the newest first", "[projectpane]")
{
    const auto view = build_print_history({print("a", "completed", "2026-08-01T00:00:00Z", "2026-08-01T02:00:00Z"),
                                           print("b", "failed", "2026-08-02T00:00:00Z", "2026-08-02T00:30:00Z"),
                                           print("c", "cancelled", "2026-08-03T00:00:00Z", "2026-08-03T00:10:00Z")});
    REQUIRE(view.detailed.size() == 3);
    CHECK(view.total == 3);
    CHECK(view.count_only == 0);
    CHECK(view.detailed[0].id == "c");
    CHECK(view.detailed[0].outcome == PrintOutcome::Cancelled);
    CHECK(view.detailed[1].outcome == PrintOutcome::Failed);
    CHECK(view.detailed[2].outcome == PrintOutcome::Completed);
    CHECK(view.detailed[2].duration_seconds == 7200);
}

TEST_CASE("A record with nothing to show is counted, not drawn", "[projectpane]")
{
    const auto view = build_print_history({print("old1", ""), print("old2", ""),
                                           print("new", "completed", "2026-08-01T00:00:00Z", "2026-08-01T01:00:00Z")});
    CHECK(view.total == 3);
    CHECK(view.count_only == 2);
    REQUIRE(view.detailed.size() == 1);
    CHECK(view.detailed[0].id == "new");
}

TEST_CASE("A sparse recorded fact is still a detailed print", "[projectpane]")
{
    PhysicalPrintRecord failure = print("failure", "");
    failure.failure = "Filament ran out";
    PhysicalPrintRecord estimate = print("estimate", "");
    estimate.statistics.material_cost = 2.5;
    const auto view = build_print_history({failure, estimate});
    CHECK(view.total == 2);
    CHECK(view.count_only == 0);
    REQUIRE(view.detailed.size() == 2);
    CHECK(view.detailed[0].estimated_cost == 2.5);
    CHECK(view.detailed[1].failure == "Filament ran out");
}

TEST_CASE("An unfamiliar recorded outcome is kept verbatim", "[projectpane]")
{
    const auto view = build_print_history({print("future", "paused")});
    REQUIRE(view.detailed.size() == 1);
    CHECK(view.detailed[0].outcome == PrintOutcome::Unrecorded);
    CHECK(view.detailed[0].recorded_outcome == "paused");
}

TEST_CASE("Missing fields stay missing instead of becoming zero", "[projectpane]")
{
    PhysicalPrintRecord record = print("a", "completed", "2026-08-01T00:00:00Z", "");
    record.statistics.material_cost = 0.0;
    const auto view = build_print_history({record});
    REQUIRE(view.detailed.size() == 1);
    const PrintHistoryEntry& entry = view.detailed[0];
    CHECK_FALSE(entry.duration_seconds.has_value());
    CHECK_FALSE(entry.estimated_seconds.has_value());
    CHECK_FALSE(entry.estimated_grams.has_value());
    CHECK_FALSE(entry.estimated_cost.has_value());
    CHECK_FALSE(entry.stopped_percent.has_value());
    CHECK(entry.plate_name.empty());
}

TEST_CASE("Slice estimates are kept as estimates, and an unrecorded outcome stays unrecorded", "[projectpane]")
{
    PhysicalPrintRecord record = print("a", "", "2026-08-01T00:00:00Z", "2026-08-01T01:00:00Z");
    record.statistics.print_time_seconds = 5400;
    record.statistics.material_grams     = 42.5;
    record.statistics.material_cost      = 3.2;
    record.stopped_percent               = 60;
    const auto view = build_print_history({record});
    REQUIRE(view.detailed.size() == 1);
    CHECK(view.detailed[0].outcome == PrintOutcome::Unrecorded);
    CHECK(view.detailed[0].estimated_seconds == 5400.0);
    CHECK(view.detailed[0].estimated_grams == 42.5);
    CHECK(view.detailed[0].estimated_cost == 3.2);
    // The measured duration comes from the timestamps, not the estimate.
    CHECK(view.detailed[0].duration_seconds == 3600);
    CHECK(view.detailed[0].stopped_percent == 60);
}

TEST_CASE("An empty ledger is empty", "[projectpane]")
{
    const auto view = build_print_history({});
    CHECK(view.total == 0);
    CHECK(view.detailed.empty());
    CHECK(view.count_only == 0);
}
