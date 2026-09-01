#include "ProjectPersistence.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Workspace::WorkspaceChangeReasons;

constexpr const char* kJusPrinDirName  = "JusPrin";
constexpr const char* kStateFileName   = "state.json";
constexpr const char* kRevisionsDir    = "revisions";
constexpr const char* kRecoveryMeta    = "recovery.json";
// Checkpoints are ordinary project archives; the exporter requires the .3mf
// suffix, and the .snapshot marker keeps them recognizable inside the
// project's auxiliary data.
constexpr const char* kSnapshotSuffix  = ".snapshot.3mf";

std::string default_clock()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string default_uuid()
{
    static std::mt19937_64 engine(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> distribution;
    std::ostringstream out;
    out << std::hex << distribution(engine) << '-' << distribution(engine);
    return out.str();
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
    // Write-then-rename keeps a crash from leaving a truncated state file.
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

// Reasons that mean the manufactured result may have changed. Selection and
// pure Project boundary changes do not create revisions.
constexpr WorkspaceChangeReasons kManufacturingReasons = WorkspaceChangeReasons::Contents |
                                                         WorkspaceChangeReasons::Transform |
                                                         WorkspaceChangeReasons::Plates;

std::string cause_of(WorkspaceChangeReasons reasons)
{
    std::string cause;
    const auto add = [&cause](const char* name) {
        if (!cause.empty())
            cause += "+";
        cause += name;
    };
    if (has_reason(reasons, WorkspaceChangeReasons::Contents))
        add("contents");
    if (has_reason(reasons, WorkspaceChangeReasons::Transform))
        add("transform");
    if (has_reason(reasons, WorkspaceChangeReasons::Plates))
        add("plates");
    if (has_reason(reasons, WorkspaceChangeReasons::History))
        add("history");
    return cause.empty() ? "change" : cause;
}

} // namespace

ProjectPersistence::ProjectPersistence(Workspace::IWorkspace& workspace, Config config)
    : m_workspace(workspace), m_config(std::move(config))
{
    if (!m_config.clock)
        m_config.clock = default_clock;
    if (!m_config.uuid)
        m_config.uuid = default_uuid;
    m_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        on_workspace_changed(change);
    });
}

void ProjectPersistence::attach()
{
    m_boundary_pending = false;
    adopt_current_project(/*in_place_reset=*/false);
}

std::string ProjectPersistence::jusprin_data_dir() const
{
    return (fs::path(m_workspace.auxiliary_data_dir()) / kJusPrinDirName).string();
}

std::string ProjectPersistence::state_file_path() const
{
    return (fs::path(jusprin_data_dir()) / kStateFileName).string();
}

std::string ProjectPersistence::recovery_dir() const
{
    if (m_config.recovery_root.empty() || !m_document.has_identity())
        return {};
    return (fs::path(m_config.recovery_root) / m_document.project_id()).string();
}

void ProjectPersistence::on_workspace_changed(const Workspace::WorkspaceChanged& change)
{
    if (m_in_revert)
        return;
    if (has_reason(change.reasons, WorkspaceChangeReasons::Project)) {
        if (!m_attached || m_workspace.auxiliary_data_dir() != m_attached_aux_dir) {
            // The new project's directory is already in place: adopt it.
            adopt_current_project(/*in_place_reset=*/false);
        } else {
            // Ambiguous: either an in-place full reset, or a replacement
            // whose event was published before the directory moved. Park the
            // decision until the directory settles.
            m_boundary_pending = true;
        }
        return;
    }
    if (heal_if_directory_moved())
        return;
    if (m_boundary_pending) {
        // A later event with the directory still in place means the project
        // operation completed without moving it: an in-place reset.
        resolve_pending_boundary();
        return;
    }
    if (!m_attached || !m_document.has_identity())
        return;
    if ((change.reasons & kManufacturingReasons) != WorkspaceChangeReasons::None) {
        // The pre-change state is gone; a missing initial checkpoint can no
        // longer be backfilled.
        m_initial_capture_pending = false;
        capture_revision(cause_of(change.reasons));
        return;
    }
    if (m_initial_capture_pending)
        retry_initial_capture();
}

