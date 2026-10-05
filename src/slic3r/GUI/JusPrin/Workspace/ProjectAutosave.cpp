#include "slic3r/GUI/I18N.hpp"

#include "ProjectAutosave.hpp"
#include "Regions.hpp"

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Support/Base64.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kProbeInterval = std::chrono::seconds(2);
constexpr auto kEditQuietPeriod = std::chrono::milliseconds(1500);
constexpr auto kSemanticMaxDelay = std::chrono::seconds(15);
constexpr std::size_t kMaxVersions = 200;
constexpr std::uintmax_t kMaxBytes = std::uintmax_t{4} * 1024 * 1024 * 1024;
constexpr std::size_t kMaxPreviewBytes = 2 * 1024 * 1024;

std::filesystem::path preview_path(const std::filesystem::path& project)
{
    return project / "preview.json";
}

std::string saved_preview(const std::filesystem::path& project, const std::string& version)
{
    std::ifstream file(preview_path(project), std::ios::binary);
    if (!file) return {};
    const auto saved = nlohmann::json::parse(file, nullptr, false);
    if (!saved.is_object() || !saved.contains("version") || !saved["version"].is_string() ||
        !saved.contains("url") || !saved["url"].is_string() || saved["version"] != version)
        return {};
    const std::string url = saved["url"].get<std::string>();
    return url.size() <= (kMaxPreviewBytes * 4 / 3 + 64) &&
           url.rfind("data:image/png;base64,", 0) == 0 ? url : std::string();
}

void write_preview(const std::filesystem::path& project, const std::string& version, const std::string& url)
{
    const auto temporary = project / ("preview-" + ProjectVersionStore::new_id() + ".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << nlohmann::json{{"version", version}, {"url", url}}.dump();
        file.flush();
        if (!file) throw std::runtime_error("Could not write local project preview");
    }
    std::error_code error;
#ifdef _WIN32
    std::filesystem::remove(preview_path(project), error);
    error.clear();
#endif
    std::filesystem::rename(temporary, preview_path(project), error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Could not publish local project preview: " + error.message());
    }
}

std::string source_preview(const std::string& source)
{
    const auto path = std::filesystem::u8path(source);
    std::error_code error;
    if (path.extension() != ".3mf" || !std::filesystem::is_regular_file(path, error))
        return {};
    const std::string png = bbs_3mf_get_thumbnail(source.c_str());
    if (png.size() < 8 || png.size() > kMaxPreviewBytes ||
        png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0)
        return {};
    return "data:image/png;base64," + base64_encode(png);
}

boost::filesystem::path boost_path(const std::filesystem::path& path)
{
#ifdef _WIN32
    return boost::filesystem::path(path.wstring());
#else
    return boost::filesystem::path(path.string());
#endif
}

bool project_edit(WorkspaceChangeReasons reasons)
{
    return has_reason(reasons, WorkspaceChangeReasons::Project) ||
           has_reason(reasons, WorkspaceChangeReasons::Contents) ||
           has_reason(reasons, WorkspaceChangeReasons::Transform) ||
           has_reason(reasons, WorkspaceChangeReasons::Plates) ||
           has_reason(reasons, WorkspaceChangeReasons::Settings) ||
           has_reason(reasons, WorkspaceChangeReasons::History);
}

bool authored_head(const std::filesystem::path& project)
{
    std::ifstream head(project / "HEAD", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(head)), std::istreambuf_iterator<char>());
    if (content.empty()) return false;
    std::string id;
    if (content.front() == '{')
        id = nlohmann::json::parse(content).at("current").get<std::string>();
    else {
        id = content;
        if (!id.empty() && id.back() == '\n') id.pop_back();
    }
    const auto version = project / "versions" / id;
    std::ifstream manifest_file(version / "manifest.json", std::ios::binary);
    const auto manifest = nlohmann::json::parse(manifest_file);
    if (!manifest.at("objects").empty())
        return true;
    const auto current_path = project / "current-state.json";
    if (std::filesystem::exists(current_path)) {
        std::ifstream current_file(current_path, std::ios::binary);
        const auto current = nlohmann::json::parse(current_file);
        if (current.value("schema", 0) == 2 ||
            (current.value("schema", 0) == 1 && current.value("baseVersion", std::string()) == id))
            return current.at("state").value("docRevision", std::uint64_t{0}) > 1;
    }
    const auto legacy_path = version / "state.json";
    if (!std::filesystem::exists(legacy_path))
        return false;
    std::ifstream legacy_file(legacy_path, std::ios::binary);
    return nlohmann::json::parse(legacy_file).value("docRevision", std::uint64_t{0}) > 1;
}

bool same_source_path(const std::filesystem::path& first, const std::filesystem::path& second)
{
    if (first.empty() || second.empty()) return false;
    std::error_code error;
    if (std::filesystem::exists(first, error) && !error &&
        std::filesystem::exists(second, error) && !error &&
        std::filesystem::equivalent(first, second, error) && !error)
        return true;
    error.clear();
    const auto normalized_first = std::filesystem::weakly_canonical(first, error);
    if (error) return false;
    error.clear();
    const auto normalized_second = std::filesystem::weakly_canonical(second, error);
    if (error) return false;
#ifdef _WIN32
    std::wstring left = normalized_first.wstring(), right = normalized_second.wstring();
    std::transform(left.begin(), left.end(), left.begin(), [](wchar_t value) { return std::towlower(value); });
    std::transform(right.begin(), right.end(), right.begin(), [](wchar_t value) { return std::towlower(value); });
    return left == right;
#else
    return normalized_first == normalized_second;
#endif
}

void append_text(std::ostringstream& out, const std::string& value)
{
    out << value.size() << ':' << value;
}

