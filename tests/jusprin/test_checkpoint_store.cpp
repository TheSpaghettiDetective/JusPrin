#include <catch2/catch_all.hpp>

#include "project_archive_fixture.hpp"
#include "slic3r/GUI/JusPrin/Project/CheckpointStore.hpp"
#include "slic3r/GUI/JusPrin/Project/MeshEntry.hpp"
#include "slic3r/GUI/JusPrin/Project/ModelReferences.hpp"
#include "slic3r/GUI/JusPrin/Project/ZipFile.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"

#include <chrono>

using namespace Slic3r::GUI::JusPrin::Project;
using namespace project_fixture;
using Slic3r::GUI::JusPrin::Workspace::utc_timestamp;

namespace {

namespace fs = std::filesystem;
using Clock  = std::chrono::system_clock;

// OrcaSlicer's backup numbers parts (k << 16 | backup id); a full save
// numbers them in sequence. The same objects, both ways.
std::vector<Object> backup_numbered() { return {{"Cube_1", {{65537, "0"}}}, {"Sphere_2", {{65538, "0.5"}, {131074, "0.25"}}}}; }
std::vector<Object> save_numbered() { return {{"Cube_1", {{1, "0"}}}, {"Sphere_2", {{2, "0.5"}, {3, "0.25"}}}}; }

struct StoreFixture
{
    fs::path        dir = fresh_dir("checkpoint-store");
    CheckpointStore store{dir / "history" / "p-1"};

    CheckpointStore::Request request(const std::string& id, const std::vector<Object>& small,
                                     const std::vector<Object>& full, const std::string& reuse = {})
    {
        REQUIRE(write_archive(dir / (id + "-small.3mf"), small, false));
        if (!full.empty())
            REQUIRE(write_archive(dir / (id + "-full.3mf"), full, true));
        CheckpointStore::Request request;
        request.id            = id;
        request.created_at    = utc_timestamp(Clock::time_point{} + std::chrono::hours(std::stoi(id.substr(1))));
        request.reason        = "editing";
        request.small_archive = dir / (id + "-small.3mf");
        if (!full.empty())
            request.mesh_archive = dir / (id + "-full.3mf");
        request.reuse_from = reuse;
        return request;
    }

    std::size_t stored_meshes() const
    {
        std::size_t count = 0;
        for (const auto& entry : fs::directory_iterator(store.root() / "meshes"))
            if (entry.path().extension() == ".model")
                ++count;
        return count;
    }
};

} // namespace

TEST_CASE_METHOD(StoreFixture, "A checkpoint stores each mesh once and restores a loadable layout", "[CheckpointStore]")
{
    std::string error;
    const auto  first = store.record(request("c1", backup_numbered(), save_numbered()), error);
    INFO(error);
    REQUIRE(first);
    REQUIRE(first->meshes.size() == 2);
    REQUIRE(stored_meshes() == 2);
    REQUIRE(fs::exists(store.root() / "checkpoints" / "c1" / "project.3mf"));

    // Layout-only edits: the same meshes again.
    const auto second = store.record(request("c2", backup_numbered(), save_numbered()), error);
    INFO(error);
    REQUIRE(second);
    REQUIRE(stored_meshes() == 2);
    REQUIRE(second->meshes[0].key == first->meshes[0].key);

    // Restore lays out the archive as .3mf beside each mesh, ids as the
    // archive names them, each mesh file a zip of one entry under its own name.
    const fs::path scratch = dir / "scratch";
    REQUIRE(store.restore("c2", scratch, error));
    INFO(error);
    REQUIRE(fs::exists(scratch / ".3mf"));
    ZipReader mesh;
    REQUIRE(mesh.open(scratch / "3D" / "Objects" / "Sphere_2.model"));
    REQUIRE(mesh.entry_count() == 1);
    REQUIRE(mesh.entry_name(0) == "3D/Objects/Sphere_2.model");
    const auto text = mesh.read(std::size_t(0));
    REQUIRE(text);
    REQUIRE(mesh_entry_ids(*text) == std::vector<std::uint64_t>{65538, 131074});
    REQUIRE(*text == mesh_entry(backup_numbered()[1]));
    ZipReader archive;
    REQUIRE(archive.open(scratch / ".3mf"));
    const auto main_model = archive.read("3D/3dmodel.model");
    REQUIRE(main_model);
    REQUIRE(mesh_references(*main_model).size() == 2);
}

