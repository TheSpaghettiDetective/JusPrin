#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"

#include <memory>
#include <type_traits>

using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

constexpr ProjectSessionId seed_session(77);

WorkspaceSnapshot sample_workspace()
{
    WorkspaceSnapshot snapshot;
    snapshot.session      = seed_session;
    snapshot.active_plate = PlateId(seed_session, 10);
    snapshot.plates       = {{PlateId(seed_session, 10), "Plate 1", true, false, {{ObjectId(seed_session, 100), "Object A", {}}}},
                             {PlateId(seed_session, 20), "Plate 2", false, false, {{ObjectId(seed_session, 200), "Object B", {}}}}};
    return snapshot;
}

ObjectId object_id(const IWorkspace& workspace, std::uint64_t raw_id)
{
    return ObjectId(workspace.snapshot().session, raw_id);
}

bool contains(const WorkspaceSnapshot& snapshot, ObjectId id)
{
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            if (object.id == id)
                return true;
    return false;
}

const WorkspaceObject* find_object(const WorkspaceSnapshot& snapshot, ObjectId id)
{
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            if (object.id == id)
                return &object;
    return nullptr;
}

} // namespace

static_assert(!std::is_same_v<PlateId, ObjectId>);
static_assert(!std::is_convertible_v<std::uint64_t, ProjectSessionId>);
static_assert(!std::is_convertible_v<std::uint64_t, PlateId>);
static_assert(!std::is_convertible_v<std::uint64_t, ObjectId>);
static_assert(!std::is_copy_constructible_v<WorkspaceSubscription>);

TEST_CASE("Process settings search is bounded, deterministic, and supplies correction metadata", "[workspace][settings]")
{
    FakeWorkspace workspace(sample_workspace());
    const auto first = workspace.search_settings({"", 3, ""});
    REQUIRE(first.items.size() == 3);
    REQUIRE(first.truncated);
    REQUIRE(first.next_cursor == "offset:3");
    const auto second = workspace.search_settings({"", 3, first.next_cursor});
    REQUIRE(first.items.back().key < second.items.front().key);
    REQUIRE(workspace.search_settings({"wall_loops"}).items.front().key == "wall_loops");
    REQUIRE(workspace.search_settings({"", 26}).error->code == "invalid_arguments");
    REQUIRE(workspace.search_settings({"", 1, "offset:-1"}).error->code == "invalid_arguments");
    REQUIRE(workspace.search_settings({"", 1, "offset:9999999999999999999999"}).error->code == "invalid_arguments");
    REQUIRE(workspace.search_settings({"", 1, "offset:1x"}).error->code == "invalid_arguments");
    const auto read = workspace.read_settings({"layer_heigt", "nozzle_diameter", "spiral_mode"});
    REQUIRE(read.issues[0].code == "unknown_setting");
    REQUIRE(read.issues[0].suggestions.front() == "layer_height");
    REQUIRE(read.issues[1].code == "unsupported_scope");
    REQUIRE_FALSE(read.items[0].definition.writable);
}

TEST_CASE("Process settings validate whole patches without changing the workspace", "[workspace][settings]")
{
    FakeWorkspace workspace(sample_workspace());
    unsigned events = 0;
    auto subscription = workspace.subscribe([&](const auto&) { ++events; });
    const auto before = workspace.snapshot();
    for (const auto& patch : std::vector<SettingsPatch>{
             {{{"layer_height", "NaN"}}}, {{{"layer_height", "0.4"}}}, {{{"wall_loops", "1.5"}}},
             {{{"sparse_infill_pattern", "invalid"}}}, {{{"notes", "no"}}},
             {{{"wall_loops", "4"}, {"brim_width", "-1"}}}}) {
        const auto preview = workspace.preview_settings(patch);
        REQUIRE_FALSE(preview.valid);
        REQUIRE_FALSE(preview.issues.empty());
        SettingsPreview applied;
        REQUIRE(workspace.apply_settings(patch, {}, applied).error == WorkspaceError::InvalidSettings);
    }
    const auto bounds = workspace.preview_settings({{{"layer_height", "0.4"}}});
    REQUIRE(bounds.issues.front().max == 0.3);
    const auto enums = workspace.preview_settings({{{"sparse_infill_pattern", "invalid"}}});
    REQUIRE(enums.issues.front().allowed == std::vector<std::string>{"grid", "gyroid", "line"});
    REQUIRE(workspace.read_settings({"wall_loops"}).items.front().value == "2");
    REQUIRE(workspace.snapshot().revision == before.revision);
    REQUIRE(events == 0);
}