void append_config(std::ostringstream& out, const DynamicPrintConfig& config)
{
    auto keys = config.keys();
    std::sort(keys.begin(), keys.end());
    out << keys.size() << ';';
    for (const std::string& key : keys) {
        append_text(out, key);
        append_text(out, config.opt_serialize(key));
    }
}

void append_transform(std::ostringstream& out, const Transform3d& transform)
{
    for (int index = 0; index < 16; ++index)
        out << transform.matrix().data()[index] << ',';
}

// Managed checkpoints keep their captured settings; ordinary 3MF exports keep Orca's
// actual preset-difference list. Rewrite only the unpublished backup archive.
void protect_saved_settings(const std::filesystem::path& metadata)
{
    constexpr const char* entry = "Metadata/project_settings.config";
    mz_zip_archive source, target;
    mz_zip_zero_struct(&source);
    mz_zip_zero_struct(&target);
    if (!open_zip_reader(&source, metadata.u8string()))
        throw std::runtime_error("Could not read captured project metadata");

    bool target_open = false;
    auto rewritten = metadata;
    rewritten += ".rewrite";
    try {
        const int entry_index = mz_zip_reader_locate_file(&source, entry, nullptr, 0);
        if (entry_index < 0)
            throw std::runtime_error("Captured project settings are missing");
        size_t size = 0;
        void* data = mz_zip_reader_extract_file_to_heap(&source, entry, &size, 0);
        if (!data)
            throw std::runtime_error("Could not read captured project settings");
        std::string original(static_cast<const char*>(data), size);
        mz_free(data);

        auto settings = nlohmann::json::parse(original);
        const auto& colours = settings.at("filament_colour");
        if (!colours.is_array() || colours.empty())
            throw std::runtime_error("Captured project has no filament settings");
        std::vector<std::string> keys;
        for (auto it = settings.begin(); it != settings.end(); ++it)
            if (it.key() != "version" && it.key() != "name" && it.key() != "from" &&
                it.key() != "different_settings_to_system")
                keys.push_back(it.key());
        const std::string protected_keys = escape_strings_cstyle(keys);
        settings["different_settings_to_system"] =
            std::vector<std::string>(colours.size() + 2, protected_keys);
        const std::string replacement = settings.dump(1, '\t') + '\n';

        if (!open_zip_writer(&target, rewritten.u8string()))
            throw std::runtime_error("Could not rewrite captured project metadata");
        target_open = true;
        for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&source); ++index) {
            const bool added = int(index) == entry_index ?
                mz_zip_writer_add_mem(&target, entry, replacement.data(), replacement.size(), MZ_DEFAULT_COMPRESSION) :
                mz_zip_writer_add_from_zip_reader(&target, &source, index);
            if (!added)
                throw std::runtime_error("Could not copy captured project metadata entry");
        }
        if (!mz_zip_writer_finalize_archive(&target))
            throw std::runtime_error("Could not finalize captured project metadata");
        target_open = false;
        if (!close_zip_writer(&target))
            throw std::runtime_error("Could not close rewritten project metadata");
        if (!close_zip_reader(&source))
            throw std::runtime_error("Could not close captured project metadata");
#ifdef _WIN32
        // This archive is still unpublished staging data; Windows cannot rename over it.
        std::filesystem::remove(metadata);
#endif
        std::filesystem::rename(rewritten, metadata);
    } catch (...) {
        if (target_open) close_zip_writer(&target);
        if (source.m_zip_mode == MZ_ZIP_MODE_READING) close_zip_reader(&source);
        std::error_code ignored;
        std::filesystem::remove(rewritten, ignored);
        throw;
    }
}

} // namespace

ProjectAutosave::ProjectAutosave(Plater& plater, Agent::ProjectPersistence& persistence, IWorkspace& workspace,
                                 std::filesystem::path root)
    : m_plater(plater), m_persistence(persistence), m_workspace(workspace), m_root(std::move(root)),
      m_next_probe(Clock::now()), m_last_edit(Clock::now()), m_last_semantic_edit(Clock::now())
{
    m_subscription = m_workspace.subscribe([this](const WorkspaceChanged& change) {
        if (!project_edit(change.reasons))
            return;
        ++m_event_sequence;
        m_last_edit = Clock::now();
        m_state = State::Saving;
    });
    adopt_project();
}

ProjectAutosave::~ProjectAutosave()
{
    m_subscription.reset();
    if (m_worker.valid())
        m_worker.wait();
}

std::string ProjectAutosave::project_name() const
{
    return m_project_name.empty() ? std::string(m_plater.get_project_name().ToUTF8()) : m_project_name;
}

void ProjectAutosave::fail(const std::string& message)
{
    BOOST_LOG_TRIVIAL(error) << "Local project save failed: " << message;
    m_error = message;
    m_state = State::Failed;
}

void ProjectAutosave::observe_document_change()
{
    const auto revision = m_persistence.document().doc_revision();
    if (revision == m_seen_semantic_revision)
        return;
    const auto now = Clock::now();
    m_seen_semantic_revision = revision;
    m_last_semantic_edit = now;
    if (!m_first_semantic_edit)
        m_first_semantic_edit = now;
    m_state = State::Saving;
}

