#include "ProjectVersionStore.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <nlohmann/json.hpp>
#include <expat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

void require(bool condition, const std::string& what)
{
    if (!condition)
        throw std::runtime_error(what);
}

bool safe_component(const std::string& value)
{
    if (value.empty() || value == "." || value == ".." ||
        value.find_first_of("/\\") != std::string::npos || value.find('\0') != std::string::npos)
        return false;
#ifdef _WIN32
    if (value.find_first_of("<>:\"|?*") != std::string::npos || value.back() == ' ' || value.back() == '.')
        return false;
#endif
    return true;
}

bool safe_entry(const std::string& entry)
{
    if (entry.rfind("3D/Objects/", 0) != 0 || entry.find('\\') != std::string::npos ||
        entry.size() < 6 || entry.substr(entry.size() - 6) != ".model")
        return false;
    std::size_t start = 0;
    while (start < entry.size()) {
        const std::size_t end = entry.find('/', start);
        if (!safe_component(entry.substr(start, end == std::string::npos ? std::string::npos : end - start)))
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

std::string entry_for(const ModelObject& object)
{
    const std::string entry = "3D/Objects/" + object.name + "_" + std::to_string(object.get_backup_id()) + ".model";
    require(safe_entry(entry), "object name cannot be represented safely in a project version: " + object.name);
    return entry;
}

std::string signature_of(const ModelObject& object, int backup_id)
{
    std::ostringstream out;
    out << backup_id << '|';
    for (const ModelVolume* volume : object.volumes)
        out << volume->id().id << ':' << volume->mesh_ptr().get() << ':'
            << volume->supported_facets.timestamp() << ':' << volume->seam_facets.timestamp() << ':'
            << volume->mmu_segmentation_facets.timestamp() << ':' << volume->fuzzy_skin_facets.timestamp() << ';';
    return out.str();
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    require(in.is_open(), "cannot open " + path.string());
    std::ostringstream out;
    out << in.rdbuf();
    require(!in.bad(), "cannot read " + path.string());
    return out.str();
}

void sync_file(const fs::path& path)
{
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "cannot open for flush: " + path.string());
    const BOOL ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    require(ok != 0, "cannot flush " + path.string());
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    require(fd >= 0, "cannot open for sync: " + path.string());
#ifdef __APPLE__
    const bool ok = ::fcntl(fd, F_FULLFSYNC) == 0 || ::fsync(fd) == 0;
#else
    const bool ok = ::fsync(fd) == 0;
#endif
    ::close(fd);
    require(ok, "cannot sync " + path.string());
#endif
}

void sync_directory(const fs::path& path)
{
#ifdef _WIN32
    // Win32 FlushFileBuffers is documented for writable file/volume handles,
    // not directory handles. Files are flushed individually and publication
    // uses MOVEFILE_WRITE_THROUGH. A volume flush would require admin rights.
    (void)path;
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    require(fd >= 0, "cannot open directory for sync: " + path.string());
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    require(ok, "cannot sync directory " + path.string());
#endif
}

void checked_write(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(out.is_open(), "cannot create " + path.string());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    require(out.good(), "cannot write " + path.string());
    out.close();
    require(!out.fail(), "cannot close " + path.string());
    sync_file(path);
}

void replace_file(const fs::path& from, const fs::path& to)
{
#ifdef _WIN32
    require(MoveFileExW(from.wstring().c_str(), to.wstring().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
            "cannot publish " + to.string());
#else
    require(::rename(from.c_str(), to.c_str()) == 0, "cannot publish " + to.string());
#endif
}

void atomic_replace(const fs::path& from, const fs::path& to)
{
    replace_file(from, to);
    sync_directory(to.parent_path());
}

bool valid_zip_entry(const fs::path& path, const std::string& entry)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_reader(&zip, path.u8string()))
        return false;
    const bool valid = mz_zip_validate_archive(&zip, 0) &&
                       (entry.empty() || mz_zip_reader_locate_file(&zip, entry.c_str(), nullptr, 0) >= 0);
    close_zip_reader(&zip);
    return valid;
}

std::string archive_digest(const fs::path& path)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    require(open_zip_reader(&zip, path.u8string()), "cannot read project metadata digest");
    std::map<std::string, std::pair<mz_uint32, mz_uint64>> entries;
    for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&zip); ++index) {
        mz_zip_archive_file_stat stat;
        require(mz_zip_reader_file_stat(&zip, index, &stat), "cannot inspect project metadata entry");
        entries[stat.m_filename] = {stat.m_crc32, stat.m_uncomp_size};
    }
    close_zip_reader(&zip);
    std::ostringstream digest;
    for (const auto& [name, values] : entries)
        digest << name << '\t' << values.first << '\t' << values.second << '\n';
    return digest.str();
}