bool ProjectPersistence::heal_if_directory_moved()
{
    // The authoritative auxiliary directory is the single source of truth
    // for project identity on disk. Whenever it no longer matches the
    // attached one — however the boundary events were ordered or raced —
    // adopt what is actually there.
    if (!m_attached || m_workspace.auxiliary_data_dir() == m_attached_aux_dir)
        return false;
    m_boundary_pending = false;
    adopt_current_project(/*in_place_reset=*/false);
    return true;
}

void ProjectPersistence::resolve_pending_boundary()
{
    if (m_in_revert)
        return;
    if (heal_if_directory_moved())
        return;
    if (!m_boundary_pending)
        return;
    m_boundary_pending = false;
    adopt_current_project(/*in_place_reset=*/true);
}

void ProjectPersistence::adopt_current_project(bool in_place_reset)
{
    m_attached_aux_dir = m_workspace.auxiliary_data_dir();
    m_attached         = true;
    m_draft.clear();

    if (in_place_reset) {
        // A full in-place reset is a new project boundary: earlier state does
        // not carry into it.
        std::error_code ec;
        fs::remove_all(jusprin_data_dir(), ec);
        start_fresh_identity();
        return;
    }

    const std::string state_text = read_file(state_file_path());
    if (state_text.empty()) {
        start_fresh_identity();
        return;
    }

    ProjectStateDocument loaded;
    const ProjectStateDocument::LoadResult result = loaded.load(state_text);
    if (result == ProjectStateDocument::LoadResult::Corrupt) {
        // Keep the unreadable file for inspection, then start over.
        std::error_code ec;
        fs::rename(state_file_path(), state_file_path() + ".corrupt", ec);
        start_fresh_identity();
        return;
    }
    m_document = std::move(loaded);

    // The recovery mirror may hold state newer than the last explicit save
    // (messages, approvals, a partial reply from before a crash).
    const std::string recovery = recovery_dir();
    if (!recovery.empty()) {
        const std::string mirror_text = read_file(fs::path(recovery) / kStateFileName);
        if (!mirror_text.empty()) {
            ProjectStateDocument mirror;
            if (mirror.load(mirror_text) != ProjectStateDocument::LoadResult::Corrupt &&
                mirror.project_id() == m_document.project_id() && mirror.doc_revision() > m_document.doc_revision())
                m_document = std::move(mirror);
        }
        load_recovery_meta();
    }

    // A stream or run interrupted by the crash/restart cannot resume.
    m_document.normalize_interrupted_state();

    m_dirty = true;
    flush();
    if (m_document_replaced)
        m_document_replaced();
}

void ProjectPersistence::start_fresh_identity()
{
    m_document = ProjectStateDocument();
    m_document.initialize_identity("p-" + m_config.uuid(), "l-" + m_config.uuid(), m_config.clock());
    capture_revision("initial");
    // Adoption can run before the canvas is able to render (early startup);
    // the project content stays equal to the initial state until the first
    // manufacturing change, so the capture can be retried until then.
    m_initial_capture_pending = m_document.revisions().back().snapshot_file.empty();
    m_dirty = true;
    flush();
    if (m_document_replaced)
        m_document_replaced();
}

void ProjectPersistence::retry_initial_capture()
{
    const std::vector<RevisionInfo> revisions = m_document.revisions();
    if (revisions.empty() || !revisions.front().snapshot_file.empty() ||
        m_document.current_revision_id() != revisions.front().id) {
        m_initial_capture_pending = false;
        return;
    }
    const RevisionInfo& initial  = revisions.front();
    const std::string   relative = std::string(kRevisionsDir) + "/" + initial.id + kSnapshotSuffix;
    const fs::path      target   = fs::path(jusprin_data_dir()) / kRevisionsDir / (initial.id + kSnapshotSuffix);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (!m_workspace.export_project_archive(target.string()).succeeded())
        return; // still not ready; keep retrying on later events
    m_initial_capture_pending = false;
    ++m_stats.captures;
    m_stats.total_snapshot_bytes += fs::file_size(target, ec);
    m_document.set_revision_snapshot(initial.id, relative);
    m_dirty = true;
    flush();
}