std::string ProjectAutosave::observed_input() const
{
    // Only native printable project state belongs in checkpoint detection.
    // Conversation, plans, intent, audit, and print facts are durable in the
    // current document and cannot force a model export.
    // The scan touches identities, timestamps and small metadata, never mesh
    // triangles or Orca's 3MF writer.
    std::ostringstream out;
    out << std::setprecision(17) << m_event_sequence << ';';
    append_text(out, m_persistence.document().project_id());
    append_text(out, m_plater.get_project_name().ToUTF8().data());
    append_text(out, m_plater.get_project_filename(".3mf").ToUTF8().data());
    const Model& model = m_plater.model();
    out << model.id().id << ';' << model.objects.size() << ';';
    if (model.model_info) {
        append_text(out, model.model_info->cover_file);
        append_text(out, model.model_info->description);
        append_text(out, model.model_info->model_name);
    }
    for (const auto& [plate_index, codes] : model.plates_custom_gcodes) {
        out << plate_index << ',' << int(codes.mode) << ',' << codes.gcodes.size() << ';';
        for (const auto& code : codes.gcodes) {
            out << code.print_z << ',' << int(code.type) << ',' << code.extruder << ';';
            append_text(out, code.color);
            append_text(out, code.extra);
        }
    }
    for (const ModelObject* object : model.objects) {
        out << object->id().id << ',' << object->printable << ','
            << static_cast<const ModelConfig&>(object->config).timestamp()
            << ',' << object->layer_height_profile.timestamp() << ';';
        append_text(out, object->name);
        out << object->brim_points.size() << ';';
        for (const auto& point : object->brim_points)
            out << point.pos.x() << ',' << point.pos.y() << ',' << point.pos.z() << ',' << point.head_front_radius << ';';
        out << object->volumes.size() << ';';
        for (const ModelVolume* volume : object->volumes) {
            out << volume->id().id << ',' << volume->mesh_ptr().get() << ',' << int(volume->type())
                << ',' << static_cast<const ModelConfig&>(volume->config).timestamp()
                << ',' << volume->supported_facets.timestamp()
                << ',' << volume->seam_facets.timestamp() << ',' << volume->mmu_segmentation_facets.timestamp()
                << ',' << volume->fuzzy_skin_facets.timestamp() << ';';
            append_text(out, volume->name);
            append_text(out, volume->material_id());
            append_transform(out, volume->get_matrix());
        }
        out << object->instances.size() << ';';
        for (const ModelInstance* instance : object->instances) {
            out << instance->id().id << ',' << instance->printable << ',' << instance->auto_drop
                << ',' << instance->arrange_order << ';';
            append_transform(out, instance->get_matrix());
        }
    }
    PartPlateList& plates = m_plater.get_partplate_list();
    out << plates.get_plate_count() << ';';
    for (int index = 0; index < plates.get_plate_count(); ++index) {
        PartPlate* plate = plates.get_plate(index);
        out << plate->id().id << ',' << int(plate->get_bed_type()) << ',' << plate->is_locked()
            << ',' << int(plate->get_filament_map_mode()) << ';';
        append_text(out, plate->get_plate_name());
        for (int filament : plate->get_filament_maps()) out << filament << ',';
        out << ';';
        append_config(out, *plate->config());
    }
    if (const PresetBundle* presets = wxGetApp().preset_bundle)
        append_config(out, presets->full_config_secure());
    return out.str();
}

void ProjectAutosave::trim_history()
{
    try {
        std::set<std::string> live_attachments;
        for (const auto& attachment : m_persistence.document().attachments())
            if (!attachment.stored_name.empty())
                live_attachments.insert(attachment.relative_path());
        m_store->prune(kMaxVersions, kMaxBytes, live_attachments);
        m_warning.clear();
    } catch (const std::exception& error) {
        // The current head was already committed. A retention failure leaves
        // extra old data and must not misreport the current project as unsaved.
        m_warning = std::string("Local history cleanup failed: ") + error.what();
        BOOST_LOG_TRIVIAL(warning) << m_warning;
    }
}