struct XmlPaths
{
    std::set<std::string> paths;
    const char* element{nullptr};
    const char* attribute{nullptr};
};

void XMLCALL collect_path(void* user, const XML_Char* element, const XML_Char** attributes)
{
    auto& result = *static_cast<XmlPaths*>(user);
    if (std::strcmp(element, result.element) != 0)
        return;
    for (const XML_Char** attribute = attributes; *attribute != nullptr; attribute += 2)
        if (std::strcmp(attribute[0], result.attribute) == 0)
            result.paths.emplace(attribute[1]);
}

std::set<std::string> archive_xml_paths(const fs::path& path, const char* zip_entry,
                                        const char* element, const char* attribute)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    require(open_zip_reader(&zip, path.u8string()), "cannot open project metadata");
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, zip_entry, &size, 0);
    close_zip_reader(&zip);
    require(data != nullptr, std::string("project metadata is missing ") + zip_entry);
    XmlPaths paths;
    paths.element = element;
    paths.attribute = attribute;
    XML_Parser parser = XML_ParserCreate(nullptr);
    require(parser != nullptr, "cannot create metadata XML parser");
    XML_SetUserData(parser, &paths);
    XML_SetStartElementHandler(parser, collect_path);
    const bool valid = size <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                       XML_Parse(parser, static_cast<const char*>(data), static_cast<int>(size), true) == XML_STATUS_OK;
    XML_ParserFree(parser);
    mz_free(data);
    require(valid, std::string("project metadata XML is invalid: ") + zip_entry);
    return paths.paths;
}

void validate_metadata(const fs::path& path, const ProjectVersionStore::Version& version)
{
    require(valid_zip_entry(path, ""), "project metadata archive is invalid");
    std::set<std::string> expected;
    for (const auto& object : version.objects)
        expected.insert("/" + object.entry);
    const auto components = archive_xml_paths(path, "3D/3dmodel.model", "component", "p:path");
    const auto relationships = expected.empty() ? std::set<std::string>{} :
        archive_xml_paths(path, "3D/_rels/3dmodel.model.rels", "Relationship", "Target");
    require(components == expected && relationships == expected,
            "project metadata does not reference exactly the captured object resources");
}

struct MeshShape
{
    std::size_t triangles{0};
    bool model{false};
    bool mesh{false};
};

void XMLCALL count_geometry(void* user, const XML_Char* element, const XML_Char**)
{
    auto& shape = *static_cast<MeshShape*>(user);
    if (std::strcmp(element, "model") == 0) shape.model = true;
    if (std::strcmp(element, "mesh") == 0) shape.mesh = true;
    if (std::strcmp(element, "triangle") == 0) ++shape.triangles;
}

void validate_resource(const fs::path& path, const std::string& entry, std::size_t expected_facets)
{
    require(valid_zip_entry(path, entry), "object resource is missing or corrupt: " + path.string());
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    require(open_zip_reader(&zip, path.u8string()), "cannot open object resource");
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, entry.c_str(), &size, 0);
    close_zip_reader(&zip);
    require(data != nullptr, "object resource has no geometry entry");
    MeshShape shape;
    XML_Parser parser = XML_ParserCreate(nullptr);
    require(parser != nullptr, "cannot create geometry XML parser");
    XML_SetUserData(parser, &shape);
    XML_SetStartElementHandler(parser, count_geometry);
    const bool valid = size <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
                       XML_Parse(parser, static_cast<const char*>(data), static_cast<int>(size), true) == XML_STATUS_OK;
    XML_ParserFree(parser);
    mz_free(data);
    // A cut can change the serialized triangle count in either direction.
    // Require a real mesh for an object with live facets, plus an intact ZIP
    // entry and valid XML. Metadata validation covers every expected object.
    require(valid && shape.model && (expected_facets == 0 || (shape.mesh && shape.triangles > 0)),
            "object resource geometry is incomplete: " + entry + " expected=" + std::to_string(expected_facets) +
            " actual=" + std::to_string(shape.triangles));
}

void validate_attachments(const fs::path& project_root, const std::string& state)
{
    const json document = json::parse(state);
    require(document.is_object() && document.contains("project") && document["project"].is_object() &&
                document.contains("attachments") && document["attachments"].is_array(),
            "project semantic state is incomplete");
    for (const json& item : document["attachments"]) {
        const std::string id = item.value("id", "");
        const std::string name = item.value("storedName", "");
        require(safe_component(id), "project attachment has an invalid identifier");
        if (name.empty())
            continue;
        require(safe_component(name), "project attachment has an invalid filename");
        const fs::path file = project_root / "attachments" / id / name;
        require(fs::is_regular_file(file) && fs::file_size(file) == item.value("sizeBytes", std::uint64_t{0}),
                "project attachment is missing or incomplete: " + id);
        sync_file(file);
        sync_directory(file.parent_path());
    }
    if (fs::exists(project_root / "attachments"))
        sync_directory(project_root / "attachments");
}

