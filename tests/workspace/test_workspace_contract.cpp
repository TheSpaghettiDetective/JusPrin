#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"

#include <type_traits>

using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

WorkspaceSnapshot sample_workspace()
{
    WorkspaceSnapshot snapshot;
    snapshot.active_plate = PlateId(10);
    snapshot.plates       = {{PlateId(10), "Plate 1", true, {{ObjectId(100), "Object A", {}}}},
                             {PlateId(20), "Plate 2", false, {{ObjectId(200), "Object B", {}}}}};
    return snapshot;
}

} // namespace

static_assert(!std::is_same_v<PlateId, ObjectId>);
static_assert(!std::is_convertible_v<std::uint64_t, PlateId>);
static_assert(!std::is_convertible_v<std::uint64_t, ObjectId>);

TEST_CASE("Workspace identifiers are strong and ordered", "[workspace]")
{
    REQUIRE(PlateId(1) != PlateId(2));
    REQUIRE(PlateId(1) < PlateId(2));
    REQUIRE_FALSE(static_cast<bool>(PlateId()));
}

TEST_CASE("Workspace command results distinguish missing and stale IDs", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());

    const CommandResult invalid = workspace.select_object(ObjectId());
    REQUIRE_FALSE(invalid.succeeded());
    REQUIRE(invalid.error == WorkspaceError::InvalidId);

    const CommandResult missing = workspace.select_object(ObjectId(999));
    REQUIRE_FALSE(missing.succeeded());
    REQUIRE(missing.error == WorkspaceError::MissingObject);

    REQUIRE(workspace.remove_object(ObjectId(100)).succeeded());
    const CommandResult stale = workspace.select_object(ObjectId(100));
    REQUIRE_FALSE(stale.succeeded());
    REQUIRE(stale.error == WorkspaceError::StaleId);
}

TEST_CASE("Workspace commands report invalid arguments and unavailable history", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());

    const CommandResult invalid_name = workspace.rename_object(ObjectId(100), "");
    REQUIRE_FALSE(invalid_name.succeeded());
    REQUIRE(invalid_name.error == WorkspaceError::InvalidArgument);

    const CommandResult unavailable_undo = workspace.undo();
    REQUIRE_FALSE(unavailable_undo.succeeded());
    REQUIRE(unavailable_undo.error == WorkspaceError::UnavailableOperation);

    const CommandResult unavailable_redo = workspace.redo();
    REQUIRE_FALSE(unavailable_redo.succeeded());
    REQUIRE(unavailable_redo.error == WorkspaceError::UnavailableOperation);
}

TEST_CASE("Workspace observer lifetime is safe", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    int callbacks = 0;
    {
        WorkspaceSubscription subscription = workspace.subscribe([&callbacks](const WorkspaceChanged&) { ++callbacks; });
        REQUIRE(workspace.select_object(ObjectId(100)).succeeded());
        REQUIRE(callbacks == 1);
    }

    REQUIRE(workspace.select_object(ObjectId(200)).succeeded());
    REQUIRE(callbacks == 1);
}

TEST_CASE("Workspace change hub merges reasons and revisions are monotonic", "[workspace]")
{
    WorkspaceChangeHub hub;
    std::vector<WorkspaceChanged> changes;
    WorkspaceSubscription subscription = hub.subscribe([&changes](const WorkspaceChanged& change) { changes.emplace_back(change); });

    hub.merge(WorkspaceChangeReasons::Selection);
    hub.merge(WorkspaceChangeReasons::Contents);
    REQUIRE(hub.flush().has_value());
    REQUIRE(changes.size() == 1);
    REQUIRE(changes.front().revision == 1);
    REQUIRE(has_reason(changes.front().reasons, WorkspaceChangeReasons::Selection));
    REQUIRE(has_reason(changes.front().reasons, WorkspaceChangeReasons::Contents));

    hub.merge(WorkspaceChangeReasons::History);
    REQUIRE(hub.flush().has_value());
    REQUIRE(changes.size() == 2);
    REQUIRE(changes.back().revision == 2);
    REQUIRE_FALSE(hub.flush().has_value());
}

TEST_CASE("Callback snapshots are at the event revision", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    std::uint64_t callback_revision    = 0;
    WorkspaceSubscription subscription = workspace.subscribe([&workspace, &callback_revision](const WorkspaceChanged& change) {
        callback_revision = workspace.snapshot().revision;
        REQUIRE(callback_revision >= change.revision);
    });

    REQUIRE(workspace.rename_object(ObjectId(100), "Renamed").succeeded());
    REQUIRE(callback_revision == 1);
}

TEST_CASE("Fake workspace preserves command history", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    const CommandResult duplicate = workspace.duplicate_object(ObjectId(100));
    REQUIRE(duplicate.succeeded());
    REQUIRE(duplicate.object_id.has_value());

    const auto contains = [](const WorkspaceSnapshot& snapshot, ObjectId id) {
        for (const WorkspacePlate& plate : snapshot.plates)
            for (const WorkspaceObject& object : plate.objects)
                if (object.id == id)
                    return true;
        return false;
    };

    WorkspaceSnapshot after_duplicate = workspace.snapshot();
    REQUIRE(after_duplicate.can_undo);
    REQUIRE(contains(after_duplicate, *duplicate.object_id));

    REQUIRE(workspace.undo().succeeded());
    WorkspaceSnapshot after_undo = workspace.snapshot();
    REQUIRE(after_undo.can_redo);
    REQUIRE_FALSE(contains(after_undo, *duplicate.object_id));

    REQUIRE(workspace.redo().succeeded());
    WorkspaceSnapshot after_redo = workspace.snapshot();
    REQUIRE(contains(after_redo, *duplicate.object_id));
}
