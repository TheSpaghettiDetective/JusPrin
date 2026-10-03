#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Shell/LeftPaneModel.hpp"

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

constexpr ProjectSessionId session(9);

OutlineCopy copy(std::uint64_t id, std::optional<PlateId> plate, bool printable = true)
{
    OutlineCopy result;
    result.id        = InstanceId(session, id);
    result.plate     = plate;
    result.printable = printable;
    return result;
}

OutlineVolume volume(std::uint64_t id, std::string name, VolumeRole role = VolumeRole::Part)
{
    return {VolumeId(session, id), std::move(name), role, {}, std::nullopt};
}

OutlineObject object(std::uint64_t id, std::string name, std::vector<OutlineCopy> copies)
{
    OutlineObject result;
    result.id      = ObjectId(session, id);
    result.name    = std::move(name);
    result.copies  = std::move(copies);
    result.volumes = {volume(id * 100, "part")};
    return result;
}

OutlinePlate plate(std::uint64_t id, std::string name, bool active)
{
    OutlinePlate result;
    result.id     = PlateId(session, id);
    result.name   = std::move(name);
    result.active = active;
    return result;
}

const PlateId one(session, 1);
const PlateId two(session, 2);

ProjectOutline ordinary_job()
{
    ProjectOutline outline;
    outline.session = session;
    outline.plates  = {plate(1, "Plate 1", true), plate(2, "Plate 2", false)};
    outline.objects = {object(10, "Bracket", {copy(11, one)}), object(20, "Lid", {copy(21, two)})};
    return outline;
}

WorkspaceSnapshot empty_snapshot()
{
    WorkspaceSnapshot snapshot;
    snapshot.session = session;
    return snapshot;
}

std::vector<PaneRow> rows_of(const ProjectOutline& outline, const WorkspaceSnapshot& snapshot = empty_snapshot(),
                             const PaneNavigation& navigation = {})
{
    return build_plate_rows(outline, snapshot, navigation);
}

using Kind = PaneRow::Kind;

} // namespace

TEST_CASE("Every plate is listed, only the active one is open, and the others carry a summary", "[leftpane]")
{
    const auto rows = rows_of(ordinary_job());
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].kind == Kind::Plate);
    CHECK(rows[0].expanded);
    CHECK(rows[0].selected);
    CHECK(rows[1].kind == Kind::Object);
    CHECK(rows[1].name == "Bracket");
    CHECK(rows[2].kind == Kind::Plate);
    CHECK_FALSE(rows[2].expanded);
    CHECK(rows[2].summary.single_object == "Lid");
}

TEST_CASE("One plate and one object still make a pane with two rows", "[leftpane]")
{
    ProjectOutline outline;
    outline.session = session;
    outline.plates  = {plate(1, "Plate 1", true)};
    outline.objects = {object(10, "Bracket", {copy(11, one)})};
    const auto rows = rows_of(outline);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].kind == Kind::Plate);
    CHECK(rows[1].kind == Kind::Object);
    CHECK_FALSE(rows[1].expandable);
}

TEST_CASE("A simple object is one row; children exist only for data that exists", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    const auto simple = rows_of(outline);
    CHECK_FALSE(simple[1].expandable);
    CHECK(simple[1].copies == 1);

    SECTION("a second copy gives copy rows")
    {
        outline.objects[0].copies.push_back(copy(12, one));
        PaneNavigation navigation;
        navigation.set_expanded(outline.objects[0].id, true);
        const auto rows = rows_of(outline, empty_snapshot(), navigation);
        REQUIRE(rows.size() == 5);
        CHECK(rows[1].expandable);
        CHECK(rows[1].copies == 2);
        CHECK(rows[2].kind == Kind::Copy);
        CHECK(rows[2].ordinal == 1);
        CHECK(rows[3].kind == Kind::Copy);
        CHECK(rows[3].ordinal == 2);
    }
    SECTION("a modifier gives volume rows, parts and all")
    {
        outline.objects[0].volumes.push_back(volume(1001, "region", VolumeRole::Modifier));
        outline.objects[0].volumes.push_back(volume(1002, "hole", VolumeRole::NegativePart));
        PaneNavigation navigation;
        navigation.set_expanded(outline.objects[0].id, true);
        const auto rows = rows_of(outline, empty_snapshot(), navigation);
        REQUIRE(rows.size() == 6);
        CHECK(rows[2].kind == Kind::Volume);
        CHECK(rows[2].role == VolumeRole::Part);
        CHECK(rows[3].role == VolumeRole::Modifier);
        CHECK(rows[4].role == VolumeRole::NegativePart);
    }
    SECTION("a closed object lists no children")
    {
        outline.objects[0].copies.push_back(copy(12, one));
        const auto rows = rows_of(outline);
        CHECK(rows.size() == 3);
        CHECK(rows[1].expandable);
        CHECK_FALSE(rows[1].expanded);
    }
}