void ProjectPersistence::capture_revision(const std::string& cause)
{
    const std::string revision_id = m_document.peek_next_revision_id();
    const std::string relative    = std::string(kRevisionsDir) + "/" + revision_id + kSnapshotSuffix;
    const fs::path    target      = fs::path(jusprin_data_dir()) / kRevisionsDir / (revision_id + kSnapshotSuffix);

    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);

    const auto started = std::chrono::steady_clock::now();
    const Workspace::CommandResult exported = m_workspace.export_project_archive(target.string());
    m_stats.last_capture_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    std::string snapshot_file;
    if (exported.succeeded()) {
        snapshot_file = relative;
        ++m_stats.captures;
        m_stats.total_snapshot_bytes += fs::file_size(target, ec);
    } else {
        // Record the revision honestly without a checkpoint; it cannot be
        // reverted to, but the timeline stays truthful.
        ++m_stats.capture_failures;
    }

    const RevisionInfo revision = [&] {
        const std::string id = m_document.add_revision(cause, snapshot_file, m_document.active_conversation_id(),
                                                       m_config.clock());
        return *m_document.find_revision(id);
    }();
    m_dirty = true;
    flush();
    if (m_revision_added)
        m_revision_added(revision);
}

void ProjectPersistence::flush()
{
    // Never write a stale document across an unresolved project boundary.
    resolve_pending_boundary();
    if (!m_attached)
        return;
    write_state_to(jusprin_data_dir());
    const std::string recovery = recovery_dir();
    if (!recovery.empty()) {
        write_state_to(recovery);
        write_recovery_meta();
    }
    m_dirty = false;
}

void ProjectPersistence::flush_if_dirty()
{
    if (m_dirty)
        flush();
}

void ProjectPersistence::write_state_to(const std::string& directory) const
{
    write_file(fs::path(directory) / kStateFileName, m_document.dump());
}

void ProjectPersistence::set_draft(const std::string& text)
{
    if (m_draft == text)
        return;
    m_draft = text;
    write_recovery_meta();
}

void ProjectPersistence::write_recovery_meta() const
{
    const std::string recovery = recovery_dir();
    if (recovery.empty())
        return;
    write_file(fs::path(recovery) / kRecoveryMeta,
               json{{"draft", m_draft}, {"updatedAt", m_config.clock()}}.dump(2));
}

void ProjectPersistence::load_recovery_meta()
{
    const std::string recovery = recovery_dir();
    if (recovery.empty())
        return;
    const json meta = json::parse(read_file(fs::path(recovery) / kRecoveryMeta), nullptr, false);
    if (meta.is_object())
        m_draft = meta.value("draft", "");
}

