#include "CheckpointStore.hpp"

#include "ArchiveCheck.hpp"
#include "MeshEntry.hpp"
#include "ModelReferences.hpp"
#include "ZipFile.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

namespace Slic3r::GUI::JusPrin::Project {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr const char* kManifest      = "manifest.json";
constexpr const char* kArchive       = "project.3mf";
constexpr const char* kRestoredName  = ".3mf";
constexpr int         kManifestVersion = 1;

bool safe_id(const std::string& id)
{
    return !id.empty() && id != "." && id != ".." &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return {};
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_file(const fs::path& path, const std::string& content)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;
        out << content;
        if (!out.good())
            return false;
    }
    fs::rename(temp, path, ec);
    return !ec;
}

std::uint64_t file_bytes(const fs::path& path)
{
    std::error_code ec;
    const auto      size = fs::file_size(path, ec);
    return ec ? 0 : size;
}

std::chrono::system_clock::time_point created(const CheckpointRecord& record)
{
    return Workspace::parse_utc_timestamp(record.created_at).value_or(std::chrono::system_clock::time_point{});
}

bool older(const CheckpointRecord& lhs, const CheckpointRecord& rhs)
{
    const auto l = created(lhs), r = created(rhs);
    return l != r ? l < r : lhs.id < rhs.id;
}

} // namespace

// -- Thinning ------------------------------------------------------------------

std::vector<std::string> thinning_candidates(const std::vector<CheckpointRecord>& records,
                                             std::chrono::system_clock::time_point now, const RetentionRules& rules)
{
    std::vector<std::string> removed;
    if (records.empty())
        return removed;
    const CheckpointRecord& newest = *std::max_element(records.begin(), records.end(), older);
    // The newest checkpoint of each bucket survives. Buckets are numbered
    // from `now`, so the survivors move as time passes, which is fine: a
    // survivor is always the newest of its bucket at the moment of thinning.
    std::map<long long, const CheckpointRecord*> survivors;
    std::vector<const CheckpointRecord*>         bucketed;
    for (const CheckpointRecord& record : records) {
        if (record.keep || &record == &newest)
            continue;
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - created(record));
        if (age < rules.keep_all)
            continue;
        long long bucket = 0;
        if (age < rules.hourly)
            bucket = age.count() / 3600;
        else if (age < rules.daily)
            bucket = 1'000'000 + age.count() / 86400;
        else
            bucket = 2'000'000; // one survivor for everything older
        bucketed.push_back(&record);
        auto [it, inserted] = survivors.emplace(bucket, &record);
        if (!inserted && older(*it->second, record))
            it->second = &record;
    }
    std::set<const CheckpointRecord*> kept;
    for (const auto& [bucket, record] : survivors)
        kept.insert(record);
    for (const CheckpointRecord* record : bucketed)
        if (kept.count(record) == 0)
            removed.push_back(record->id);
    return removed;
}

// -- Store ---------------------------------------------------------------------

CheckpointStore::CheckpointStore(fs::path root) : m_root(std::move(root)) {}

std::optional<CheckpointRecord> CheckpointStore::read_manifest(const fs::path& dir) const
{
    const json manifest = json::parse(read_file(dir / kManifest), nullptr, false);
    if (!manifest.is_object() || manifest.value("version", 0) != kManifestVersion)
        return std::nullopt;
    CheckpointRecord record;
    record.id            = manifest.value("id", "");
    record.created_at    = manifest.value("createdAt", "");
    record.label         = manifest.value("label", "");
    record.reason        = manifest.value("reason", "");
    record.keep          = manifest.value("keep", false);
    record.archive_bytes = manifest.value("archiveBytes", std::uint64_t(0));
    if (record.id.empty() || !manifest.contains("meshes") || !manifest["meshes"].is_array())
        return std::nullopt;
    for (const json& mesh : manifest["meshes"]) {
        if (!mesh.is_object())
            return std::nullopt;
        record.meshes.push_back({mesh.value("path", ""), mesh.value("key", ""), mesh.value("bytes", std::uint64_t(0))});
    }
    return record;
}

bool CheckpointStore::write_manifest(const fs::path& dir, const CheckpointRecord& record) const
{
    json meshes = json::array();
    for (const StoredMesh& mesh : record.meshes)
        meshes.push_back({{"path", mesh.path}, {"key", mesh.key}, {"bytes", mesh.bytes}});
    const json manifest{{"version", kManifestVersion}, {"id", record.id},         {"createdAt", record.created_at},
                        {"label", record.label},       {"reason", record.reason}, {"keep", record.keep},
                        {"archiveBytes", record.archive_bytes}, {"meshes", meshes}};
    return write_file(dir / kManifest, manifest.dump(2));
}