bool ProjectAutosave::settle(bool wait)
{
    if (!m_worker.valid())
        return true;
    if (!wait && m_worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    try {
        m_worker.get();
        if (m_worker_kind == WorkerKind::Model) {
            m_committed_head = m_store->head_id();
            m_last_capture_input = std::move(m_inflight_capture_input);
            m_seen_model_input = m_last_capture_input;
            trim_history();
        }
        if (m_worker_kind == WorkerKind::Document)
            m_committed_semantic_revision = m_inflight_semantic_revision;
        m_worker_kind = WorkerKind::None;
        if (m_seen_semantic_revision == m_committed_semantic_revision)
            m_first_semantic_edit.reset();
        m_error.clear();
        // A main-thread edit may have landed while the worker wrote the
        // frozen revision. Probe again before saying the current model is saved.
        m_state = State::Saving;
        m_next_probe = Clock::now();
        return true;
    } catch (const std::exception& error) {
        m_worker_kind = WorkerKind::None;
        m_inflight_capture_input.clear();
        fail(error.what());
    } catch (...) {
        m_worker_kind = WorkerKind::None;
        m_inflight_capture_input.clear();
        fail("project version writer failed");
    }
    return false;
}

void ProjectAutosave::adopt_project()
{
    m_persistence.resolve_pending_boundary();
    const std::string id = m_persistence.document().project_id();
    if (m_store && m_store->project_id() == id)
        return;
    settle(true);
    m_store.reset();
    if (id.empty())
        throw std::runtime_error("open project has no identity");
    m_store = std::make_unique<ProjectVersionStore>(m_root, id);
    m_last_capture_input.clear();
    m_seen_model_input.clear();
    m_inflight_capture_input.clear();
    m_committed_head = m_store->head_id();
    m_committed_semantic_revision = m_committed_head.empty() ? 0 : m_persistence.document().doc_revision();
    m_seen_semantic_revision = m_persistence.document().doc_revision();
    m_last_semantic_edit = Clock::now();
    m_first_semantic_edit = m_seen_semantic_revision != m_committed_semantic_revision ?
        std::optional<Clock::time_point>(m_last_semantic_edit) : std::nullopt;
    const auto versions = m_store->history();
    m_project_name = versions.empty() ? std::string() : versions.back().project_name;
    m_revision = 0;
    for (const auto& version : versions)
        m_revision = std::max(m_revision, version.revision);
    m_state = State::Saving;
    m_next_probe = Clock::now();
}

bool ProjectAutosave::capture(bool wait_for_commit)
{
    if (!m_store)
        return false;
    std::filesystem::path metadata;
    try {
        const std::string id = ProjectVersionStore::new_id();
        metadata = m_store->pending_metadata_path(id);
        ++m_capture_attempts;
        const int result = m_plater.export_3mf(boost_path(metadata),
                                                SaveStrategy::Backup | SaveStrategy::Silence);
        if (result < 0)
            throw std::runtime_error("OrcaSlicer could not capture project metadata");
        protect_saved_settings(metadata);
        const wxString source = m_plater.get_project_filename(".3mf");
        auto frozen = m_store->freeze(m_plater.model(), id, ++m_revision, m_persistence.document().dump(),
                                      source.ToUTF8().data(), m_persistence.timestamp());
        const auto& changes = m_persistence.document().changes();
        if (!changes.empty())
            frozen.version.change_seq = changes.back().seq;
        const auto semantic_revision = m_persistence.document().doc_revision();
        m_committed_semantic_revision = semantic_revision;
        if (m_seen_semantic_revision == semantic_revision)
            m_first_semantic_edit.reset();
        const std::string plater_name(m_plater.get_project_name().ToUTF8());
        frozen.version.project_name = m_project_name.empty() ? plater_name : m_project_name;
        if ((plater_name.empty() || plater_name == _L("Untitled").ToUTF8().data()) &&
            !m_plater.model().objects.empty())
            frozen.version.project_name = std::filesystem::u8path(m_plater.model().objects.front()->name).stem().u8string();
        m_project_name = frozen.version.project_name;
        if (m_store->unchanged(frozen)) {
            m_store->discard(frozen);
            m_last_capture_input = observed_input();
            m_seen_model_input = m_last_capture_input;
            m_seen_semantic_revision = semantic_revision;
            m_first_semantic_edit.reset();
            m_error.clear();
            m_state = State::Saved;
            return true;
        }
        m_state = State::Saving;
        if (wait_for_commit) {
            m_store->publish(std::move(frozen));
            m_committed_head = m_store->head_id();
            m_last_capture_input = observed_input();
            m_seen_model_input = m_last_capture_input;
            m_seen_semantic_revision = semantic_revision;
            m_first_semantic_edit.reset();
            trim_history();
            m_error.clear();
            m_state = State::Saved;
        } else {
            m_inflight_capture_input = observed_input();
            m_seen_model_input = m_inflight_capture_input;
            m_inflight_semantic_revision = semantic_revision;
            m_worker_kind = WorkerKind::Model;
            m_worker = std::async(std::launch::async, [this, frozen = std::move(frozen)]() mutable {
                m_store->publish(std::move(frozen));
            });
        }
        return true;
    } catch (const std::exception& error) {
        m_worker_kind = WorkerKind::None;
        if (!metadata.empty())
            std::filesystem::remove_all(metadata.parent_path());
        fail(error.what());
    }
    return false;
}

bool ProjectAutosave::persist_document(bool wait_for_commit)
{
    if (!m_store)
        return false;
    try {
        const std::string state = m_persistence.document().dump();
        const auto revision = m_persistence.document().doc_revision();
        m_state = State::Saving;
        if (wait_for_commit) {
            m_store->publish_document(state);
            m_committed_semantic_revision = revision;
            m_seen_semantic_revision = revision;
            m_first_semantic_edit.reset();
            m_error.clear();
            m_state = State::Saved;
        } else {
            m_inflight_semantic_revision = revision;
            m_worker_kind = WorkerKind::Document;
            m_worker = std::async(std::launch::async, [this, state] {
                m_store->publish_document(state);
            });
        }
        return true;
    } catch (const std::exception& error) {
        m_worker_kind = WorkerKind::None;
        fail(error.what());
        return false;
    }
}

void ProjectAutosave::tick()
{
    try {
        if (!m_resume_attempted) {
            GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
            if (canvas == nullptr || !canvas->is_initialized())
                return;
            resume_latest();
        }
        adopt_project();
        if (!settle(false) && m_worker.valid())
            return;
        observe_document_change();
        const auto now = Clock::now();
        if (now < m_next_probe)
            return;
        m_next_probe = now + kProbeInterval;
        if (m_plater.model().objects.empty() && m_store->head_id().empty() &&
            m_persistence.document().doc_revision() <= 1) {
            m_state = State::Saved;
            return;
        }
        const std::string model_input = observed_input();
        if (model_input != m_seen_model_input) {
            m_seen_model_input = model_input;
            m_last_edit = now;
            m_state = State::Saving;
        }
        const bool document_pending = m_persistence.document().doc_revision() != m_committed_semantic_revision;
        const bool document_due = document_pending &&
            (now - m_last_semantic_edit >= kEditQuietPeriod ||
             (m_first_semantic_edit && now - *m_first_semantic_edit >= kSemanticMaxDelay));
        // A continuously edited model must not starve the current document.
        // Model capture also publishes the document, so a ready model still
        // takes precedence until the document's maximum delay is reached.
        if (document_due && m_first_semantic_edit && now - *m_first_semantic_edit >= kSemanticMaxDelay) {
            persist_document(false);
            return;
        }
        if (m_store->head_id().empty() || model_input != m_last_capture_input) {
            GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
            if (canvas == nullptr || !canvas->is_initialized() ||
                canvas->get_gizmos_manager().is_in_editing_mode(true) ||
                now - m_last_edit < kEditQuietPeriod)
                return;
            capture(false);
        } else if (document_pending) {
            if (!document_due)
                return;
            persist_document(false);
        } else {
            m_state = State::Saved;
            m_first_semantic_edit.reset();
        }
    } catch (const std::exception& error) {
        fail(error.what());
    }
}

bool ProjectAutosave::resume_latest()
{
    m_resume_attempted = true;
    try {
        if (!m_plater.model().objects.empty() || !m_plater.get_project_filename(".3mf").empty() ||
            m_persistence.document().doc_revision() > 1)
            return false;
        std::filesystem::path latest;
        std::filesystem::file_time_type latest_time{};
        if (!std::filesystem::is_directory(m_root))
            return false;
        for (const auto& project : std::filesystem::directory_iterator(m_root)) {
            if (!project.is_directory() || !std::filesystem::is_regular_file(project.path() / "HEAD"))
                continue;
            if (m_store && project.path().filename().string() == m_store->project_id())
                continue;
            try {
                if (!authored_head(project.path()))
                    continue;
            } catch (const std::exception& error) {
                BOOST_LOG_TRIVIAL(warning) << "Recent local project index is invalid: " << project.path().string()
                                           << ": " << error.what();
                continue;
            }
            auto modified = std::filesystem::last_write_time(project.path() / "HEAD");
            if (std::filesystem::is_regular_file(project.path() / "current-state.json"))
                modified = std::max(modified, std::filesystem::last_write_time(project.path() / "current-state.json"));
            if (latest.empty() || modified > latest_time) {
                latest = project.path();
                latest_time = modified;
            }
        }
        if (latest.empty())
            return false;
        // The untouched startup draft has no authored state. Release its
        // lock before opening the durable project selected by the store.
        settle(true);
        m_store.reset();
        auto opened = std::make_unique<ProjectVersionStore>(m_root, latest.filename().string());
        const std::string head = opened->head_id();
        if (head.empty())
            throw std::runtime_error("recent managed project has no current version");
        const auto materialized = opened->materialize(head);
        const auto versions = opened->history();
        const auto selected = std::find_if(versions.begin(), versions.end(), [&head](const auto& item) {
            return item.id == head;
        });
        if (selected == versions.end())
            throw std::runtime_error("recent managed project has no current manifest");
        m_plater.reset();
        m_plater.take_snapshot("Resume local project", UndoRedo::SnapshotType::ProjectSeparator);
        auto strategy = LoadStrategy::Restore;
        if (materialized.empty)
            strategy = strategy | LoadStrategy::AllowEmpty;
        const auto loaded = m_plater.load_files(
            std::vector<boost::filesystem::path>{boost_path(materialized.metadata),
                                                 boost_path(materialized.absent_original)},
            strategy, false);
        if ((loaded.empty() && !materialized.empty) || (materialized.empty && !m_plater.model().objects.empty()))
            throw std::runtime_error("recent managed project could not be restored");
        m_plater.set_project_filename(wxString::FromUTF8(selected->source_path));
        m_project_name = selected->project_name;
        m_persistence.adopt_managed_state(materialized.semantic_state);
        opened->seed_from_live(m_plater.model());
        m_last_capture_input = observed_input();
        m_seen_model_input = m_last_capture_input;
        m_committed_semantic_revision = m_persistence.document().doc_revision();
        m_seen_semantic_revision = m_committed_semantic_revision;
        m_first_semantic_edit.reset();
        m_revision = 0;
        for (const auto& version : versions)
            m_revision = std::max(m_revision, version.revision);
        m_committed_head = head;
        m_store = std::move(opened);
        m_active_materialization = materialized;
        m_active_materialization_project = m_store->project_id();
        m_error.clear();
        m_state = State::Saved;
        m_next_probe = Clock::now() + kProbeInterval;
        return true;
    } catch (const std::exception& error) {
        fail(error.what());
        return false;
    }
}

std::vector<ProjectAutosave::ManagedProject> ProjectAutosave::projects()
{
    settle(true);
    // Home can be opened before the first project switch. Render the current
    // project once here; the preview is independent of the frequent backups.
    ensure_current_preview();
    std::vector<ManagedProject> result;
    for (const auto& entry : std::filesystem::directory_iterator(m_root)) {
        if (!entry.is_directory() || !std::filesystem::exists(entry.path() / "HEAD"))
            continue;
        try {
            if (!authored_head(entry.path()))
                continue;
            const std::string id = entry.path().filename().string();
            const auto add = [&result, &entry, &id](const ProjectVersionStore::Version& version) {
                const auto source = std::filesystem::u8path(version.source_path);
                std::string name = !version.project_name.empty() ? version.project_name : source.stem().u8string();
                if (name.empty() && !version.objects.empty()) {
                    // Versions written before projectName was added still have
                    // the original object name in their Orca resource entry.
                    const auto& object = version.objects.front();
                    name = std::filesystem::u8path(object.entry).filename().u8string();
                    const std::string suffix = "_" + std::to_string(object.backup_id) + ".model";
                    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                        name.erase(name.size() - suffix.size());
                    name = std::filesystem::u8path(name).stem().u8string();
                }
                ManagedProject project{id, name, version.created_at, version.source_path, entry.path().u8string()};
                try {
                    project.thumbnail_url = saved_preview(entry.path(), version.id);
                    if (project.thumbnail_url.empty()) {
                        // Older local projects have no preview sidecar. Orca's
                        // original 3MF thumbnail keeps their Home cards useful
                        // until the project is next opened and rendered locally.
                        project.thumbnail_url = source_preview(version.source_path);
                        if (!project.thumbnail_url.empty())
                            write_preview(entry.path(), version.id, project.thumbnail_url);
                    }
                } catch (const std::exception& error) {
                    // A damaged preview must not hide an otherwise healthy
                    // local project from Home.
                    BOOST_LOG_TRIVIAL(warning) << "Local project preview unavailable: " << error.what();
                }
                result.push_back(std::move(project));
            };
            if (m_store && id == m_store->project_id()) {
                const auto versions = m_store->history();
                if (!versions.empty())
                    add(versions.back());
            } else {
                ProjectVersionStore other(m_root, id);
                const auto versions = other.history();
                if (!versions.empty())
                    add(versions.back());
            }
        } catch (const std::exception& error) {
            BOOST_LOG_TRIVIAL(warning) << "Managed project unavailable: " << entry.path().string() << ": " << error.what();
        }
    }
    std::sort(result.begin(), result.end(), [](const ManagedProject& a, const ManagedProject& b) {
        return a.updated_at > b.updated_at;
    });
    return result;
}

bool ProjectAutosave::has_local_project_for_source(const std::filesystem::path& source) const
{
    return !projects_for_source(source).empty();
}

std::vector<std::string> ProjectAutosave::projects_for_source(const std::filesystem::path& source) const
{
    if (source.empty())
        return {};
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> matches;
    if (!std::filesystem::is_directory(m_root))
        return {};
    for (const auto& entry : std::filesystem::directory_iterator(m_root)) {
        if (!entry.is_directory() || !std::filesystem::is_regular_file(entry.path() / "HEAD"))
            continue;
        try {
            std::ifstream head_file(entry.path() / "HEAD", std::ios::binary);
            const std::string head((std::istreambuf_iterator<char>(head_file)), std::istreambuf_iterator<char>());
            if (head.empty()) continue;
            std::vector<std::string> versions;
            if (head.front() == '{')
                versions = nlohmann::json::parse(head).at("versions").get<std::vector<std::string>>();
            else {
                std::string version = head;
                if (!version.empty() && version.back() == '\n') version.pop_back();
                versions.push_back(std::move(version));
            }
            for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
                if (it->empty() || it->find_first_of("/\\") != std::string::npos) continue;
                std::ifstream manifest_file(entry.path() / "versions" / *it / "manifest.json", std::ios::binary);
                if (!manifest_file) continue;
                const nlohmann::json manifest = nlohmann::json::parse(manifest_file);
                if (same_source_path(source, std::filesystem::u8path(manifest.value("sourcePath", std::string())))) {
                    auto updated = std::filesystem::last_write_time(entry.path() / "HEAD");
                    if (std::filesystem::is_regular_file(entry.path() / "current-state.json"))
                        updated = std::max(updated, std::filesystem::last_write_time(entry.path() / "current-state.json"));
                    matches.emplace_back(updated, entry.path().filename().string());
                    break;
                }
            }
        } catch (const std::exception& error) {
            BOOST_LOG_TRIVIAL(warning) << "Local project source index unavailable: " << entry.path().string()
                                       << ": " << error.what();
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second > b.second;
    });
    std::vector<std::string> result;
    result.reserve(matches.size());
    for (const auto& [updated, id] : matches)
        result.push_back(id);
    const wxString current_source = m_plater.get_project_filename(".3mf");
    if (same_source_path(source, std::filesystem::u8path(current_source.ToUTF8().data()))) {
        const std::string& current_id = m_persistence.document().project_id();
        const auto current = std::find(result.begin(), result.end(), current_id);
        if (current != result.end())
            std::rotate(result.begin(), current, current + 1);
        else if (!current_id.empty())
            result.insert(result.begin(), current_id);
    }
    return result;
}