ProjectPersistence::RevertResult ProjectPersistence::revert_to_revision(const std::string& revision_id)
{
    const std::optional<RevisionInfo> target = m_document.find_revision(revision_id);
    if (!target)
        return {false, "The revision does not exist."};
    if (target->snapshot_file.empty())
        return {false, "This revision has no checkpoint and cannot be restored."};
    if (m_document.current_revision_id() == revision_id)
        return {false, "The project is already at this revision."};

    // Everything needed after the restore is copied out first: restoring
    // replaces the project, and the old auxiliary directory (which holds the
    // checkpoints) does not survive the replacement.
    const fs::path old_dir = fs::path(jusprin_data_dir());
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path(ec) / ("jusprin-revert-" + m_config.uuid());
    fs::create_directories(scratch, ec);

    const fs::path target_snapshot = old_dir / fs::path(target->snapshot_file);
    if (!fs::is_regular_file(target_snapshot, ec))
        return {false, "The revision checkpoint file is missing."};

    std::vector<std::string> kept_files;
    for (const RevisionInfo& revision : m_document.revisions()) {
        if (revision.seq > target->seq || revision.snapshot_file.empty())
            continue;
        const fs::path source = old_dir / fs::path(revision.snapshot_file);
        if (!fs::is_regular_file(source, ec))
            continue;
        const fs::path copied = scratch / fs::path(revision.snapshot_file).filename();
        fs::copy_file(source, copied, fs::copy_options::overwrite_existing, ec);
        if (!ec)
            kept_files.push_back(revision.snapshot_file);
    }

    // Attachment blobs live under the same soon-to-be-replaced directory. Copy
    // every current blob dir aside; after truncation only the survivors are
    // copied back, which drops orphaned ones without a separate delete.
    for (const AttachmentRecord& attachment : m_document.attachments()) {
        const fs::path source = old_dir / fs::path(attachment.relative_dir());
        if (!fs::is_directory(source, ec))
            continue;
        const fs::path staged = scratch / fs::path(attachment.relative_dir());
        fs::create_directories(staged.parent_path(), ec);
        fs::copy(source, staged, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    }

    m_in_revert = true;
    const auto started = std::chrono::steady_clock::now();
    const Workspace::CommandResult restored =
        m_workspace.restore_project_archive((scratch / target_snapshot.filename()).string());
    m_stats.last_restore_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    if (!restored.succeeded()) {
        m_in_revert = false;
        fs::remove_all(scratch, ec);
        return {false, "Restoring the checkpoint failed: " + restored.message};
    }

    // The native project is now at the target revision; truncate the
    // document only after that success so a failed restore changes nothing.
    const std::optional<ProjectStateDocument::TruncateResult> truncated =
        m_document.revert_to_revision(revision_id);
    m_document.normalize_interrupted_state();

    m_attached_aux_dir = m_workspace.auxiliary_data_dir();
    const fs::path new_dir = fs::path(jusprin_data_dir());
    fs::create_directories(new_dir / kRevisionsDir, ec);
    for (const std::string& file : kept_files)
        fs::copy_file(scratch / fs::path(file).filename(), new_dir / fs::path(file),
                      fs::copy_options::overwrite_existing, ec);
    // Restore only the attachment blobs the truncation kept; orphaned ones are
    // simply never copied back into the fresh directory.
    if (truncated)
        for (const std::string& dir : truncated->kept_attachment_dirs) {
            const fs::path staged = scratch / fs::path(dir);
            if (!fs::is_directory(staged, ec))
                continue;
            const fs::path destination = new_dir / fs::path(dir);
            fs::create_directories(destination.parent_path(), ec);
            fs::copy(staged, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
    fs::remove_all(scratch, ec);

    // A Revert restores an earlier durable project state. Composer text is
    // recovery-only working state, so carrying it across would resurrect work
    // the user explicitly discarded.
    m_draft.clear();
    m_dirty = true;
    flush();
    m_in_revert = false;
    if (m_document_replaced)
        m_document_replaced();
    return {true, {}};
}

Workspace::CommandResult ProjectPersistence::export_clean_copy(const std::string& file_path)
{
    return m_workspace.export_project_archive(file_path);
}

std::string ProjectPersistence::attachments_dir() const
{
    return (fs::path(jusprin_data_dir()) / "attachments").string();
}

bool ProjectPersistence::write_attachment_blob(const std::string& relative_path, const std::string& bytes)
{
    if (!m_attached)
        return false;
    return write_file(fs::path(jusprin_data_dir()) / fs::path(relative_path), bytes);
}

std::string ProjectPersistence::read_attachment_blob(const std::string& relative_path) const
{
    if (!m_attached || relative_path.empty())
        return {};
    return read_file(fs::path(jusprin_data_dir()) / fs::path(relative_path));
}

void ProjectPersistence::remove_attachment_dir(const std::string& relative_dir)
{
    if (!m_attached || relative_dir.empty())
        return;
    std::error_code ec;
    fs::remove_all(fs::path(jusprin_data_dir()) / fs::path(relative_dir), ec);
}

} // namespace Slic3r::GUI::JusPrin::Agent