TEST_CASE_METHOD(StoreFixture, "A changed mesh adds one stored copy; unchanged ones are shared", "[CheckpointStore]")
{
    std::string error;
    REQUIRE(store.record(request("c1", backup_numbered(), save_numbered()), error));
    auto small = backup_numbered();
    auto full  = save_numbered();
    small[1].parts[0].paint = full[1].parts[0].paint = "12"; // one paint stroke on the sphere
    const auto second = store.record(request("c2", small, full), error);
    INFO(error);
    REQUIRE(second);
    REQUIRE(stored_meshes() == 3);
    const auto first = store.find("c1");
    REQUIRE(first);
    REQUIRE(second->meshes[0].key == first->meshes[0].key);
    REQUIRE(second->meshes[1].key != first->meshes[1].key);
}

TEST_CASE_METHOD(StoreFixture, "Meshes the mesh archive lacks come from the checkpoint to reuse", "[CheckpointStore]")
{
    std::string error;
    REQUIRE(store.record(request("c1", backup_numbered(), save_numbered()), error));
    // Only the changed object was written this time.
    auto small = backup_numbered();
    auto changed = std::vector<Object>{save_numbered()[0]};
    changed[0].parts[0].vertex = small[0].parts[0].vertex = "0.75";
    const auto second = store.record(request("c2", small, changed, "c1"), error);
    INFO(error);
    REQUIRE(second);
    REQUIRE(stored_meshes() == 3);
    REQUIRE(second->meshes[1].key == store.find("c1")->meshes[1].key);

    // Without a checkpoint to reuse, the missing mesh is an error and nothing is recorded.
    REQUIRE_FALSE(store.record(request("c3", small, changed), error));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("No mesh was supplied for 3D/Objects/Sphere_2.model"));
    REQUIRE_FALSE(store.find("c3"));
    REQUIRE(store.checkpoints().size() == 2);
}

TEST_CASE_METHOD(StoreFixture, "A mesh whose part count differs from its references is refused", "[CheckpointStore]")
{
    std::string error;
    auto        full = save_numbered();
    full[1].parts.pop_back(); // the sphere lost a part in the mesh archive only
    REQUIRE_FALSE(store.record(request("c1", backup_numbered(), full), error));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("holds 1 parts but the checkpoint refers to 2"));
    REQUIRE(store.checkpoints().empty());
    REQUIRE(stored_meshes() == 0); // the cube written before the failure was removed again
}

TEST_CASE_METHOD(StoreFixture, "A damaged small archive or a bad id is refused", "[CheckpointStore]")
{
    std::string error;
    auto        bad = request("c1", backup_numbered(), save_numbered());
    bad.small_archive = dir / "none.3mf";
    REQUIRE_FALSE(store.record(bad, error));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("not whole"));
    bad = request("c2", backup_numbered(), save_numbered());
    bad.id = "../escape";
    REQUIRE_FALSE(store.record(bad, error));
    REQUIRE(store.checkpoints().empty());
}

TEST_CASE_METHOD(StoreFixture, "Removing a checkpoint and sweeping frees only meshes nothing else uses", "[CheckpointStore]")
{
    std::string error;
    REQUIRE(store.record(request("c1", backup_numbered(), save_numbered()), error));
    auto small = backup_numbered();
    auto full  = save_numbered();
    small[0].parts[0].vertex = full[0].parts[0].vertex = "0.9";
    REQUIRE(store.record(request("c2", small, full), error));
    REQUIRE(stored_meshes() == 3);
    REQUIRE(store.sweep() == 0);
    REQUIRE(store.remove("c2"));
    REQUIRE_FALSE(store.remove("c2"));
    REQUIRE(store.sweep() > 0);
    REQUIRE(stored_meshes() == 2);
    REQUIRE(store.restore("c1", dir / "scratch", error));
    REQUIRE(store.bytes() > 0);
}