std::set<std::string> attachment_paths(const std::string& state)
{
    std::set<std::string> paths;
    const json document = json::parse(state);
    require(document.is_object() && document.contains("attachments") && document["attachments"].is_array(),
            "version attachment index is invalid");
    for (const json& item : document["attachments"]) {
        const std::string id = item.value("id", "");
        const std::string name = item.value("storedName", "");
        if (name.empty()) continue;
        require(safe_component(id) && safe_component(name), "version attachment path is invalid");
        paths.insert("attachments/" + id + "/" + name);
    }
    return paths;
}

json version_json(const ProjectVersionStore::Version& version)
{
    json objects = json::array();
    for (const auto& object : version.objects)
        objects.push_back({{"objectId", object.object_id}, {"backupId", object.backup_id}, {"facets", object.facets}, {"entry", object.entry},
                           {"resource", object.resource.id}, {"resourceEntry", object.resource.entry}});
    return {{"schema", 1}, {"id", version.id}, {"revision", version.revision},
            {"changeSeq", version.change_seq},
            {"createdAt", version.created_at}, {"sourcePath", version.source_path},
            {"projectName", version.project_name}, {"objects", std::move(objects)}};
}

ProjectVersionStore::Version parse_version(const std::string& text)
{
    const json value = json::parse(text);
    require(value.is_object() && value.value("schema", 0) == 1 && value.contains("objects") && value["objects"].is_array(),
            "unsupported project version manifest");
    ProjectVersionStore::Version version;
    version.id = value.at("id").get<std::string>();
    version.revision = value.at("revision").get<std::uint64_t>();
    version.change_seq = value.value("changeSeq", std::uint64_t{0});
    version.created_at = value.at("createdAt").get<std::string>();
    version.source_path = value.value("sourcePath", "");
    version.project_name = value.value("projectName", "");
    require(safe_component(version.id), "invalid version id");
    for (const json& item : value["objects"]) {
        ProjectVersionStore::ObjectResource object;
        object.object_id = item.at("objectId").get<std::uint64_t>();
        object.backup_id = item.at("backupId").get<int>();
        object.facets = item.at("facets").get<std::size_t>();
        object.entry = item.at("entry").get<std::string>();
        object.resource.id = item.at("resource").get<std::string>();
        object.resource.entry = item.at("resourceEntry").get<std::string>();
        require(safe_entry(object.entry) && safe_entry(object.resource.entry) && safe_component(object.resource.id),
                "invalid object resource in version manifest");
        version.objects.push_back(std::move(object));
    }
    return version;
}

void copy_or_relabel(const fs::path& source, const std::string& old_entry, const fs::path& target,
                     const std::string& new_entry)
{
    fs::create_directories(target.parent_path());
    if (old_entry == new_entry) {
        fs::copy_file(source, target);
        return;
    }
    mz_zip_archive input;
    mz_zip_zero_struct(&input);
    require(open_zip_reader(&input, source.u8string()), "cannot open object resource " + source.string());
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&input, old_entry.c_str(), &size, 0);
    require(data != nullptr, "object resource entry missing: " + old_entry);
    mz_zip_archive output;
    mz_zip_zero_struct(&output);
    const bool opened = open_zip_writer(&output, target.u8string());
    const bool ok = opened && mz_zip_writer_add_mem(&output, new_entry.c_str(), data, size, MZ_DEFAULT_COMPRESSION) &&
                    mz_zip_writer_finalize_archive(&output);
    if (opened) close_zip_writer(&output);
    close_zip_reader(&input);
    mz_free(data);
    require(ok, "cannot relabel object resource " + target.string());
}

} // namespace

ProjectVersionStore::ProjectVersionStore(fs::path root, std::string project_id)
    : m_root(std::move(root) / project_id), m_project_id(std::move(project_id))
{
    require(safe_component(m_project_id), "invalid project id");
    fs::create_directories(m_root / "versions");
    fs::create_directories(m_root / "resources");
    fs::create_directories(m_root / "pending");
    fs::create_directories(m_root / "materialized");
    fs::create_directories(m_root / "operations");
    lock();
    try {
        require(!fs::exists(m_root / "chat-restore.pending.json"),
                "an interrupted chat restoration requires recovery before this project can be edited");
        load_head();
        recover_uncommitted();
    } catch (...) {
        unlock();
        throw;
    }
}

void ProjectVersionStore::begin_chat_restore(const std::string& from_version,
                                              const std::string& to_version,
                                              const std::string& conversation_id)
{
    require(safe_component(from_version) && safe_component(to_version) && safe_component(conversation_id),
            "invalid chat restoration identity");
    const fs::path pending = m_root / "chat-restore.writing.json";
    checked_write(pending, json{{"fromVersion", from_version}, {"toVersion", to_version},
                                {"conversationId", conversation_id}}.dump(2));
    atomic_replace(pending, m_root / "chat-restore.pending.json");
}