TEST_CASE("Process settings batches are atomic and reversible outside project Undo", "[workspace][settings]")
{
    FakeWorkspace workspace(sample_workspace());
    std::vector<WorkspaceChanged> events;
    auto subscription = workspace.subscribe([&](const auto& event) {
        events.push_back(event);
        REQUIRE(workspace.snapshot().revision == event.revision);
        REQUIRE(workspace.read_settings({"wall_loops"}).items.front().value == (events.size() == 1 ? "4" : "2"));
    });
    const auto before = workspace.snapshot();
    const SettingsPatch patch{{{"wall_loops", "4"}, {"sparse_infill_density", "25"}}};
    const auto preview = workspace.preview_settings(patch);
    REQUIRE(preview.valid);
    REQUIRE(preview.changes[0].after == "25%");
    REQUIRE(events.empty());
    SettingsPreview applied;
    REQUIRE(workspace.apply_settings(patch, settings_confirmation(preview), applied).succeeded());
    REQUIRE(workspace.snapshot().revision == before.revision + 1);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().reasons == WorkspaceChangeReasons::Settings);
    REQUIRE(workspace.snapshot().setup.process_preset_dirty);
    REQUIRE(workspace.snapshot().can_undo == before.can_undo);
    REQUIRE(workspace.snapshot().can_redo == before.can_redo);
    SettingsPatch inverse;
    for (const auto& change : applied.changes)
        inverse.changes[change.key] = change.before;
    const auto undo_preview = workspace.preview_settings(inverse);
    REQUIRE(workspace.apply_settings(inverse, settings_confirmation(undo_preview), applied).succeeded());
    REQUIRE_FALSE(workspace.snapshot().setup.process_preset_dirty);
    REQUIRE(events.size() == 2);
    const auto unchanged = workspace.preview_settings(inverse);
    REQUIRE(unchanged.changes.empty());
    REQUIRE(unchanged.warnings.front().code == "unchanged");
    REQUIRE(workspace.apply_settings(inverse, {}, applied).error == WorkspaceError::NoChange);
    REQUIRE(events.size() == 2);
}

TEST_CASE("Settings apply enforces confirmed values and reports dependent normalization", "[workspace][settings]")
{
    FakeWorkspace workspace(sample_workspace());
    const SettingsPatch patch{{{"wall_loops", "4"}}};
    auto confirmed = settings_confirmation(workspace.preview_settings(patch));
    workspace.set_setting_for_testing("wall_loops", "3", false);
    SettingsPreview applied;
    const auto before = workspace.snapshot();
    REQUIRE(workspace.apply_settings(patch, confirmed, applied).error == WorkspaceError::StaleSettings);
    REQUIRE(workspace.snapshot().revision == before.revision);
    REQUIRE(workspace.read_settings({"wall_loops"}).items.front().value == "3");

    workspace.set_setting_for_testing("spiral_mode", "1");
    REQUIRE(workspace.preview_settings(patch).issues.front().code == "incompatible_settings");
    workspace.set_setting_for_testing("spiral_mode", "0");
    workspace.set_setting_for_testing("fill_multiline", "3");
    const SettingsPatch multiline{{{"sparse_infill_pattern", "line"}}};
    const auto preview = workspace.preview_settings(multiline);
    REQUIRE(preview.valid);
    REQUIRE(preview.dependencies.front().key == "fill_multiline");
    REQUIRE(preview.warnings.front().code == "normalized_dependency");
    REQUIRE(workspace.apply_settings(multiline, settings_confirmation(preview), applied).succeeded());
    REQUIRE(workspace.read_settings({"fill_multiline"}).items.front().value == "1");
    REQUIRE(applied.warnings.back().code == "normalized");
}

TEST_CASE("Workspace identifiers are strong, ordered, and session scoped", "[workspace]")
{
    REQUIRE(PlateId(ProjectSessionId(1), 1) != PlateId(ProjectSessionId(1), 2));
    REQUIRE(PlateId(ProjectSessionId(1), 1) < PlateId(ProjectSessionId(2), 1));
    REQUIRE_FALSE(static_cast<bool>(PlateId()));
    REQUIRE_FALSE(static_cast<bool>(ObjectId(ProjectSessionId(), 1)));
    REQUIRE_FALSE(static_cast<bool>(ObjectId(ProjectSessionId(1), 0)));
}

