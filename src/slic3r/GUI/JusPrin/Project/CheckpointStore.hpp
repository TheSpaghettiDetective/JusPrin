#pragma once

// Point-in-time copies of a project a person can go back to, kept outside
// the project file under one folder per project:
//
//   <root>/
//     meshes/<key>.model        one mesh entry, stored once, as its own zip
//     meshes/<key>.json         the <object id> values that copy carries
//     checkpoints/<id>/
//       project.3mf             OrcaSlicer's backup-style archive: settings,
//                               plates, placement, no meshes, no thumbnails
//       manifest.json           what the checkpoint is and which mesh entry
//                               each of its references resolves to
//
// A checkpoint of settings or layout edits costs about 16 KB; a changed mesh
// costs one compressed mesh (measured in
// agent-docs/jusprin/autosave-checkpoint-storage-findings.md). Restoring
// rebuilds a folder OrcaSlicer's restore loader accepts: the archive as
// `.3mf` beside a `3D/Objects/` folder holding every mesh entry it refers
// to, ids rewritten to the ones that archive names.
//
// GUI-free, no OrcaSlicer types. Not thread-safe: one owner per store.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Project {

struct StoredMesh
{
    std::string   path; // archive entry name the checkpoint refers to
    std::string   key;  // mesh_entry_key of the stored copy
    std::uint64_t bytes{0};
};

struct CheckpointRecord
{
    std::string             id;
    std::string             created_at; // ISO 8601 UTC, as UtcTime.hpp writes it
    std::string             label;      // a person's name for it, or empty
    std::string             reason;     // why it was taken: "opened", "agent", "editing", "restore"
    bool                    keep{false}; // never thinned or capped away
    std::uint64_t           archive_bytes{0};
    std::vector<StoredMesh> meshes;
};

struct RetentionRules
{
    // Every checkpoint newer than this is kept.
    std::chrono::seconds keep_all{std::chrono::hours(48)};
    // Older than keep_all and newer than this: one per hour.
    std::chrono::seconds hourly{std::chrono::hours(24 * 7)};
    // Older than hourly and newer than this: one per day. Older: none.
    std::chrono::seconds daily{std::chrono::hours(24 * 30)};
    // A size cap never removes these many newest checkpoints.
    std::size_t never_cap_newest{10};
};

// The checkpoints thinning removes: the oldest of each hour or day bucket
// goes, the newest of each stays, and the newest overall and every kept one
// stay whatever their age. `records` oldest first.
std::vector<std::string> thinning_candidates(const std::vector<CheckpointRecord>& records,
                                             std::chrono::system_clock::time_point now,
                                             const RetentionRules& rules = {});

class CheckpointStore
{
public:
    explicit CheckpointStore(std::filesystem::path root);

    const std::filesystem::path& root() const { return m_root; }

    struct Request
    {
        std::string id; // a file name: letters, digits, '-', '_', '.'
        std::string created_at;
        std::string label;
        std::string reason;
        bool        keep{false};
        // OrcaSlicer's backup-style archive of the moment.
        std::filesystem::path small_archive;
        // An archive whose 3D/Objects entries hold the meshes the small
        // archive refers to: a full save of the model, or of the changed
        // objects only. May be empty when reuse_from covers every mesh.
        std::filesystem::path mesh_archive;
        // A checkpoint whose stored meshes stand in, by entry name, for the
        // ones mesh_archive does not hold. The caller vouches those objects
        // did not change in between.
        std::string reuse_from;
    };

    // Records the checkpoint or leaves nothing behind. A small archive that
    // does not verify, a mesh the request does not supply, or an entry whose
    // object count does not match its references is an error.
    std::optional<CheckpointRecord> record(const Request& request, std::string& error);

    std::vector<CheckpointRecord>   checkpoints() const; // oldest first
    std::optional<CheckpointRecord> find(const std::string& id) const;
    bool update(const std::string& id, const std::string& label, bool keep);
    bool remove(const std::string& id);

    // Lays out `scratch` for OrcaSlicer's restore loader: `.3mf` plus
    // 3D/Objects/<entry> for every mesh the archive refers to. The scratch
    // folder is the loader's to adopt and delete; the store is never
    // pointed at directly.
    bool restore(const std::string& id, const std::filesystem::path& scratch, std::string& error) const;

    // Removes meshes no checkpoint refers to. Returns the bytes freed.
    std::uint64_t sweep();
    std::uint64_t bytes() const;

    // Thins by age, then removes the oldest unkept checkpoints outside the
    // newest few until the store fits `cap_bytes` (0 for no cap). Returns
    // the ids removed.
    std::vector<std::string> apply_retention(const RetentionRules& rules, std::chrono::system_clock::time_point now,
                                             std::uint64_t cap_bytes);

private:
    std::filesystem::path checkpoint_dir(const std::string& id) const { return m_root / "checkpoints" / id; }
    std::filesystem::path mesh_path(const std::string& key) const { return m_root / "meshes" / (key + ".model"); }
    std::filesystem::path mesh_ids_path(const std::string& key) const { return m_root / "meshes" / (key + ".json"); }
    std::optional<CheckpointRecord> read_manifest(const std::filesystem::path& dir) const;
    bool write_manifest(const std::filesystem::path& dir, const CheckpointRecord& record) const;
    std::optional<std::vector<std::uint64_t>> stored_ids(const std::string& key) const;

    std::filesystem::path m_root;
};

} // namespace Slic3r::GUI::JusPrin::Project