void ProjectVersionStore::finish_chat_restore()
{
    fs::remove(m_root / "chat-restore.pending.json");
    sync_directory(m_root);
}

void ProjectVersionStore::recover_uncommitted()
{
    const std::set<std::string> committed(m_committed_ids.begin(), m_committed_ids.end());
    // The lock proves no earlier process can still be loading from these
    // disposable directories. Orca never uses the retained store as backup.
    for (const auto& entry : fs::directory_iterator(m_root / "materialized"))
        fs::remove_all(entry.path());
    for (const auto& entry : fs::directory_iterator(m_root / "pending"))
        fs::remove_all(entry.path());
    for (const auto& entry : fs::directory_iterator(m_root / "versions"))
        if (entry.is_directory() && !committed.count(entry.path().filename().string()))
            fs::remove_all(entry.path());
    std::set<std::string> reachable;
    for (const Version& version : history())
        for (const ObjectResource& object : version.objects)
            reachable.insert(object.resource.id);
    for (const auto& entry : fs::directory_iterator(m_root / "resources"))
        if (entry.is_regular_file() && !reachable.count(entry.path().stem().string()))
            fs::remove(entry.path());
    fs::remove(m_root / "HEAD.pending");
    fs::remove(m_root / "current-state.pending");
    fs::remove(m_root / "pins.pending");
    fs::remove(m_root / "chat-restore.writing.json");
    for (const auto& entry : fs::directory_iterator(m_root / "operations"))
        if (entry.path().extension() == ".pending")
            fs::remove(entry.path());
    sync_directory(m_root / "materialized");
    sync_directory(m_root / "pending");
    sync_directory(m_root / "versions");
    sync_directory(m_root / "resources");
    sync_directory(m_root / "operations");
    sync_directory(m_root);
}

ProjectVersionStore::~ProjectVersionStore() { unlock(); }

void ProjectVersionStore::lock()
{
    const fs::path path = m_root / "lock";
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "cannot open project lock");
    OVERLAPPED overlap{};
    if (LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap) == 0) {
        CloseHandle(handle);
        throw std::runtime_error("project is open in another JusPrin instance");
    }
    m_lock_handle = handle;
#else
    m_lock_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
    require(m_lock_fd >= 0, "cannot open project lock");
    if (::flock(m_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(m_lock_fd);
        m_lock_fd = -1;
        throw std::runtime_error("project is open in another JusPrin instance");
    }
#endif
}

void ProjectVersionStore::unlock()
{
#ifdef _WIN32
    if (m_lock_handle != nullptr) {
        OVERLAPPED overlap{};
        UnlockFileEx(static_cast<HANDLE>(m_lock_handle), 0, 1, 0, &overlap);
        CloseHandle(static_cast<HANDLE>(m_lock_handle));
        m_lock_handle = nullptr;
    }
#else
    if (m_lock_fd >= 0) {
        ::flock(m_lock_fd, LOCK_UN);
        ::close(m_lock_fd);
        m_lock_fd = -1;
    }
#endif
}

std::string ProjectVersionStore::new_id()
{
    static std::mt19937_64 random(std::random_device{}());
    static std::mutex random_mutex;
    const std::lock_guard<std::mutex> guard(random_mutex);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream id;
    id << millis << '-' << std::hex << std::setw(16) << std::setfill('0') << random();
    return id.str();
}

void ProjectVersionStore::validate_export_archive(const fs::path& archive)
{
    require(valid_zip_entry(archive, "3D/3dmodel.model"), "exported 3MF is incomplete");
    const auto components = archive_xml_paths(archive, "3D/3dmodel.model", "component", "p:path");
    for (const std::string& path : components) {
        require(path.size() > 1 && path.front() == '/' && safe_entry(path.substr(1)),
                "exported 3MF has an invalid object path");
        validate_resource(archive, path.substr(1), 1);
    }
}

void ProjectVersionStore::publish_export_archive(const fs::path& temporary, const fs::path& destination)
{
    require(temporary.parent_path() == destination.parent_path(), "export staging must be beside its destination");
    validate_export_archive(temporary);
    sync_file(temporary);
    atomic_replace(temporary, destination);
}

fs::path ProjectVersionStore::version_dir(const std::string& id) const { return m_root / "versions" / id; }
fs::path ProjectVersionStore::resource_path(const std::string& id) const { return m_root / "resources" / (id + ".zip"); }
fs::path ProjectVersionStore::pending_metadata_path(const std::string& id) const
{
    require(safe_component(id), "invalid version id");
    const fs::path directory = m_root / "pending" / id;
    require(!fs::exists(directory), "version id already in use");
    fs::create_directories(directory);
    return directory / "metadata.3mf";
}