std::optional<std::vector<std::uint64_t>> CheckpointStore::stored_ids(const std::string& key) const
{
    const json sidecar = json::parse(read_file(mesh_ids_path(key)), nullptr, false);
    if (!sidecar.is_object() || !sidecar.contains("ids") || !sidecar["ids"].is_array())
        return std::nullopt;
    std::vector<std::uint64_t> ids;
    for (const json& id : sidecar["ids"]) {
        if (!id.is_number_unsigned())
            return std::nullopt;
        ids.push_back(id.get<std::uint64_t>());
    }
    return ids;
}

std::optional<CheckpointRecord> CheckpointStore::record(const Request& request, std::string& error)
{
    if (!safe_id(request.id)) {
        error = "The checkpoint id is not a file name";
        return std::nullopt;
    }
    if (fs::exists(checkpoint_dir(request.id))) {
        error = "A checkpoint with that id exists";
        return std::nullopt;
    }
    const ArchiveCheck check = verify_project_archive(request.small_archive, /*meshes_elsewhere=*/true);
    if (!check.ok) {
        error = "The checkpoint archive is not whole: " + check.problem;
        return std::nullopt;
    }
    ZipReader small;
    small.open(request.small_archive);
    const auto main_model = small.read("3D/3dmodel.model");
    if (!main_model) {
        error = "The checkpoint archive has no readable main model";
        return std::nullopt;
    }
    const std::vector<MeshReference> references = mesh_references(*main_model);

    ZipReader meshes;
    if (!request.mesh_archive.empty() && !meshes.open(request.mesh_archive)) {
        error = "The mesh archive does not open";
        return std::nullopt;
    }
    std::optional<CheckpointRecord> previous;
    if (!request.reuse_from.empty()) {
        previous = find(request.reuse_from);
        if (!previous) {
            error = "The checkpoint to reuse meshes from, " + request.reuse_from + ", does not exist";
            return std::nullopt;
        }
    }

    CheckpointRecord record;
    record.id            = request.id;
    record.created_at    = request.created_at;
    record.label         = request.label;
    record.reason        = request.reason;
    record.keep          = request.keep;
    record.archive_bytes = check.bytes;

    // Meshes written this time are removed again if the checkpoint fails
    // later on; nothing refers to them yet.
    std::vector<std::string> written;
    auto                     fail = [&](std::string message) {
        for (const std::string& key : written) {
            std::error_code ec;
            fs::remove(mesh_path(key), ec);
            fs::remove(mesh_ids_path(key), ec);
        }
        error = std::move(message);
        return std::optional<CheckpointRecord>{};
    };

    for (const MeshReference& reference : references) {
        const std::optional<std::size_t> index = meshes.is_open() ? meshes.find(reference.path) : std::nullopt;
        if (index) {
            const auto text = meshes.read(*index);
            if (!text)
                return fail("The mesh " + reference.path + " does not inflate");
            const std::vector<std::uint64_t> ids = mesh_entry_ids(*text);
            if (ids.size() != reference.ids.size())
                return fail("The mesh " + reference.path + " holds " + std::to_string(ids.size()) +
                            " parts but the checkpoint refers to " + std::to_string(reference.ids.size()));
            const std::string key = mesh_entry_key(*text);
            if (!fs::exists(mesh_path(key))) {
                ZipWriter copy;
                if (!copy.open(mesh_path(key)) || !copy.add_from(meshes, *index) || !copy.finish())
                    return fail("The mesh " + reference.path + " could not be stored");
                written.push_back(key);
                json sidecar{{"ids", ids}, {"path", reference.path}};
                if (!write_file(mesh_ids_path(key), sidecar.dump()))
                    return fail("The mesh " + reference.path + " could not be stored");
            }
            record.meshes.push_back({reference.path, key, file_bytes(mesh_path(key))});
            continue;
        }
        if (previous) {
            const auto reused = std::find_if(previous->meshes.begin(), previous->meshes.end(),
                                             [&](const StoredMesh& mesh) { return mesh.path == reference.path; });
            if (reused != previous->meshes.end()) {
                const auto ids = stored_ids(reused->key);
                if (!ids || !fs::exists(mesh_path(reused->key)))
                    return fail("The stored mesh " + reference.path + " of " + previous->id + " is missing");
                if (ids->size() != reference.ids.size())
                    return fail("The stored mesh " + reference.path + " holds " + std::to_string(ids->size()) +
                                " parts but the checkpoint refers to " + std::to_string(reference.ids.size()));
                record.meshes.push_back(*reused);
                continue;
            }
        }
        return fail("No mesh was supplied for " + reference.path);
    }

    // The checkpoint folder appears whole or not at all.
    const fs::path  final_dir = checkpoint_dir(request.id);
    const fs::path  temp_dir  = final_dir.string() + ".tmp";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);
    if (ec || !fs::copy_file(request.small_archive, temp_dir / kArchive, fs::copy_options::overwrite_existing, ec) || ec ||
        !write_manifest(temp_dir, record)) {
        fs::remove_all(temp_dir, ec);
        return fail("The checkpoint could not be written");
    }
    fs::rename(temp_dir, final_dir, ec);
    if (ec) {
        fs::remove_all(temp_dir, ec);
        return fail("The checkpoint could not be written");
    }
    return record;
}

