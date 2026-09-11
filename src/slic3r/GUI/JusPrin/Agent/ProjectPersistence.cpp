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
constexpr const char* kRecoveryMeta    = "recovery.json";

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

const char* edit_kind_name(Workspace::EditKind kind)
{
    switch (kind) {
    case Workspace::EditKind::Step: return "step";
    case Workspace::EditKind::Undo: return "undo";
    case Workspace::EditKind::Redo: return "redo";
    case Workspace::EditKind::Setting: return "setting";
    case Workspace::EditKind::Preset: return "preset";
    }
    throw std::logic_error("Unknown workspace edit kind");
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
    m_edit_subscription = m_workspace.subscribe_edits([this](const Workspace::WorkspaceEdit& edit) { on_edit(edit); });
}

void ProjectPersistence::on_edit(const Workspace::WorkspaceEdit& edit)
{
    // The edit belongs to whatever project is really open now.
    resolve_pending_boundary();
    ChangeEntry entry;
    entry.kind   = edit_kind_name(edit.kind);
    entry.actor  = edit.actor == Workspace::EditActor::Agent ? "agent" : "person";
    entry.label  = edit.label;
    entry.from   = edit.before;
    entry.to     = edit.after;
    entry.preset = edit.preset;
    const ChangeEntry stored = m_document.add_change(std::move(entry), m_config.clock());
    // A paint session is a burst of edits; the owner's pacing timer writes them.
    commit();
    if (m_change_added)
        m_change_added(stored);
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
    }
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
    notify_document_replaced();
}

void ProjectPersistence::notify_document_replaced()
{
    if (m_document_replaced)
        m_document_replaced();
    // A replaced document brings a different manufacturing ledger with it.
    notify_ledger_changed();
}

void ProjectPersistence::start_fresh_identity()
{
    m_document = ProjectStateDocument();
    m_document.initialize_identity("p-" + m_config.uuid(), "l-" + m_config.uuid(), m_config.clock());
    m_dirty = true;
    flush();
    notify_document_replaced();
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