void ProjectVersionStore::load_head()
{
    if (fs::exists(m_root / "HEAD")) {
        const std::string head = read_file(m_root / "HEAD");
        if (!head.empty() && head.front() == '{') {
            const json value = json::parse(head);
            require(value.value("schema", 0) == 1 && value.contains("versions") && value["versions"].is_array(),
                    "unsupported current-version index");
            m_head_id = value.at("current").get<std::string>();
            std::set<std::string> seen;
            for (const json& item : value["versions"]) {
                const std::string id = item.get<std::string>();
                require(safe_component(id) && seen.insert(id).second, "invalid committed-version index");
                m_committed_ids.push_back(id);
            }
            require(!m_committed_ids.empty() && m_committed_ids.back() == m_head_id,
                    "current version does not match committed-version index");
        } else {
            // Upgrade indexes written by the first implementation of the local
            // store. Its HEAD was a single version ID.
            m_head_id = head;
            if (!m_head_id.empty() && m_head_id.back() == '\n') m_head_id.pop_back();
            for (const auto& entry : fs::directory_iterator(m_root / "versions"))
                if (entry.is_directory() && entry.path().filename().string() <= m_head_id)
                    m_committed_ids.push_back(entry.path().filename().string());
            std::sort(m_committed_ids.begin(), m_committed_ids.end());
        }
        require(safe_component(m_head_id), "invalid current version id");
        const Version version = read_version(m_head_id);
        m_last_metadata_digest = archive_digest(version_dir(m_head_id) / "metadata.3mf");
        if (fs::exists(version_dir(m_head_id) / "state.json"))
            m_last_semantic_state = read_file(version_dir(m_head_id) / "state.json");
        for (const ObjectResource& object : version.objects)
            m_last_resources[object.object_id] = object;
    }
    if (fs::exists(m_root / "current-state.json")) {
        const json current = json::parse(read_file(m_root / "current-state.json"));
        require(current.is_object() && (current.value("schema", 0) == 1 || current.value("schema", 0) == 2),
                "current project document index is invalid");
        const int schema = current["schema"].get<int>();
        if (schema == 2 || (current.contains("baseVersion") && current["baseVersion"].is_string() &&
                            current["baseVersion"] == m_head_id)) {
            require(current.contains("state") && current["state"].is_object() &&
                    current["state"].contains("project") && current["state"]["project"].is_object() &&
                    current["state"]["project"].value("projectId", std::string()) == m_project_id,
                    "current project document does not match the project");
            m_last_semantic_state = current["state"].dump(2);
            m_document_schema = schema;
        }
    }
    if (!m_head_id.empty())
        require(!m_last_semantic_state.empty(), "current project document is missing");
    if (!m_last_semantic_state.empty())
        validate_attachments(m_root, m_last_semantic_state);
}

void ProjectVersionStore::seed_from_live(Model& live)
{
    if (m_head_id.empty())
        return;
    const Version version = read_version(m_head_id);
    if (version.objects.size() != live.objects.size())
        return;
    std::map<std::uint64_t, ObjectResource> resources;
    std::map<std::uint64_t, std::string> signatures;
    for (ModelObject* object : live.objects) {
        const int backup_id = live.get_object_backup_id(*object);
        const auto found = std::find_if(version.objects.begin(), version.objects.end(), [backup_id](const ObjectResource& item) {
            return item.backup_id == backup_id;
        });
        if (found == version.objects.end() || resources.count(object->id().id))
            return;
        resources[object->id().id] = *found;
        signatures[object->id().id] = signature_of(*object, backup_id);
    }
    m_last_resources = std::move(resources);
    m_last_signatures = std::move(signatures);
    m_last_model = std::make_shared<Model>(live);
}