std::vector<CheckpointRecord> CheckpointStore::checkpoints() const
{
    std::vector<CheckpointRecord> records;
    std::error_code               ec;
    for (const auto& entry : fs::directory_iterator(m_root / "checkpoints", ec)) {
        if (!entry.is_directory() || entry.path().extension() == ".tmp")
            continue;
        if (auto record = read_manifest(entry.path()))
            records.push_back(std::move(*record));
    }
    std::sort(records.begin(), records.end(), older);
    return records;
}

std::optional<CheckpointRecord> CheckpointStore::find(const std::string& id) const
{
    return safe_id(id) ? read_manifest(checkpoint_dir(id)) : std::nullopt;
}

bool CheckpointStore::update(const std::string& id, const std::string& label, bool keep)
{
    auto record = find(id);
    if (!record)
        return false;
    record->label = label;
    record->keep  = keep;
    return write_manifest(checkpoint_dir(id), *record);
}

bool CheckpointStore::remove(const std::string& id)
{
    if (!safe_id(id) || !fs::exists(checkpoint_dir(id)))
        return false;
    std::error_code ec;
    fs::remove_all(checkpoint_dir(id), ec);
    return !ec;
}

bool CheckpointStore::restore(const std::string& id, const fs::path& scratch, std::string& error) const
{
    const auto record = find(id);
    if (!record) {
        error = "There is no checkpoint " + id;
        return false;
    }
    std::error_code ec;
    fs::create_directories(scratch, ec);
    if (ec || !fs::copy_file(checkpoint_dir(id) / kArchive, scratch / kRestoredName, fs::copy_options::overwrite_existing, ec) || ec) {
        error = "The checkpoint archive could not be copied";
        return false;
    }
    ZipReader archive;
    archive.open(scratch / kRestoredName);
    const auto main_model = archive.read("3D/3dmodel.model");
    if (!main_model) {
        error = "The checkpoint archive has no readable main model";
        return false;
    }
    for (const MeshReference& reference : mesh_references(*main_model)) {
        const auto stored = std::find_if(record->meshes.begin(), record->meshes.end(),
                                         [&](const StoredMesh& mesh) { return mesh.path == reference.path; });
        if (stored == record->meshes.end()) {
            error = "The checkpoint has no mesh for " + reference.path;
            return false;
        }
        ZipReader mesh;
        if (!mesh.open(mesh_path(stored->key)) || mesh.entry_count() != 1) {
            error = "The stored mesh " + reference.path + " is missing";
            return false;
        }
        const auto text = mesh.read(std::size_t(0));
        if (!text) {
            error = "The stored mesh " + reference.path + " does not inflate";
            return false;
        }
        std::string rewritten;
        try {
            rewritten = rewrite_mesh_entry_ids(*text, reference.ids);
        } catch (const std::invalid_argument& mismatch) {
            error = "The stored mesh " + reference.path + " does not match the checkpoint: " + mismatch.what();
            return false;
        }
        // Laid out like OrcaSlicer's own backup folder: the entry, under its
        // own name, inside a zip file of that name.
        ZipWriter out;
        if (!out.open(scratch / fs::path(reference.path)) || !out.add(reference.path, rewritten) || !out.finish()) {
            error = "The mesh " + reference.path + " could not be written";
            return false;
        }
    }
    return true;
}

std::uint64_t CheckpointStore::sweep()
{
    std::set<std::string> referenced;
    for (const CheckpointRecord& record : checkpoints())
        for (const StoredMesh& mesh : record.meshes)
            referenced.insert(mesh.key);
    std::uint64_t   freed = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(m_root / "meshes", ec)) {
        if (referenced.count(entry.path().stem().string()) != 0)
            continue;
        freed += file_bytes(entry.path());
        fs::remove(entry.path(), ec);
    }
    return freed;
}

std::uint64_t CheckpointStore::bytes() const
{
    std::uint64_t   total = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(m_root, ec))
        if (entry.is_regular_file(ec))
            total += file_bytes(entry.path());
    return total;
}

std::vector<std::string> CheckpointStore::apply_retention(const RetentionRules& rules,
                                                          std::chrono::system_clock::time_point now,
                                                          std::uint64_t cap_bytes)
{
    std::vector<std::string> removed = thinning_candidates(checkpoints(), now, rules);
    for (const std::string& id : removed)
        remove(id);
    if (!removed.empty())
        sweep();
    while (cap_bytes > 0 && bytes() > cap_bytes) {
        std::vector<CheckpointRecord> records = checkpoints();
        if (records.size() > rules.never_cap_newest)
            records.resize(records.size() - rules.never_cap_newest);
        else
            records.clear();
        const auto oldest = std::find_if(records.begin(), records.end(), [](const CheckpointRecord& r) { return !r.keep; });
        if (oldest == records.end())
            break;
        remove(oldest->id);
        removed.push_back(oldest->id);
        sweep();
    }
    return removed;
}

} // namespace Slic3r::GUI::JusPrin::Project