bool ProjectAutosave::open_managed_project(const std::string& project_id)
{
    if (!save_now())
        return false;
    ensure_current_preview();
    if (m_store->project_id() == project_id)
        return true;
    const std::string previous_id = m_store->head_id();
    std::optional<ProjectVersionStore::Materialization> target_materialized;
    std::unique_ptr<ProjectVersionStore> target;
    bool replacement_started = false;
    const auto load = [this](const ProjectVersionStore::Materialization& materialized,
                             const ProjectVersionStore::Version& version) {
        m_plater.reset();
        m_plater.take_snapshot("Open local project", UndoRedo::SnapshotType::ProjectSeparator);
        auto strategy = LoadStrategy::Restore;
        if (materialized.empty)
            strategy = strategy | LoadStrategy::AllowEmpty;
        const auto loaded = m_plater.load_files(
            std::vector<boost::filesystem::path>{boost_path(materialized.metadata),
                                                 boost_path(materialized.absent_original)},
            strategy, false);
        if ((loaded.empty() && !materialized.empty) || (materialized.empty && !m_plater.model().objects.empty()))
            throw std::runtime_error("local project could not be restored");
        m_plater.set_project_filename(wxString::FromUTF8(version.source_path));
    };
    try {
        target = std::make_unique<ProjectVersionStore>(m_root, project_id);
        const auto versions = target->history();
        if (versions.empty() || versions.back().id != target->head_id())
            throw std::runtime_error("local project has no current version");
        target_materialized = target->materialize(target->head_id());
        replacement_started = true;
        load(*target_materialized, versions.back());
        m_persistence.adopt_managed_state(target_materialized->semantic_state);
        target->seed_from_live(m_plater.model());
        m_last_capture_input = observed_input();
        m_seen_model_input = m_last_capture_input;
        m_committed_semantic_revision = m_persistence.document().doc_revision();
        m_seen_semantic_revision = m_committed_semantic_revision;
        m_first_semantic_edit.reset();
        if (m_active_materialization && m_active_materialization_project == m_store->project_id()) {
            try {
                m_store->remove_materialization(*m_active_materialization);
            } catch (const std::exception& cleanup_error) {
                m_warning = std::string("Old project workspace could not be removed: ") + cleanup_error.what();
                BOOST_LOG_TRIVIAL(warning) << m_warning;
            }
        }
        m_store = std::move(target);
        m_project_name = versions.back().project_name;
        m_active_materialization = *target_materialized;
        m_active_materialization_project = project_id;
        m_committed_head = m_store->head_id();
        m_revision = 0;
        for (const auto& version : versions)
            m_revision = std::max(m_revision, version.revision);
        m_error.clear();
        m_state = State::Saved;
        m_resume_attempted = true;
        m_next_probe = Clock::now() + kProbeInterval;
        return true;
    } catch (const std::exception& error) {
        const std::string reason = error.what();
        if (replacement_started) {
            try {
                const auto previous = m_store->materialize(previous_id);
                const auto versions = m_store->history();
                const auto old = std::find_if(versions.begin(), versions.end(), [&previous_id](const auto& version) {
                    return version.id == previous_id;
                });
                if (old == versions.end())
                    throw std::runtime_error("previous project version was not found");
                load(previous, *old);
                m_persistence.adopt_managed_state(previous.semantic_state);
                m_store->seed_from_live(m_plater.model());
                m_active_materialization = previous;
                m_active_materialization_project = m_store->project_id();
                if (target && target_materialized)
                    target->remove_materialization(*target_materialized);
            } catch (const std::exception& rollback_error) {
                fail(reason + "; previous project also failed to reload: " + rollback_error.what());
                return false;
            }
        }
        fail(reason);
        return false;
    }
}