ProjectVersionStore::Capture ProjectVersionStore::freeze(Model& live, const std::string& version_id,
                                                          std::uint64_t revision, std::string semantic_state,
                                                          std::string source_path, std::string created_at)
{
    require(safe_component(version_id), "invalid version id");
    const fs::path pending = m_root / "pending" / version_id;
    require(fs::exists(pending / "metadata.3mf"), "project metadata was not captured");
    require(valid_zip_entry(pending / "metadata.3mf", ""), "project metadata archive is invalid");
    publish_document(std::move(semantic_state));
    Capture capture;
    capture.version = {version_id, revision, 0, std::move(created_at), std::move(source_path), {}};
    capture.pending_dir = pending;
    capture.frozen = std::make_shared<Model>(live);
    for (std::size_t index = 0; index < live.objects.size(); ++index) {
        ModelObject& original = *live.objects[index];
        ModelObject& object = *capture.frozen->objects[index];
        const int backup_id = live.get_object_backup_id(original);
        capture.frozen->set_object_backup_id(object, backup_id);
        ObjectResource item;
        item.object_id = object.id().id;
        item.backup_id = backup_id;
        for (const ModelVolume* volume : object.volumes)
            item.facets += volume->mesh().facets_count();
        item.entry = entry_for(object);
        const std::string signature = signature_of(object, backup_id);
        capture.signatures[item.object_id] = signature;
        const auto previous = m_last_resources.find(item.object_id);
        const auto old_signature = m_last_signatures.find(item.object_id);
        if (previous != m_last_resources.end() && old_signature != m_last_signatures.end() &&
            old_signature->second == signature) {
            item.resource = previous->second.resource;
        } else {
            item.resource = {new_id(), item.entry};
            capture.changed_objects.push_back(item.object_id);
        }
        capture.version.objects.push_back(std::move(item));
    }
    validate_metadata(pending / "metadata.3mf", capture.version);
    return capture;
}

bool ProjectVersionStore::unchanged(const Capture& capture) const
{
    return !m_head_id.empty() && capture.signatures == m_last_signatures &&
           archive_digest(capture.pending_dir / "metadata.3mf") == m_last_metadata_digest;
}

void ProjectVersionStore::discard(const Capture& capture) const
{
    require(capture.pending_dir.parent_path() == m_root / "pending", "invalid pending version directory");
    fs::remove_all(capture.pending_dir);
}

void ProjectVersionStore::publish(Capture capture)
{
    require(capture.frozen != nullptr, "capture has no model");
    require(m_document_schema == 2, "project document must be durable before a model checkpoint");
    const fs::path pending = capture.pending_dir;
    validate_metadata(pending / "metadata.3mf", capture.version);
    fs::create_directories(pending / "new-resources");
    Model subset(*capture.frozen);
    for (std::size_t index = subset.objects.size(); index > 0; --index)
        if (std::find(capture.changed_objects.begin(), capture.changed_objects.end(),
                      subset.objects[index - 1]->id().id) == capture.changed_objects.end())
            subset.delete_object(index - 1);
    subset.set_backup_path((pending / "writer-private").u8string());
    if (!subset.objects.empty()) {
        const fs::path changed_file = pending / "changed.3mf";
        const std::string changed_path = changed_file.u8string();
        StoreParams params;
        params.path = changed_path.c_str();
        params.model = &subset;
        params.config = nullptr;
        params.strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::SkipAuxiliary | SaveStrategy::Zip64;
        require(store_bbs_3mf(params), "changed-object serialization failed");
        require(valid_zip_entry(changed_path, ""), "changed-object archive is invalid");
        mz_zip_archive input;
        mz_zip_zero_struct(&input);
        require(open_zip_reader(&input, changed_path), "cannot open changed-object archive");
        for (ObjectResource& object : capture.version.objects) {
            if (std::find(capture.changed_objects.begin(), capture.changed_objects.end(), object.object_id) ==
                capture.changed_objects.end())
                continue;
            const auto subset_object = std::find_if(subset.objects.begin(), subset.objects.end(), [&object](const ModelObject* item) {
                return item->id().id == object.object_id;
            });
            require(subset_object != subset.objects.end(), "changed object was omitted from the serializer input");
            object.resource.entry = entry_for(**subset_object);
            const int entry_index = mz_zip_reader_locate_file(&input, object.resource.entry.c_str(), nullptr, 0);
            require(entry_index >= 0, "serialized object resource is missing: " + object.resource.entry);
            const fs::path target = pending / "new-resources" / (object.resource.id + ".zip");
            mz_zip_archive output;
            mz_zip_zero_struct(&output);
            require(open_zip_writer(&output, target.u8string()), "cannot create object resource");
            const bool written = mz_zip_writer_add_from_zip_reader(&output, &input, entry_index) &&
                                 mz_zip_writer_finalize_archive(&output);
            close_zip_writer(&output);
            require(written, "object resource write is incomplete: " + object.entry);
            validate_resource(target, object.resource.entry, object.facets);
            sync_file(target);
        }
        close_zip_reader(&input);
        fs::remove(changed_file);
    }
    for (const ObjectResource& object : capture.version.objects) {
        const fs::path path = fs::exists(pending / "new-resources" / (object.resource.id + ".zip")) ?
            pending / "new-resources" / (object.resource.id + ".zip") : resource_path(object.resource.id);
        require(fs::exists(path), "referenced object resource is missing");
    }
    checked_write(pending / "manifest.json", version_json(capture.version).dump(2));
    sync_file(pending / "metadata.3mf");
    sync_directory(pending);
    for (const ObjectResource& object : capture.version.objects) {
        const fs::path staged = pending / "new-resources" / (object.resource.id + ".zip");
        if (fs::exists(staged))
            atomic_replace(staged, resource_path(object.resource.id));
    }
    fs::remove_all(pending / "new-resources");
    fs::remove_all(pending / "writer-private");
    sync_directory(m_root / "resources");
    if (m_publish_stage_hook) m_publish_stage_hook(PublishStage::ResourcesDurable);
    sync_directory(pending);
    atomic_replace(pending, version_dir(capture.version.id));
    if (m_publish_stage_hook) m_publish_stage_hook(PublishStage::VersionPublished);
    auto committed = m_committed_ids;
    committed.push_back(capture.version.id);
    std::map<std::uint64_t, ObjectResource> next_resources;
    for (const ObjectResource& object : capture.version.objects)
        next_resources[object.object_id] = object;
    std::string next_head = capture.version.id;
    std::string next_digest = archive_digest(version_dir(capture.version.id) / "metadata.3mf");
    checked_write(m_root / "HEAD.pending",
                  json{{"schema", 1}, {"current", capture.version.id}, {"versions", committed}}.dump());
    replace_file(m_root / "HEAD.pending", m_root / "HEAD");
    m_committed_ids.swap(committed);
    m_head_id.swap(next_head);
    m_last_resources.swap(next_resources);
    m_last_signatures = std::move(capture.signatures);
    m_last_model = std::move(capture.frozen);
    m_last_metadata_digest.swap(next_digest);
    m_head_needs_sync = true;
    ensure_durable_head();
    if (m_publish_stage_hook) m_publish_stage_hook(PublishStage::HeadPublished);
}

