#include <catch2/catch_all.hpp>

#include "../jusprin_support/FakeWorkspace.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectOutline.hpp"

using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

constexpr ProjectSessionId session(5);

OutlineCopy copy(std::uint64_t id, std::optional<PlateId> plate, bool printable = true)
{
    OutlineCopy result;
    result.id        = InstanceId(session, id);
    result.plate     = plate;
    result.printable = printable;
    return result;
}

OutlineObject object(std::uint64_t id, std::string name, std::vector<OutlineCopy> copies)
{
    OutlineObject result;
    result.id     = ObjectId(session, id);
    result.name   = std::move(name);
    result.copies = std::move(copies);
    result.volumes.push_back({VolumeId(session, id * 100), "part", VolumeRole::Part, {}, std::nullopt});
    return result;
}

OutlinePlate plate(std::uint64_t id, std::string name, bool active = false)
{
    OutlinePlate result;
    result.id     = PlateId(session, id);
    result.name   = std::move(name);
    result.active = active;
    return result;
}

const PlateId plate_one(session, 1);
const PlateId plate_two(session, 2);

ProjectOutline two_plate_job()
{
    ProjectOutline outline;
    outline.session = session;
    outline.plates  = {plate(1, "Plate 1", true), plate(2, "Plate 2")};
    outline.objects = {object(10, "Bracket", {copy(11, plate_one), copy(12, plate_one)}),
                       object(20, "Lid", {copy(21, plate_two)}),
                       object(30, "Clip", {copy(31, plate_two)})};
    return outline;
}

} // namespace

TEST_CASE("A folded plate with one object names it and counts its copies", "[workspace][outline]")
{
    const ProjectOutline outline = two_plate_job();
    const PlateSummary   summary = summarize_plate(outline, plate_one);
    CHECK(summary.objects == 1);
    CHECK(summary.copies == 2);
    CHECK(summary.wont_print == 0);
    CHECK(summary.single_object == "Bracket");
}

TEST_CASE("A folded plate with several objects reports their number, not a name", "[workspace][outline]")
{
    const ProjectOutline outline = two_plate_job();
    const PlateSummary   summary = summarize_plate(outline, plate_two);
    CHECK(summary.objects == 2);
    CHECK(summary.copies == 2);
    CHECK(summary.single_object.empty());
}

TEST_CASE("Disabled copies count toward the total and are named separately", "[workspace][outline]")
{
    ProjectOutline outline = two_plate_job();
    outline.objects[0].copies[1].printable = false;

    const PlateSummary summary = summarize_plate(outline, plate_one);
    CHECK(summary.copies == 2);
    CHECK(summary.wont_print == 1);
}

TEST_CASE("A disabled object makes every one of its copies not print", "[workspace][outline]")
{
    ProjectOutline outline = two_plate_job();
    outline.objects[0].printable = false;

    const PlateSummary summary = summarize_plate(outline, plate_one);
    CHECK(summary.copies == 2);
    CHECK(summary.wont_print == 2);
}

TEST_CASE("A plate with nothing on it is empty, and a summary follows the copies when they move", "[workspace][outline]")
{
    ProjectOutline outline = two_plate_job();
    outline.plates.push_back(plate(3, "Plate 3"));
    const PlateId plate_three(session, 3);
    CHECK(summarize_plate(outline, plate_three).empty());

    // Moving a copy to another plate changes both summaries and nothing is stored.
    outline.objects[0].copies[0].plate = plate_three;
    CHECK(summarize_plate(outline, plate_one).copies == 1);
    CHECK(summarize_plate(outline, plate_three).copies == 1);
    CHECK(summarize_plate(outline, plate_three).single_object == "Bracket");
}

TEST_CASE("Copies are off the plate only when no plate holds them", "[workspace][outline]")
{
    ProjectOutline outline = two_plate_job();
    CHECK(copies_off_plate(outline).empty());

    outline.objects[1].copies.push_back(copy(22, std::nullopt));
    const auto off = copies_off_plate(outline);
    REQUIRE(off.size() == 1);
    CHECK(off[0].object->name == "Lid");
    CHECK(off[0].copy->id == InstanceId(session, 22));
    // An off-plate copy is on no plate's summary.
    CHECK(summarize_plate(outline, plate_two).copies == 2);
}

TEST_CASE("A simple object has no volume children; modifiers and extra parts give it some", "[workspace][outline]")
{
    OutlineObject simple = object(10, "Bracket", {copy(11, plate_one)});
    CHECK_FALSE(has_volume_children(simple));

    OutlineObject with_modifier = simple;
    with_modifier.volumes.push_back({VolumeId(session, 1001), "region", VolumeRole::Modifier, {}, std::nullopt});
    CHECK(has_volume_children(with_modifier));

    OutlineObject two_parts = simple;
    two_parts.volumes.push_back({VolumeId(session, 1002), "second", VolumeRole::Part, {}, std::nullopt});
    CHECK(has_volume_children(two_parts));

    OutlineObject lone_negative = simple;
    lone_negative.volumes = {{VolumeId(session, 1003), "hole", VolumeRole::NegativePart, {}, std::nullopt}};
    CHECK(has_volume_children(lone_negative));
}