TEST_CASE("Workspace selection distinguishes invalid, missing, stale, and unsupported state", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    const ObjectId first = object_id(workspace, 100);

    REQUIRE(workspace.select_object(ObjectId()).error == WorkspaceError::InvalidId);
    REQUIRE(workspace.select_object(object_id(workspace, 999)).error == WorkspaceError::MissingObject);

    REQUIRE(workspace.select_object(first).succeeded());
    WorkspaceSnapshot selected = workspace.snapshot();
    REQUIRE(selected.selection_status == SelectionStatus::Objects);
    REQUIRE(selected.selected_objects == std::vector<ObjectId>{first});
    REQUIRE(workspace.select_object(first).error == WorkspaceError::NoChange);

    workspace.set_unsupported_selection();
    WorkspaceSnapshot unsupported = workspace.snapshot();
    REQUIRE(unsupported.selection_status == SelectionStatus::Unsupported);
    REQUIRE(unsupported.selected_objects.empty());

    REQUIRE(workspace.remove_object(first).succeeded());
    REQUIRE(workspace.select_object(first).error == WorkspaceError::StaleId);
}

TEST_CASE("Workspace rename validates input and reports no-op truthfully", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    const ObjectId first = object_id(workspace, 100);
    int callbacks = 0;
    WorkspaceSubscription subscription = workspace.subscribe([&](const WorkspaceChanged&) { ++callbacks; });

    REQUIRE(workspace.rename_object(first, "").error == WorkspaceError::InvalidArgument);
    REQUIRE(workspace.rename_object(first, "   ").error == WorkspaceError::InvalidArgument);
    REQUIRE(workspace.rename_object(first, "Object A").error == WorkspaceError::NoChange);
    REQUIRE(callbacks == 0);
    REQUIRE(workspace.snapshot().revision == 0);

    REQUIRE(workspace.rename_object(first, "Renamed").succeeded());
    REQUIRE(find_object(workspace.snapshot(), first)->name == "Renamed");
    REQUIRE(callbacks == 1);
    REQUIRE(workspace.snapshot().can_undo);
}

TEST_CASE("Duplicate returns a stable ID and removal makes it stale", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    const CommandResult duplicate = workspace.duplicate_object(object_id(workspace, 100));
    REQUIRE(duplicate.succeeded());
    REQUIRE(duplicate.object_id.has_value());
    REQUIRE(duplicate.object_id->session() == workspace.snapshot().session);
    REQUIRE(contains(workspace.snapshot(), *duplicate.object_id));

    REQUIRE(workspace.remove_object(*duplicate.object_id).succeeded());
    REQUIRE_FALSE(contains(workspace.snapshot(), *duplicate.object_id));
    REQUIRE(workspace.select_object(*duplicate.object_id).error == WorkspaceError::StaleId);
}

TEST_CASE("Undo and redo preserve IDs and report actual availability", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    const CommandResult duplicate = workspace.duplicate_object(object_id(workspace, 100));
    REQUIRE(duplicate.succeeded());

    WorkspaceSnapshot after_duplicate = workspace.snapshot();
    REQUIRE(after_duplicate.can_undo);
    REQUIRE_FALSE(after_duplicate.can_redo);

    REQUIRE(workspace.undo().succeeded());
    WorkspaceSnapshot after_undo = workspace.snapshot();
    REQUIRE_FALSE(contains(after_undo, *duplicate.object_id));
    REQUIRE(after_undo.can_redo);
    REQUIRE(workspace.select_object(*duplicate.object_id).error == WorkspaceError::StaleId);

    REQUIRE(workspace.redo().succeeded());
    WorkspaceSnapshot after_redo = workspace.snapshot();
    REQUIRE(contains(after_redo, *duplicate.object_id));
    REQUIRE(*duplicate.object_id == ObjectId(after_redo.session, duplicate.object_id->value()));
}