void ProjectVersionStore::publish_document(std::string semantic_state)
{
    const json state = json::parse(semantic_state);
    require(state.is_object() && state.contains("project") && state["project"].is_object(),
            "project document has no identity");
    const std::string document_id = state["project"].value("projectId", std::string());
    require(document_id == m_project_id,
            "project document " + document_id + " does not match local project " + m_project_id);
    if (m_document_schema == 2 && semantic_state == m_last_semantic_state)
        return;
    validate_attachments(m_root, semantic_state);
    checked_write(m_root / "current-state.pending",
                  json{{"schema", 2}, {"state", state}}.dump());
    atomic_replace(m_root / "current-state.pending", m_root / "current-state.json");
    m_last_semantic_state = std::move(semantic_state);
    m_document_schema = 2;
}

void ProjectVersionStore::ensure_durable_head()
{
    if (!m_head_needs_sync)
        return;
    sync_directory(m_root);
    m_head_needs_sync = false;
}

ProjectVersionStore::Version ProjectVersionStore::read_version(const std::string& id) const
{
    require(safe_component(id), "invalid version id");
    Version version = parse_version(read_file(version_dir(id) / "manifest.json"));
    require(version.id == id, "version manifest id does not match directory");
    return version;
}

std::vector<ProjectVersionStore::Version> ProjectVersionStore::history() const
{
    std::vector<Version> versions;
    for (const std::string& id : m_committed_ids)
        if (fs::exists(version_dir(id) / "manifest.json"))
            versions.push_back(read_version(id));
    return versions;
}

ProjectVersionStore::Materialization ProjectVersionStore::materialize(const std::string& version_id) const
{
    const Version version = read_version(version_id);
    Materialization result;
    result.directory = m_root / "materialized" / new_id();
    result.metadata = result.directory / ".3mf";
    result.absent_original = result.directory / "source-unavailable.3mf";
    result.empty = version.objects.empty();
    fs::create_directories(result.directory / "3D" / "Objects");
    const fs::path stored_metadata = version_dir(version_id) / "metadata.3mf";
    validate_metadata(stored_metadata, version);
    fs::copy_file(stored_metadata, result.metadata);
    result.semantic_state = m_last_semantic_state;
    require(json::parse(result.semantic_state).is_object(), "current project document is invalid");
    validate_attachments(m_root, result.semantic_state);
    for (const ObjectResource& object : version.objects) {
        const fs::path source = resource_path(object.resource.id);
        validate_resource(source, object.resource.entry, object.facets);
        copy_or_relabel(source, object.resource.entry, result.directory / fs::u8path(object.entry), object.entry);
    }
    return result;
}

void ProjectVersionStore::remove_materialization(const Materialization& materialization) const
{
    require(materialization.directory.parent_path() == m_root / "materialized", "invalid materialization directory");
    fs::remove_all(materialization.directory);
}

void ProjectVersionStore::pin(const std::string& version_id)
{
    read_version(version_id);
    std::set<std::string> pins;
    if (fs::exists(m_root / "pins.json"))
        for (const json& item : json::parse(read_file(m_root / "pins.json")))
            pins.insert(item.get<std::string>());
    pins.insert(version_id);
    checked_write(m_root / "pins.pending", json(pins).dump());
    atomic_replace(m_root / "pins.pending", m_root / "pins.json");
}