TEST_CASE("A filament assignment shows only for a multi-material job or an object override", "[workspace][outline]")
{
    ProjectOutline outline = two_plate_job();
    CHECK_FALSE(shows_filament(outline, outline.objects[0]));

    outline.objects[0].extruder = 1;
    CHECK_FALSE(shows_filament(outline, outline.objects[0])); // the slot every object starts in
    outline.objects[0].extruder = 2;
    CHECK(shows_filament(outline, outline.objects[0]));
    CHECK_FALSE(shows_filament(outline, outline.objects[1]));

    outline.filament_count = 4;
    CHECK(shows_filament(outline, outline.objects[1]));
}

TEST_CASE("Mesh problems and intentional customization are separate facts", "[workspace][outline]")
{
    MeshProblem repaired;
    repaired.repaired_errors = 3;
    CHECK(repaired.any());
    CHECK_FALSE(MeshProblem{}.any());

    ObjectCustomization painted;
    painted.support_painting = true;
    CHECK(painted.any());
    CHECK_FALSE(ObjectCustomization{}.any());

    OutlineObject broken = object(10, "Bracket", {});
    broken.mesh = {4, 0};
    // A broken mesh says nothing about whether the person customized the object.
    CHECK_FALSE(broken.customization.any());
    CHECK(broken.mesh.any());
}

TEST_CASE("Selecting a plate moves the active plate, and reports a no-op truthfully", "[workspace][outline]")
{
    FakeWorkspace workspace;
    ProjectOutline outline;
    outline.plates = {plate(1, "Plate 1", true), plate(2, "Plate 2")};
    workspace.set_outline_for_testing(outline);
    const ProjectSessionId live = workspace.snapshot().session;

    const PlateId first(live, 1), second(live, 2);
    int changes = 0;
    WorkspaceSubscription subscription = workspace.subscribe([&](const WorkspaceChanged&) { ++changes; });

    CHECK(workspace.select_plate(first).error == WorkspaceError::NoChange);
    CHECK(changes == 0);

    REQUIRE(workspace.select_plate(second).succeeded());
    CHECK(changes == 1);
    const ProjectOutline after = workspace.outline();
    CHECK_FALSE(after.plates[0].active);
    CHECK(after.plates[1].active);

    CHECK(workspace.select_plate(PlateId{}).error == WorkspaceError::InvalidId);
    CHECK(workspace.select_plate(PlateId(live, 99)).error == WorkspaceError::MissingObject);
    CHECK(workspace.select_plate(PlateId(ProjectSessionId(live.value() + 1000), 1)).error == WorkspaceError::StaleId);
}

TEST_CASE("Adding a plate activates it and is one undo step", "[workspace][outline]")
{
    FakeWorkspace workspace;
    ProjectOutline outline;
    outline.plates = {plate(1, "Plate 1", true)};
    workspace.set_outline_for_testing(outline);

    REQUIRE(workspace.add_plate().succeeded());
    ProjectOutline after = workspace.outline();
    REQUIRE(after.plates.size() == 2);
    CHECK(after.plates[1].active);
    CHECK_FALSE(after.plates[0].active);
    CHECK(workspace.snapshot().can_undo);
}

TEST_CASE("Selecting a copy or a volume goes through the workspace and rejects bad ids", "[workspace][outline]")
{
    FakeWorkspace workspace;
    const ProjectSessionId live = workspace.snapshot().session;
    ProjectOutline outline;
    outline.plates = {plate(1, "Plate 1", true)};
    OutlineObject two = object(10, "Bracket", {});
    two.copies        = {copy(11, PlateId(live, 1)), copy(12, PlateId(live, 1))};
    two.volumes.push_back({VolumeId(live, 1001), "region", VolumeRole::Modifier, {}, std::nullopt});
    OutlineObject simple = object(20, "Lid", {});
    simple.copies        = {copy(21, PlateId(live, 1))};
    simple.volumes       = {{VolumeId(live, 2001), "part", VolumeRole::Part, {}, std::nullopt}};
    outline.objects      = {two, simple};
    workspace.set_outline_for_testing(outline);

    REQUIRE(workspace.select_copy(InstanceId(live, 12)).succeeded());
    CHECK(workspace.outline().selected_copies == std::vector<InstanceId>{InstanceId(live, 12)});
    CHECK(workspace.select_copy(InstanceId(live, 12)).error == WorkspaceError::NoChange);
    CHECK(workspace.select_copy(InstanceId(live, 99)).error == WorkspaceError::MissingObject);
    CHECK(workspace.select_copy(InstanceId(ProjectSessionId(live.value() + 1000), 12)).error == WorkspaceError::StaleId);

    REQUIRE(workspace.select_volume(VolumeId(live, 1001)).succeeded());
    CHECK(workspace.outline().selected_volumes == std::vector<VolumeId>{VolumeId(live, 1001)});
    CHECK(workspace.outline().selected_copies.empty());
    // A volume of a single-part object cannot be selected on its own.
    CHECK(workspace.select_volume(VolumeId(live, 2001)).error == WorkspaceError::UnavailableOperation);
}

