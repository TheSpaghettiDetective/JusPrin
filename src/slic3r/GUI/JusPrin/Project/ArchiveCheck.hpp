#pragma once

// Proof that a saved project archive is whole. OrcaSlicer's exporter reports
// success for a damaged file when the disk runs out during the write: the
// per-object mesh copy (mz_zip_writer_add_from_zip_reader) and the final
// close are unchecked, so the good file is replaced by an archive that is
// truncated or is missing a mesh (measured 2026-09-29, see
// agent-docs/jusprin/autosave-checkpoint-storage-handoff.md). JusPrin checks
// free space before it writes and the archive after.
//
// GUI-free, no OrcaSlicer types.

#include <cstdint>
#include <filesystem>
#include <string>

namespace Slic3r::GUI::JusPrin::Project {

struct ArchiveCheck
{
    bool          ok{false};
    std::string   problem; // empty when ok
    std::uint64_t bytes{0};
    std::size_t   entries{0};
    std::size_t   meshes{0}; // mesh entries the main model refers to
};

// The central directory opens, the parts every project has are present, every
// mesh entry the main model refers to is present (unless `meshes_elsewhere`:
// OrcaSlicer's backup archive keeps them in files beside it), and, with
// `inflate`, every entry inflates to the size and CRC it claims.
ArchiveCheck verify_project_archive(const std::filesystem::path& path, bool meshes_elsewhere = false, bool inflate = true);

struct SpaceCheck
{
    bool          enough{false};
    std::uint64_t available{0};
    std::uint64_t required{0};
};

// The bytes a save needs free before it starts: the archive it will write,
// written beside the one it replaces, plus a margin for everything else the
// machine is writing meanwhile.
std::uint64_t required_free_space(std::uint64_t expected_archive_bytes);
SpaceCheck    check_free_space(const std::filesystem::path& directory, std::uint64_t expected_archive_bytes);

} // namespace Slic3r::GUI::JusPrin::Project