TEST_CASE_METHOD(StoreFixture, "Labels and keep flags are updated in place", "[CheckpointStore]")
{
    std::string error;
    REQUIRE(store.record(request("c1", backup_numbered(), save_numbered()), error));
    REQUIRE(store.update("c1", "Before the lid", true));
    const auto record = store.find("c1");
    REQUIRE(record);
    REQUIRE(record->label == "Before the lid");
    REQUIRE(record->keep);
    REQUIRE_FALSE(store.update("c9", "", false));
}

TEST_CASE_METHOD(StoreFixture, "Checkpoints list oldest first by creation time", "[CheckpointStore]")
{
    std::string error;
    REQUIRE(store.record(request("c5", backup_numbered(), save_numbered()), error));
    REQUIRE(store.record(request("c2", backup_numbered(), save_numbered()), error));
    REQUIRE(store.record(request("c9", backup_numbered(), save_numbered()), error));
    const auto records = store.checkpoints();
    REQUIRE(records.size() == 3);
    REQUIRE(records[0].id == "c2");
    REQUIRE(records[1].id == "c5");
    REQUIRE(records[2].id == "c9");
}

TEST_CASE("Thinning keeps everything recent, one per hour, one per day, then one", "[CheckpointStore]")
{
    const Clock::time_point now = Clock::time_point{} + std::chrono::hours(24 * 400);
    auto at = [&](const std::string& id, std::chrono::seconds age, bool keep = false) {
        CheckpointRecord record;
        record.id         = id;
        record.created_at = utc_timestamp(now - age);
        record.keep       = keep;
        return record;
    };
    using namespace std::chrono_literals;
    std::vector<CheckpointRecord> records{
        at("old-a", 24h * 60), at("old-b", 24h * 45),                          // older than 30 days: one stays
        at("day-a", 24h * 10 + 1h), at("day-b", 24h * 10 + 5h),                // same day: the newer stays
        at("day-c", 24h * 12),                                                  // its own day
        at("hour-a", 72h + 10min), at("hour-b", 72h + 40min), at("hour-c", 73h + 5min), // two hours: a and b share one
        at("kept", 24h * 20, true),                                             // a named one
        at("fresh-a", 47h), at("fresh-b", 1h), at("fresh-c", 0s)};
    std::sort(records.begin(), records.end(), [](const CheckpointRecord& l, const CheckpointRecord& r) { return l.created_at < r.created_at; });
    const std::vector<std::string> removed = thinning_candidates(records, now);
    const std::vector<std::string> expected{"old-a", "day-b", "hour-b"};
    REQUIRE_THAT(removed, Catch::Matchers::UnorderedEquals(expected));
}

TEST_CASE("Thinning never removes the newest checkpoint even when it is old", "[CheckpointStore]")
{
    const Clock::time_point now = Clock::time_point{} + std::chrono::hours(24 * 400);
    CheckpointRecord        only;
    only.id         = "only";
    only.created_at = utc_timestamp(now - std::chrono::hours(24 * 90));
    REQUIRE(thinning_candidates({only}, now).empty());
    REQUIRE(thinning_candidates({}, now).empty());
}

TEST_CASE_METHOD(StoreFixture, "A size cap removes the oldest unkept checkpoints outside the newest few", "[CheckpointStore]")
{
    std::string error;
    for (int i = 1; i <= 6; ++i) {
        auto small = backup_numbered();
        auto full  = save_numbered();
        small[0].parts[0].vertex = full[0].parts[0].vertex = std::to_string(i); // every checkpoint changes the cube
        REQUIRE(store.record(request("c" + std::to_string(i), small, full), error));
    }
    REQUIRE(store.update("c2", "keep me", true));
    RetentionRules rules;
    rules.never_cap_newest = 2;
    const Clock::time_point now = Clock::time_point{} + std::chrono::hours(7);
    // A cap of one byte: everything that may go, goes.
    const auto removed = store.apply_retention(rules, now, 1);
    REQUIRE(removed == std::vector<std::string>{"c1", "c3", "c4"});
    const auto left = store.checkpoints();
    REQUIRE(left.size() == 3);
    REQUIRE(left[0].id == "c2");
    REQUIRE(left[1].id == "c5");
    REQUIRE(left[2].id == "c6");
    REQUIRE(stored_meshes() == 4); // three cubes and the sphere
    REQUIRE(store.apply_retention(rules, now, 0).empty());
}
