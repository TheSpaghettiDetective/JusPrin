#include "ArchiveCheck.hpp"

#include "ModelReferences.hpp"
#include "ZipFile.hpp"

#include <algorithm>
#include <system_error>

namespace Slic3r::GUI::JusPrin::Project {

namespace {

constexpr const char* kRequiredEntries[] = {"[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model"};

// 64 MB: room for the temporary file the exporter renames from, OrcaSlicer's
// own backup writes, and the log.
constexpr std::uint64_t kFreeSpaceMargin = std::uint64_t(64) << 20;

} // namespace

ArchiveCheck verify_project_archive(const std::filesystem::path& path, bool meshes_elsewhere, bool inflate)
{
    ArchiveCheck    check;
    std::error_code ec;
    check.bytes = std::filesystem::file_size(path, ec);
    if (ec) {
        check.problem = "The file is missing";
        return check;
    }
    ZipReader archive;
    if (!archive.open(path)) {
        check.problem = "The file is not a complete zip archive";
        return check;
    }
    check.entries = archive.entry_count();
    for (const char* required : kRequiredEntries)
        if (!archive.find(required)) {
            check.problem = std::string("The archive has no ") + required;
            return check;
        }
    const auto main_model = archive.read("3D/3dmodel.model");
    if (!main_model) {
        check.problem = "The main model file does not inflate";
        return check;
    }
    const std::vector<MeshReference> references = mesh_references(*main_model);
    check.meshes                                = references.size();
    if (!meshes_elsewhere)
        for (const MeshReference& reference : references)
            if (!archive.find(reference.path)) {
                check.problem = "The archive is missing the mesh " + reference.path;
                return check;
            }
    if (inflate)
        for (std::size_t i = 0; i < archive.entry_count(); ++i)
            if (!archive.validate(i)) {
                check.problem = "The entry " + archive.entry_name(i) + " is damaged";
                return check;
            }
    check.ok = true;
    return check;
}

std::uint64_t required_free_space(std::uint64_t expected_archive_bytes)
{
    return expected_archive_bytes + kFreeSpaceMargin;
}

SpaceCheck check_free_space(const std::filesystem::path& directory, std::uint64_t expected_archive_bytes)
{
    SpaceCheck      check;
    std::error_code ec;
    const auto      space = std::filesystem::space(directory, ec);
    check.required        = required_free_space(expected_archive_bytes);
    if (ec)
        return check; // an unknown volume is not proof of room
    check.available = space.available;
    check.enough    = check.available >= check.required;
    return check;
}

} // namespace Slic3r::GUI::JusPrin::Project