TEST_CASE("Unavailable history operations emit no false change", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    int callbacks = 0;
    WorkspaceSubscription subscription = workspace.subscribe([&](const WorkspaceChanged&) { ++callbacks; });

    REQUIRE(workspace.undo().error == WorkspaceError::UnavailableOperation);
    REQUIRE(workspace.redo().error == WorkspaceError::UnavailableOperation);
    REQUIRE(callbacks == 0);
    REQUIRE(workspace.snapshot().revision == 0);
}

TEST_CASE("One logical operation commits one monotonic snapshot-consistent revision", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    std::vector<WorkspaceChanged> changes;
    std::vector<std::uint64_t> callback_snapshot_revisions;
    WorkspaceSubscription subscription = workspace.subscribe([&](const WorkspaceChanged& change) {
        changes.emplace_back(change);
        callback_snapshot_revisions.emplace_back(workspace.snapshot().revision);
    });

    REQUIRE(workspace.rename_object(object_id(workspace, 100), "Renamed").succeeded());
    REQUIRE(workspace.remove_object(object_id(workspace, 200)).succeeded());

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].revision == 1);
    REQUIRE(changes[1].revision == 2);
    REQUIRE(callback_snapshot_revisions == std::vector<std::uint64_t>{1, 2});
    REQUIRE(changes[0].session == workspace.snapshot().session);
}

TEST_CASE("Change hub coalesces reasons before one committed callback", "[workspace]")
{
    WorkspaceChangeHub hub;
    const ProjectSessionId session(4);
    std::vector<WorkspaceChanged> changes;
    WorkspaceSubscription subscription = hub.subscribe([&](const WorkspaceChanged& change) { changes.emplace_back(change); });

    hub.merge(WorkspaceChangeReasons::Selection);
    hub.merge(WorkspaceChangeReasons::Contents);
    WorkspaceChangeDelivery delivery = hub.commit(session);
    REQUIRE(hub.revision() == 1);
    REQUIRE(changes.empty());
    delivery.deliver();

    REQUIRE(changes.size() == 1);
    REQUIRE(changes.front().revision == 1);
    REQUIRE(has_reason(changes.front().reasons, WorkspaceChangeReasons::Selection));
    REQUIRE(has_reason(changes.front().reasons, WorkspaceChangeReasons::Contents));
    REQUIRE_FALSE(hub.commit(session));
}

TEST_CASE("Removing subscriptions during callback dispatch is safe", "[workspace]")
{
    WorkspaceChangeHub hub;
    int first_callbacks = 0;
    int second_callbacks = 0;
    WorkspaceSubscription second;
    WorkspaceSubscription first = hub.subscribe([&](const WorkspaceChanged&) {
        ++first_callbacks;
        second.reset();
    });
    second = hub.subscribe([&](const WorkspaceChanged&) { ++second_callbacks; });

    hub.publish(ProjectSessionId(1), WorkspaceChangeReasons::Selection);
    REQUIRE(first_callbacks == 1);
    REQUIRE(second_callbacks == 0);

    WorkspaceSubscription self;
    int self_callbacks = 0;
    self = hub.subscribe([&](const WorkspaceChanged&) {
        ++self_callbacks;
        self.reset();
    });
    hub.publish(ProjectSessionId(1), WorkspaceChangeReasons::Contents);
    hub.publish(ProjectSessionId(1), WorkspaceChangeReasons::Contents);
    REQUIRE(self_callbacks == 1);
}

TEST_CASE("Destroying the change owner during callback stops dispatch safely", "[workspace]")
{
    auto hub = std::make_unique<WorkspaceChangeHub>();
    int first_callbacks = 0;
    int second_callbacks = 0;
    WorkspaceSubscription first = hub->subscribe([&](const WorkspaceChanged&) {
        ++first_callbacks;
        hub.reset();
    });
    WorkspaceSubscription second = hub->subscribe([&](const WorkspaceChanged&) { ++second_callbacks; });

    hub->publish(ProjectSessionId(1), WorkspaceChangeReasons::Contents);
    REQUIRE(first_callbacks == 1);
    REQUIRE(second_callbacks == 0);

    first.reset();
    second.reset();
}