void ProjectAutosave::ensure_current_preview()
{
    if (!m_store || m_store->head_id().empty() || m_plater.model().objects.empty())
        return;
    try {
        const auto project = m_root / m_store->project_id();
        if (!saved_preview(project, m_store->head_id()).empty())
            return;
        GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
        if (canvas == nullptr || !canvas->is_initialized() || m_plater.get_partplate_list().get_plate_count() == 0)
            return;
        ThumbnailData data;
        // Frame all model volumes, including objects on other plates.
        ThumbnailsParams params{{}, false, true, false, true, 0, false};
        canvas->render_thumbnail(data, 256, 256, params, Camera::EType::Ortho);
        if (!data.is_valid())
            return;
        const bool has_subject = [&data] {
            for (std::size_t pixel = 3; pixel < data.pixels.size(); pixel += 4)
                if (data.pixels[pixel] != 0)
                    return true;
            return false;
        }();
        if (!has_subject)
            return;
        const auto png = GCodeThumbnails::compress_thumbnail(data, GCodeThumbnailsFormat::PNG);
        if (!png || !png->data || png->size == 0 || png->size > kMaxPreviewBytes)
            return;
        write_preview(project, m_store->head_id(),
                      "data:image/png;base64," + base64_encode(std::string(static_cast<const char*>(png->data), png->size)));
    } catch (const std::exception& error) {
        // A preview is derived display data. A failed render or cache write
        // must not prevent opening the committed project.
        BOOST_LOG_TRIVIAL(warning) << "Local project preview unavailable: " << error.what();
    }
}