TEST_CASE("A disabled copy, and a disabled object, say they will not print", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    outline.objects[0].copies.push_back(copy(12, one, /*printable=*/false));
    PaneNavigation navigation;
    navigation.set_expanded(outline.objects[0].id, true);

    auto rows = rows_of(outline, empty_snapshot(), navigation);
    CHECK(rows[1].copies == 2);
    CHECK(rows[1].wont_print == 1);
    CHECK_FALSE(rows[1].all_wont_print());
    CHECK_FALSE(rows[2].copy_wont_print);
    CHECK(rows[3].copy_wont_print);

    outline.objects[0].printable = false;
    rows = rows_of(outline, empty_snapshot(), navigation);
    CHECK(rows[1].all_wont_print());
    CHECK(rows[2].copy_wont_print);
}

TEST_CASE("A filament appears only for a multi-material job or an object override", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    CHECK_FALSE(rows_of(outline)[1].filament.has_value());

    outline.objects[0].extruder = 1;
    CHECK_FALSE(rows_of(outline)[1].filament.has_value());
    outline.objects[0].extruder = 3;
    CHECK(rows_of(outline)[1].filament == 3);

    outline.objects[0].extruder.reset();
    outline.filament_count = 4;
    CHECK(rows_of(outline)[1].filament == 1);
}

TEST_CASE("Modifiers and negative parts show a filament only when they override it", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    outline.filament_count = 3;
    outline.objects[0].volumes.push_back(volume(1001, "modifier", VolumeRole::Modifier));
    outline.objects[0].volumes.push_back(volume(1002, "hole", VolumeRole::NegativePart));
    PaneNavigation navigation;
    navigation.set_expanded(outline.objects[0].id, true);
    auto rows = rows_of(outline, empty_snapshot(), navigation);
    CHECK(rows[2].filament == 1);
    CHECK_FALSE(rows[3].filament);
    CHECK_FALSE(rows[4].filament);

    outline.objects[0].volumes[1].extruder = 2;
    rows = rows_of(outline, empty_snapshot(), navigation);
    CHECK(rows[3].filament == 2);
}

TEST_CASE("Not on a plate appears only with a copy that no plate holds", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    for (const PaneRow& row : rows_of(outline))
        CHECK(row.kind != Kind::OffPlateHeader);

    outline.objects[1].copies.push_back(copy(22, std::nullopt));
    const auto rows = rows_of(outline);
    REQUIRE(rows.size() == 5);
    CHECK(rows[3].kind == Kind::OffPlateHeader);
    CHECK(rows[3].copies == 1);
    CHECK(rows[4].kind == Kind::Object);
    CHECK(rows[4].name == "Lid");
    CHECK(rows[4].copies == 1);
    CHECK_FALSE(rows[4].plate);
}

TEST_CASE("The selection is shown on the row Orca selected, and opens a collapsed object", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    outline.objects[0].copies.push_back(copy(12, one));
    outline.objects[0].volumes.push_back(volume(1001, "region", VolumeRole::Modifier));

    WorkspaceSnapshot snapshot = empty_snapshot();
    SECTION("a whole object")
    {
        snapshot.selection_status = SelectionStatus::Objects;
        snapshot.selected_objects = {outline.objects[0].id};
        const auto rows           = rows_of(outline, snapshot);
        CHECK(rows[1].selected);
        CHECK_FALSE(rows[1].expanded);
    }
    SECTION("one copy")
    {
        outline.selected_copies = {InstanceId(session, 12)};
        const auto rows         = rows_of(outline, snapshot);
        CHECK_FALSE(rows[1].selected);
        CHECK(rows[1].expanded);
        const auto selected = std::count_if(rows.begin(), rows.end(), [](const PaneRow& row) { return row.selected; });
        CHECK(selected == 2); // the active plate and the copy
        const auto copy_row = std::find_if(rows.begin(), rows.end(), [](const PaneRow& row) { return row.kind == Kind::Copy && row.selected; });
        REQUIRE(copy_row != rows.end());
        CHECK(copy_row->ordinal == 2);
    }
    SECTION("one volume")
    {
        outline.selected_volumes = {VolumeId(session, 1001)};
        const auto rows          = rows_of(outline, snapshot);
        const auto volume_row = std::find_if(rows.begin(), rows.end(), [](const PaneRow& row) { return row.kind == Kind::Volume && row.selected; });
        REQUIRE(volume_row != rows.end());
        CHECK(volume_row->role == VolumeRole::Modifier);
    }
}