TEST_CASE("Project replacement invalidates every prior-session ID", "[workspace]")
{
    FakeWorkspace workspace(sample_workspace());
    const WorkspaceSnapshot before = workspace.snapshot();
    const ObjectId old_object = ObjectId(before.session, 100);
    const PlateId old_plate = *before.active_plate;
    WorkspaceChanged reset_event;
    WorkspaceSubscription subscription = workspace.subscribe([&](const WorkspaceChanged& change) { reset_event = change; });

    WorkspaceSnapshot replacement = sample_workspace();
    replacement.plates.front().objects.front().name = "Replacement object";
    workspace.replace_project(std::move(replacement));

    const WorkspaceSnapshot after = workspace.snapshot();
    REQUIRE(after.session != before.session);
    REQUIRE(*after.active_plate != old_plate);
    REQUIRE(workspace.select_object(old_object).error == WorkspaceError::StaleId);
    REQUIRE(ObjectId(after.session, 100) != old_object);
    REQUIRE_FALSE(after.can_undo);
    REQUIRE_FALSE(after.can_redo);
    REQUIRE(reset_event.session == after.session);
    REQUIRE(reset_event.revision == after.revision);
    REQUIRE(has_reason(reset_event.reasons, WorkspaceChangeReasons::Project));
}

TEST_CASE("Queued notification cannot call an observer after workspace destruction", "[workspace]")
{
    int callbacks = 0;
    WorkspaceChangeDelivery queued;
    WorkspaceSubscription subscription;
    {
        auto workspace = std::make_unique<FakeWorkspace>(sample_workspace());
        subscription = workspace->subscribe([&](const WorkspaceChanged&) { ++callbacks; });
        queued = workspace->queue_change(WorkspaceChangeReasons::Contents);
        REQUIRE(workspace->snapshot().revision == 1);
        workspace.reset();
    }

    queued.deliver();
    REQUIRE(callbacks == 0);
    subscription.reset();
}

TEST_CASE("Authoritative project transactions coalesce nested native changes", "[workspace]")
{
    using namespace Slic3r::GUI;

    ProjectStateObserverHub hub;
    std::vector<ProjectStateChanged> changes;
    ProjectStateSubscription subscription = hub.subscribe(
        [&](const ProjectStateChanged& change) { changes.emplace_back(change); });

    {
        ProjectStateTransaction outer = hub.transaction();
        hub.publish(ProjectStateChangeReason::Objects);
        {
            ProjectStateTransaction inner = hub.transaction();
            hub.publish(ProjectStateChangeReason::Selection);
            hub.publish(ProjectStateChangeReason::History, true);
        }
        REQUIRE(changes.empty());
    }

    REQUIRE(changes.size() == 1);
    REQUIRE(changes.front().sequence == 1);
    REQUIRE(changes.front().project_replaced);
    REQUIRE(changes.front().project_session == 2);
    REQUIRE(hub.project_session() == 2);
    REQUIRE((static_cast<std::uint32_t>(changes.front().reasons) &
             static_cast<std::uint32_t>(ProjectStateChangeReason::Objects)) != 0);
    REQUIRE((static_cast<std::uint32_t>(changes.front().reasons) &
             static_cast<std::uint32_t>(ProjectStateChangeReason::Selection)) != 0);
    REQUIRE((static_cast<std::uint32_t>(changes.front().reasons) &
             static_cast<std::uint32_t>(ProjectStateChangeReason::History)) != 0);
}

TEST_CASE("Authoritative project subscriptions are safe during dispatch and owner teardown", "[workspace]")
{
    using namespace Slic3r::GUI;

    int first_callbacks = 0;
    int second_callbacks = 0;
    ProjectStateSubscription first;
    ProjectStateSubscription second;
    {
        auto hub = std::make_unique<ProjectStateObserverHub>();
        first = hub->subscribe([&](const ProjectStateChanged&) {
            ++first_callbacks;
            second.reset();
        });
        second = hub->subscribe([&](const ProjectStateChanged&) { ++second_callbacks; });
        hub->publish(ProjectStateChangeReason::Selection);

        REQUIRE(first_callbacks == 1);
        REQUIRE(second_callbacks == 0);
    }

    first.reset();
    second.reset();

    auto owner = std::make_unique<ProjectStateObserverHub>();
    ProjectStateSubscription destroys_owner = owner->subscribe(
        [&](const ProjectStateChanged&) { owner.reset(); });
    ProjectStateSubscription skipped = owner->subscribe(
        [&](const ProjectStateChanged&) { FAIL("dispatch continued after its owner was destroyed"); });
    owner->publish(ProjectStateChangeReason::Objects);
    destroys_owner.reset();
    skipped.reset();
}