bool ProjectAutosave::save_now()
{
    try {
        adopt_project();
        settle(true); // A failed older write can be retried from the live model.
        m_store->ensure_durable_head();
        observe_document_change();
        if (m_plater.model().objects.empty() && m_store->head_id().empty() &&
            m_persistence.document().doc_revision() <= 1) {
            m_error.clear();
            m_state = State::Saved;
            return true;
        }
        if (!m_store->head_id().empty() && !m_last_capture_input.empty() &&
            observed_input() == m_last_capture_input) {
            if (m_persistence.document().doc_revision() != m_committed_semantic_revision)
                return persist_document(true);
            m_error.clear();
            m_state = State::Saved;
            return true;
        }
        GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
        if (canvas == nullptr || !canvas->is_initialized()) {
            fail("The project canvas is not ready for a durable save");
            return false;
        }
        if (canvas->get_gizmos_manager().is_in_editing_mode(true)) {
            fail("Finish the current tool edit before replacing the project");
            return false;
        }
        return capture(true);
    } catch (const std::exception& error) {
        fail(error.what());
        return false;
    }
}

std::string ProjectAutosave::current_version() const
{
    return m_committed_head;
}

std::vector<ProjectVersionStore::Version> ProjectAutosave::history()
{
    if (m_worker.valid())
        settle(true);
    return m_store ? m_store->history() : std::vector<ProjectVersionStore::Version>{};
}

std::string ProjectAutosave::pin_current()
{
    if (!save_now())
        return {};
    // A blank startup draft has no automatic version, but it is still the
    // exact before-state of an Agent operation that may add its first object.
    if (m_store->head_id().empty()) {
        GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
        if (canvas == nullptr || !canvas->is_initialized()) {
            fail("The project canvas is not ready for a durable save");
            return {};
        }
        if (!capture(true))
            return {};
    }
    const std::string id = m_store->head_id();
    m_store->pin(id);
    return id;
}

void ProjectAutosave::record_agent_operation(const std::string& action_id, const std::string& tool,
                                             const std::string& outcome, const std::string& before)
{
    const bool saved = save_now();
    m_store->record_operation(action_id, tool, outcome, before, saved ? m_committed_head : std::string());
    if (!saved)
        throw std::runtime_error(m_error);
}

bool ProjectAutosave::restore(const std::string& version_id)
{
    return restore_impl(version_id, nullptr, {});
}

bool ProjectAutosave::restore_chat(const std::string& version_id, const nlohmann::json& planning,
                                   const std::string& conversation_id)
{
    return restore_impl(version_id, &planning, conversation_id);
}