TEST_CASE("A customization mark and a mesh problem are separate", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    outline.objects[0].customization.support_painting = true;
    outline.objects[1].mesh                           = {3, 0};

    const auto rows = rows_of(outline);
    CHECK(rows[1].customized);
    CHECK_FALSE(rows[1].mesh.any());

    outline.plates[1].active = true;
    outline.plates[0].active = false;
    const auto other = rows_of(outline);
    CHECK_FALSE(other[2].customized);
    CHECK(other[2].mesh.any());
}

TEST_CASE("Plate flags are shown only when they apply", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    outline.plates[0].locked          = true;
    outline.plates[1].custom_settings = true;
    outline.plates[1].sliced          = true;

    const auto rows = rows_of(outline);
    CHECK(rows[0].locked);
    CHECK_FALSE(rows[0].custom_settings);
    CHECK_FALSE(rows[0].sliced);
    CHECK(rows[2].custom_settings);
    CHECK(rows[2].sliced);
    CHECK_FALSE(rows[2].locked);
}

TEST_CASE("Folded summaries follow the copies when they move", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    CHECK(rows_of(outline)[2].summary.single_object == "Lid");

    outline.objects[1].copies[0].plate = one;
    outline.plates[1].active           = false;
    const auto rows                    = rows_of(outline);
    // Plate 2 is empty now, and Plate 1 lists both objects.
    CHECK(rows.back().kind == Kind::Plate);
    CHECK(rows.back().summary.empty());
    CHECK(rows[0].summary.objects == 2);
}

TEST_CASE("Tabs land at their top level and Escape goes up one Project level", "[leftpane][navigation]")
{
    PaneNavigation navigation;
    CHECK(navigation.tab() == PaneTab::Plates);

    // Escape on Plates, and at the Project root, is not the pane's to take.
    CHECK_FALSE(navigation.back());
    navigation.show_project();
    CHECK(navigation.view() == ProjectView::Root);
    CHECK_FALSE(navigation.back());
    CHECK(navigation.tab() == PaneTab::Project);

    navigation.open(ProjectView::Details);
    CHECK(navigation.back());
    CHECK(navigation.view() == ProjectView::Root);
    CHECK(navigation.tab() == PaneTab::Project);

    // The Plates tab leaves a Project subview directly.
    navigation.open(ProjectView::PrintHistory);
    navigation.show_plates();
    CHECK(navigation.tab() == PaneTab::Plates);
    CHECK(navigation.view() == ProjectView::Root);

    // Choosing the Project tab again lands on its root, not where it was left.
    navigation.open(ProjectView::Versions);
    navigation.show_project();
    CHECK(navigation.view() == ProjectView::Root);
}

TEST_CASE("A new project drops the expansion and any Project subview", "[leftpane][navigation]")
{
    PaneNavigation navigation;
    CHECK(navigation.follow_session(session));
    const ObjectId id(session, 10);
    navigation.set_expanded(id, true);
    navigation.open(ProjectView::Details);

    CHECK_FALSE(navigation.follow_session(session));
    CHECK(navigation.is_expanded(id));

    CHECK(navigation.follow_session(ProjectSessionId(10)));
    CHECK_FALSE(navigation.is_expanded(id));
    CHECK(navigation.view() == ProjectView::Root);
}

TEST_CASE("A shown filament carries its slot's colour, and no colour is invented", "[leftpane]")
{
    ProjectOutline outline = ordinary_job();
    outline.filament_count   = 3;
    outline.filament_colours = {"#FF0000", "#00FF00"};

    outline.objects[0].extruder = 2;
    CHECK(rows_of(outline)[1].filament == 2);
    CHECK(rows_of(outline)[1].filament_colour == "#00FF00");

    // The third slot has no colour in the profile.
    outline.objects[0].extruder = 3;
    CHECK(rows_of(outline)[1].filament == 3);
    CHECK(rows_of(outline)[1].filament_colour.empty());

    // A single-material job shows no filament at all, so no colour either.
    outline.filament_count = 1;
    outline.objects[0].extruder.reset();
    CHECK_FALSE(rows_of(outline)[1].filament.has_value());
    CHECK(rows_of(outline)[1].filament_colour.empty());
}