TEST_CASE("Replacing the project drops the outline and invalidates its ids", "[workspace][outline]")
{
    FakeWorkspace workspace;
    ProjectOutline outline;
    outline.plates = {plate(1, "Plate 1", true)};
    workspace.set_outline_for_testing(outline);
    const PlateId old_plate(workspace.snapshot().session, 1);

    workspace.replace_project({});
    CHECK(workspace.outline().plates.empty());
    CHECK(workspace.select_plate(old_plate).error == WorkspaceError::StaleId);
}

TEST_CASE("A plate action runs only when the plate offers it", "[workspace][outline]")
{
    FakeWorkspace workspace;
    ProjectOutline outline;
    outline.plates = {plate(1, "Plate 1", true), plate(2, "Plate 2")};
    workspace.set_outline_for_testing(outline);
    const ProjectSessionId live = workspace.snapshot().session;
    const PlateId first(live, 1);

    workspace.set_plate_actions_for_testing({PlateAction::Rename, PlateAction::Arrange});
    CHECK(workspace.plate_actions(first) == std::vector<PlateAction>{PlateAction::Rename, PlateAction::Arrange});

    REQUIRE(workspace.run_plate_action(first, PlateAction::Arrange).succeeded());
    REQUIRE(workspace.plate_actions_run.size() == 1);
    CHECK(workspace.plate_actions_run[0].second == PlateAction::Arrange);

    // An action Orca does not offer is refused, not run anyway.
    CHECK(workspace.run_plate_action(first, PlateAction::Delete).error == WorkspaceError::UnavailableOperation);
    CHECK(workspace.plate_actions_run.size() == 1);
    CHECK(workspace.run_plate_action(PlateId{}, PlateAction::Rename).error == WorkspaceError::InvalidId);
    CHECK(workspace.run_plate_action(PlateId(live, 99), PlateAction::Rename).error == WorkspaceError::MissingObject);
    CHECK(workspace.run_plate_action(PlateId(ProjectSessionId(live.value() + 1000), 1), PlateAction::Rename).error ==
          WorkspaceError::StaleId);
    CHECK(workspace.plate_actions(PlateId(live, 99)).empty());
}

TEST_CASE("An object action runs only when the object offers it, and a copy toggles on its own", "[workspace][outline]")
{
    FakeWorkspace workspace;
    ProjectOutline outline;
    outline.plates = {plate(1, "Plate 1", true)};
    outline.objects = {object(10, "Bracket", {copy(11, PlateId(session, 1)), copy(12, PlateId(session, 1))})};
    workspace.set_outline_for_testing(outline);
    const ProjectSessionId live = workspace.snapshot().session;
    const ObjectId         id(live, 10);

    workspace.set_object_actions_for_testing({ObjectAction::Quantity, ObjectAction::Filament});
    CHECK(workspace.object_actions(id) == std::vector<ObjectAction>{ObjectAction::Quantity, ObjectAction::Filament});
    REQUIRE(workspace.run_object_action(id, ObjectAction::Filament, 2).succeeded());
    REQUIRE(workspace.object_actions_run.size() == 1);
    CHECK(workspace.object_actions_run[0].slot == 2);

    CHECK(workspace.run_object_action(id, ObjectAction::Split).error == WorkspaceError::UnavailableOperation);
    CHECK(workspace.run_object_action(ObjectId{}, ObjectAction::Quantity).error == WorkspaceError::InvalidId);
    CHECK(workspace.run_object_action(ObjectId(ProjectSessionId(live.value() + 1000), 10), ObjectAction::Quantity).error ==
          WorkspaceError::StaleId);
    CHECK(workspace.object_actions(ObjectId(live, 99)).empty());

    const InstanceId second(live, 12);
    REQUIRE(workspace.toggle_copy_printable(second).succeeded());
    CHECK_FALSE(workspace.outline().objects[0].copies[1].printable);
    CHECK(workspace.outline().objects[0].copies[0].printable);
    CHECK(workspace.toggle_copy_printable(InstanceId(live, 99)).error == WorkspaceError::MissingObject);
}