bool ProjectAutosave::restore_impl(const std::string& version_id, const nlohmann::json* planning,
                                   const std::string& conversation_id)
{
    std::string before_id;
    std::string before_state;
    std::optional<ProjectVersionStore::Materialization> materialized;
    bool replacement_started = false;
    bool marker_written = false;
    const auto validate_regions = [&] {
        if (!planning)
            return;
        std::vector<RegionRecord> regions;
        for (const auto& entry : planning->at("regions"))
            regions.push_back(region_record_from(entry));
        for (const RegionStatus& status : m_workspace.region_status(regions))
            if (status.binding_lost)
                throw std::runtime_error("a saved region no longer matches this model");
    };
    try {
        if (!save_now())
            return false;
        if (version_id == m_committed_head && planning == nullptr)
            return true;
        before_id = m_committed_head;
        before_state = m_persistence.document().dump();
        m_store->pin(before_id);
        m_store->pin(version_id);
        if (planning) {
            m_store->begin_chat_restore(before_id, version_id, conversation_id);
            marker_written = true;
        }
        if (version_id == before_id) {
            // Planning-only resume keeps the same committed model version.
            // The document transition still has to be durable before input
            // moves to the resumed chat.
            validate_regions();
            m_persistence.record_chat_restore(before_state, before_id, version_id,
                                              *planning, conversation_id);
            if (!persist_document(true))
                throw std::runtime_error(m_error);
            m_store->finish_chat_restore();
            m_persistence.notify_chat_restore_published();
            return true;
        }
        materialized = m_store->materialize(version_id);
        const auto selected = m_store->history();
        const auto version = std::find_if(selected.begin(), selected.end(), [&version_id](const auto& item) {
            return item.id == version_id;
        });
        if (version == selected.end())
            throw std::runtime_error("selected project version is unavailable");
        m_persistence.during_managed_restore([&] {
            replacement_started = true;
            m_plater.reset();
            m_plater.take_snapshot("Restore project version", UndoRedo::SnapshotType::ProjectSeparator);
            auto strategy = LoadStrategy::Restore;
            if (materialized->empty)
                strategy = strategy | LoadStrategy::AllowEmpty;
            const auto loaded = m_plater.load_files(
                std::vector<boost::filesystem::path>{boost_path(materialized->metadata),
                                                     boost_path(materialized->absent_original)},
                strategy, false);
            if (loaded.empty() && !materialized->empty)
                throw std::runtime_error("OrcaSlicer could not restore the selected model");
            if (materialized->empty && !m_plater.model().objects.empty())
                throw std::runtime_error("the empty project version restored geometry unexpectedly");
            m_plater.set_project_filename(wxString::FromUTF8(version->source_path));
            m_persistence.adopt_managed_state(before_state);
            m_last_edit = Clock::now();
            validate_regions();
            if (!capture(true))
                throw std::runtime_error(m_error);
            if (planning) {
                m_persistence.record_chat_restore(before_state, before_id, version_id,
                                                  *planning, conversation_id);
            } else
                m_persistence.record_managed_restore(before_state, before_id, version_id);
            if (!persist_document(true))
                throw std::runtime_error(m_error);
        });
        if (m_active_materialization && m_active_materialization_project == m_store->project_id()) {
            try {
                m_store->remove_materialization(*m_active_materialization);
            } catch (const std::exception& cleanup_error) {
                m_warning = std::string("Old restore workspace could not be removed: ") + cleanup_error.what();
                BOOST_LOG_TRIVIAL(warning) << m_warning;
            }
        }
        m_active_materialization = *materialized;
        m_active_materialization_project = m_store->project_id();
        if (planning) {
            m_store->finish_chat_restore();
            m_persistence.notify_chat_restore_published();
        }
        return true;
    } catch (const std::exception& error) {
        const std::string reason = error.what();
        if (marker_written && !replacement_started &&
            m_persistence.document().active_conversation_id() != conversation_id) {
            try {
                m_store->finish_chat_restore();
            } catch (const std::exception& cleanup_error) {
                fail(reason + "; restoration marker cleanup failed: " + cleanup_error.what());
                return false;
            }
        }
        if (replacement_started && m_store && m_store->head_id() == before_id) {
            try {
                const auto previous = m_store->materialize(before_id);
                const auto versions = m_store->history();
                const auto old = std::find_if(versions.begin(), versions.end(), [&before_id](const auto& item) {
                    return item.id == before_id;
                });
                if (old == versions.end())
                    throw std::runtime_error("previous project version is unavailable");
                m_plater.reset();
                m_plater.take_snapshot("Recover previous project version", UndoRedo::SnapshotType::ProjectSeparator);
                auto strategy = LoadStrategy::Restore;
                if (previous.empty)
                    strategy = strategy | LoadStrategy::AllowEmpty;
                const auto loaded = m_plater.load_files(
                    std::vector<boost::filesystem::path>{boost_path(previous.metadata), boost_path(previous.absent_original)},
                    strategy, false);
                if ((loaded.empty() && !previous.empty) || (previous.empty && !m_plater.model().objects.empty()))
                    throw std::runtime_error("previous project version could not be reloaded");
                m_plater.set_project_filename(wxString::FromUTF8(old->source_path));
                m_persistence.adopt_managed_state(before_state);
                m_store->seed_from_live(m_plater.model());
                m_active_materialization = previous;
                m_active_materialization_project = m_store->project_id();
                if (materialized)
                    m_store->remove_materialization(*materialized);
                if (marker_written)
                    m_store->finish_chat_restore();
            } catch (const std::exception& rollback_error) {
                fail(reason + "; previous project also failed to reload: " + rollback_error.what());
                return false;
            }
        }
        fail(reason);
        return false;
    }
}

bool ProjectAutosave::export_copy(const std::filesystem::path& destination)
{
    if (!save_now())
        return false;
    if (destination.empty() || destination.extension() != ".3mf")
        throw std::runtime_error("choose a .3mf export destination");
    const std::string id = ProjectVersionStore::new_id();
    const auto temporary = destination.parent_path() /
        std::filesystem::path(L".jusprin-pending-" + std::wstring(id.begin(), id.end()) + L".3mf");
    try {
        const auto exported = m_workspace.export_project_archive(temporary.u8string());
        if (!exported.succeeded())
            throw std::runtime_error(exported.message);
        ProjectVersionStore::publish_export_archive(temporary, destination);
        return true;
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

} // namespace Slic3r::GUI::JusPrin::Workspace