void ProjectVersionStore::record_operation(const std::string& action_id, const std::string& tool,
                                           const std::string& outcome, const std::string& before,
                                           const std::string& after)
{
    require(safe_component(action_id) && safe_component(before) && (after.empty() || safe_component(after)),
            "invalid operation version reference");
    read_version(before);
    if (!after.empty())
        read_version(after);
    const fs::path directory = m_root / "operations";
    const fs::path file = directory / (action_id + ".json");
    require(!fs::exists(file), "operation version record already exists");
    const fs::path staged = directory / (action_id + ".pending");
    checked_write(staged, json{{"schema", 1}, {"actionId", action_id}, {"tool", tool},
                               {"outcome", outcome}, {"before", before}, {"after", after}}.dump(2));
    atomic_replace(staged, file);
}

void ProjectVersionStore::prune(std::size_t max_versions, std::uintmax_t max_bytes,
                                const std::set<std::string>& live_attachments)
{
    require(max_versions >= 1 && max_bytes > 0, "invalid retention budget");
    std::set<std::string> protected_ids{m_head_id};
    if (fs::exists(m_root / "pins.json"))
        for (const json& item : json::parse(read_file(m_root / "pins.json")))
            protected_ids.insert(item.get<std::string>());
    for (const auto& entry : fs::directory_iterator(m_root / "operations")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        const json operation = json::parse(read_file(entry.path()));
        for (const char* field : {"before", "after"}) {
            const std::string id = operation.value(field, "");
            if (!id.empty()) protected_ids.insert(id);
        }
    }
    auto versions = history();
    const auto reachable_files = [this, &live_attachments](const std::vector<Version>& retained) {
        std::set<std::string> resources;
        std::set<std::string> attachments = live_attachments;
        if (!m_last_semantic_state.empty()) {
            const auto current_paths = attachment_paths(m_last_semantic_state);
            attachments.insert(current_paths.begin(), current_paths.end());
        }
        std::uintmax_t bytes = 0;
        for (const Version& version : retained) {
            for (const auto& entry : fs::recursive_directory_iterator(version_dir(version.id)))
                if (entry.is_regular_file()) bytes += entry.file_size();
            for (const ObjectResource& object : version.objects)
                resources.insert(object.resource.id);
            if (fs::exists(version_dir(version.id) / "state.json")) {
                const auto paths = attachment_paths(read_file(version_dir(version.id) / "state.json"));
                attachments.insert(paths.begin(), paths.end());
            }
        }
        for (const std::string& id : resources)
            bytes += fs::file_size(resource_path(id));
        for (const std::string& path : attachments)
            if (fs::exists(m_root / fs::u8path(path)))
                bytes += fs::file_size(m_root / fs::u8path(path));
        return std::make_tuple(bytes, resources, attachments);
    };
    std::set<std::string> removed;
    while (true) {
        const auto bytes = std::get<0>(reachable_files(versions));
        if (versions.size() <= max_versions && bytes <= max_bytes)
            break;
        const auto candidate = std::find_if(versions.begin(), versions.end(), [&protected_ids](const Version& version) {
            return !protected_ids.count(version.id);
        });
        if (candidate == versions.end())
            break; // The budget is soft when all remaining history is protected.
        removed.insert(candidate->id);
        versions.erase(candidate);
    }
    const auto [retained_bytes, kept_resources, kept_attachments] = reachable_files(versions);
    (void)retained_bytes;
    if (!removed.empty()) {
        std::vector<std::string> committed;
        for (const std::string& id : m_committed_ids)
            if (!removed.count(id)) committed.push_back(id);
        // Publish reachability before deleting files. A crash after this point
        // leaves only orphaned data, which recover_uncommitted() can remove.
        checked_write(m_root / "HEAD.pending",
                      json{{"schema", 1}, {"current", m_head_id}, {"versions", committed}}.dump());
        atomic_replace(m_root / "HEAD.pending", m_root / "HEAD");
        m_committed_ids.swap(committed);
        for (const std::string& id : removed)
            fs::remove_all(version_dir(id));
        sync_directory(m_root / "versions");
    }
    for (const auto& resource : fs::directory_iterator(m_root / "resources"))
        if (resource.is_regular_file() && !kept_resources.count(resource.path().stem().string()))
            fs::remove(resource.path());
    sync_directory(m_root / "resources");
    if (fs::exists(m_root / "attachments")) {
        for (const auto& entry : fs::recursive_directory_iterator(m_root / "attachments"))
            if (entry.is_regular_file() &&
                !kept_attachments.count(fs::relative(entry.path(), m_root).generic_u8string()))
                fs::remove(entry.path());
        sync_directory(m_root / "attachments");
    }
}

} // namespace Slic3r::GUI::JusPrin::Workspace
