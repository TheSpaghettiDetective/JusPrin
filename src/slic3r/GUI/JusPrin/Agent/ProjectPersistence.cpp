#include "ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"

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

std::string default_clock() { return Workspace::utc_now(); }

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

// Shared with each subscription's unsubscribe closure, which outlives neither
// the observers nor, through the weak pointer, the persistence that owns them.
struct ProjectPersistence::LedgerObservers
{
    std::uint64_t                                  next_id{1};
    std::map<std::uint64_t, std::function<void()>> observers;
};

Workspace::WorkspaceSubscription ProjectPersistence::subscribe_ledger(std::function<void()> listener)
{
    const std::uint64_t id = m_ledger_observers->next_id++;
    m_ledger_observers->observers.emplace(id, std::move(listener));
    std::weak_ptr<LedgerObservers> weak = m_ledger_observers;
    return Workspace::WorkspaceSubscription([weak, id]() {
        if (auto state = weak.lock())
            state->observers.erase(id);
    });
}

void ProjectPersistence::notify_ledger_changed() const
{
    // Look each observer up just before calling it, so one that unsubscribes
    // another, or itself, during dispatch is safe.
    std::vector<std::uint64_t> ids;
    for (const auto& observer : m_ledger_observers->observers)
        ids.push_back(observer.first);
    for (const std::uint64_t id : ids) {
        const auto found = m_ledger_observers->observers.find(id);
        if (found == m_ledger_observers->observers.end())
            continue;
        const std::function<void()> callback = found->second;
        callback();
    }
}

ProjectPersistence::ProjectPersistence(Workspace::IWorkspace& workspace, Config config)
    : m_workspace(workspace), m_config(std::move(config)), m_ledger_observers(std::make_shared<LedgerObservers>())
{
    if (!m_config.clock)
        m_config.clock = default_clock;
    if (!m_config.uuid)
        m_config.uuid = default_uuid;
    if (m_config.in_memory) {
        // No project to follow and nowhere to mirror to: this session is its
        // own boundary, and it needs an identity now because ids are minted
        // from it.
        m_config.recovery_root.clear();
        m_document.initialize_identity("p-" + m_config.uuid(), "l-" + m_config.uuid(), m_config.clock());
        return;
    }
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
    entry.key    = edit.key;
    const ChangeEntry stored = m_document.add_change(std::move(entry), m_config.clock());
    m_document.mark_plan_needs_reassessment(stored.label.empty() ? "The project changed" : stored.label);
    // A paint session is a burst of edits; the owner's pacing timer writes them.
    commit();
    if (m_change_added)
        m_change_added(stored);
}

void ProjectPersistence::attach()
{
    // An in-memory session adopts no project: it holds its own document and
    // must not follow the open project into its auxiliary directory.
    if (m_config.in_memory)
        return;
    m_boundary_pending = false;
    adopt_current_project(/*in_place_reset=*/false);
}

std::string ProjectPersistence::jusprin_data_dir() const
{
    if (!m_config.managed_root.empty() && m_document.has_identity())
        return (fs::path(m_config.managed_root) / m_document.project_id()).string();
    return (fs::path(m_workspace.auxiliary_data_dir()) / kJusPrinDirName).string();
}

std::string ProjectPersistence::state_file_path() const
{
    return (fs::path(jusprin_data_dir()) / kStateFileName).string();
}

std::string ProjectPersistence::recovery_dir() const
{
    if (!m_config.managed_root.empty())
        return {};
    if (m_config.recovery_root.empty() || !m_document.has_identity())
        return {};
    return (fs::path(m_config.recovery_root) / m_document.project_id()).string();
}

void ProjectPersistence::on_workspace_changed(const Workspace::WorkspaceChanged& change)
{
    if (m_managed_restore_active)
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
    }
}

bool ProjectPersistence::heal_if_directory_moved()
{
    // A changed auxiliary directory marks an Orca project boundary. Managed
    // state is then adopted from the local store or imported from that
    // directory's legacy auxiliary document.
    if (!m_attached || m_workspace.auxiliary_data_dir() == m_attached_aux_dir)
        return false;
    m_boundary_pending = false;
    adopt_current_project(/*in_place_reset=*/false);
    return true;
}

void ProjectPersistence::resolve_pending_boundary()
{
    if (m_managed_restore_active)
        return;
    if (heal_if_directory_moved())
        return;
    if (!m_boundary_pending)
        return;
    m_boundary_pending = false;
    adopt_current_project(/*in_place_reset=*/true);
}

void ProjectPersistence::during_managed_restore(const std::function<void()>& operation)
{
    if (m_config.managed_root.empty() || m_managed_restore_active)
        throw std::logic_error("managed restore requires an open managed project");
    m_managed_restore_active = true;
    struct RestoreGuard {
        bool& active;
        ~RestoreGuard() { active = false; }
    } guard{m_managed_restore_active};
    operation();
}

void ProjectPersistence::adopt_current_project(bool in_place_reset)
{
    m_attached_aux_dir = m_workspace.auxiliary_data_dir();
    m_attached         = true;
    m_draft.clear();

    if (in_place_reset) {
        // A full in-place reset is a new project boundary: earlier state does
        // not carry into it.
        if (m_config.managed_root.empty()) {
            std::error_code ec;
            fs::remove_all(jusprin_data_dir(), ec);
        }
        start_fresh_identity();
        return;
    }

    // An external archive can carry the old auxiliary document. Read it once
    // for migration; managed snapshots never write authoritative state there.
    const fs::path legacy_state = fs::path(m_attached_aux_dir) / kJusPrinDirName / kStateFileName;
    const std::string state_text = read_file(m_config.managed_root.empty() ? fs::path(state_file_path()) : legacy_state);
    if (state_text.empty()) {
        start_fresh_identity();
        return;
    }

    ProjectStateDocument loaded;
    const ProjectStateDocument::LoadResult result = loaded.load(state_text);
    if (result == ProjectStateDocument::LoadResult::Corrupt) {
        // Keep the unreadable file for inspection, then start over.
        std::error_code ec;
        fs::rename(legacy_state, legacy_state.string() + ".corrupt", ec);
        start_fresh_identity();
        return;
    }
    m_document = std::move(loaded);

    // The recovery mirror may hold state newer than the last explicit save
    // (messages, tool activity, a partial reply from before a crash).
    const std::string recovery = m_config.managed_root.empty() ? recovery_dir() :
        (m_config.recovery_root.empty() ? std::string() :
            (fs::path(m_config.recovery_root) / m_document.project_id()).string());
    if (!recovery.empty()) {
        const std::string mirror_text = read_file(fs::path(recovery) / kStateFileName);
        if (!mirror_text.empty()) {
            ProjectStateDocument mirror;
            if (mirror.load(mirror_text) != ProjectStateDocument::LoadResult::Corrupt &&
                mirror.project_id() == m_document.project_id() && mirror.doc_revision() > m_document.doc_revision())
                m_document = std::move(mirror);
        }
        if (m_config.managed_root.empty())
            load_recovery_meta();
    }

    if (!m_config.managed_root.empty()) {
        // A copied or reopened external archive is an import, even when it
        // carries an identity that already has a managed head on this host.
        if (fs::exists(fs::path(m_config.managed_root) / m_document.project_id() / "HEAD"))
            m_document.fork_identity("p-" + m_config.uuid(), m_config.clock());
        const fs::path old_attachments = fs::path(m_attached_aux_dir) / kJusPrinDirName / "attachments";
        const fs::path local_attachments = fs::path(jusprin_data_dir()) / "attachments";
        if (fs::is_directory(old_attachments)) {
            for (const auto& entry : fs::recursive_directory_iterator(old_attachments)) {
                const fs::path target = local_attachments / fs::relative(entry.path(), old_attachments);
                if (entry.is_directory())
                    fs::create_directories(target);
                else if (entry.is_regular_file()) {
                    fs::create_directories(target.parent_path());
                    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
                }
            }
        }
    }

    // A stream or run interrupted by the crash/restart cannot resume.
    m_document.normalize_interrupted_state();
    if (!m_config.managed_root.empty()) {
        m_draft = m_document.draft();
    }

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
    if (m_config.in_memory) {
        m_dirty = false;
        return;
    }
    // Never write a stale document across an unresolved project boundary.
    resolve_pending_boundary();
    if (!m_attached)
        return;
    if (!m_config.managed_root.empty()) {
        // ProjectAutosave publishes the document with a model version or as
        // a coalesced current-state write between model edits.
        m_dirty = false;
        return;
    }
    write_state_to(jusprin_data_dir());
    const std::string recovery = recovery_dir();
    if (!recovery.empty()) {
        write_state_to(recovery);
        write_recovery_meta();
    }
    m_dirty = false;
}

void ProjectPersistence::adopt_managed_state(const std::string& state)
{
    if (m_config.managed_root.empty())
        throw std::logic_error("managed state requires a local project store");
    ProjectStateDocument loaded;
    if (loaded.load(state) == ProjectStateDocument::LoadResult::Corrupt || !loaded.has_identity())
        throw std::runtime_error("managed project state is invalid");
    loaded.normalize_interrupted_state();
    m_document = std::move(loaded);
    m_draft = m_document.draft();
    m_attached_aux_dir = m_workspace.auxiliary_data_dir();
    m_attached = true;
    m_boundary_pending = false;
    m_dirty = false;
    notify_document_replaced();
}

void ProjectPersistence::flush_if_dirty()
{
    if (m_dirty)
        flush();
}

void ProjectPersistence::record_managed_restore(const std::string& current_state, const std::string& from_version,
                                                const std::string& selected_version)
{
    if (m_config.managed_root.empty())
        throw std::logic_error("model restore requires a managed project document");
    ProjectStateDocument current;
    if (current.load(current_state) == ProjectStateDocument::LoadResult::Corrupt || !current.has_identity())
        throw std::runtime_error("current project document is invalid during model restore");
    current.normalize_interrupted_state();
    ChangeEntry change;
    change.kind = "restore";
    change.actor = "person";
    change.label = "Restored an earlier project version";
    change.from = from_version;
    change.to = selected_version;
    current.add_change(std::move(change), m_config.clock());
    current.mark_plan_needs_reassessment("An earlier model version was restored");
    m_document = std::move(current);
    m_draft = m_document.draft();
    m_attached_aux_dir = m_workspace.auxiliary_data_dir();
    m_attached = true;
    m_boundary_pending = false;
    m_dirty = true;
    flush();
    notify_document_replaced();
}

void ProjectPersistence::record_chat_restore(const std::string& current_state,
                                              const std::string& from_version,
                                              const std::string& selected_version,
                                              const nlohmann::json& planning,
                                              const std::string& conversation_id)
{
    if (m_config.managed_root.empty())
        throw std::logic_error("chat restore requires a managed project document");
    ProjectStateDocument current;
    if (current.load(current_state) == ProjectStateDocument::LoadResult::Corrupt || !current.has_identity())
        throw std::runtime_error("chat checkpoint is unavailable during restore");
    const auto checkpoint = current.chat_checkpoint(conversation_id);
    if (!checkpoint || checkpoint->value("versionId", "") != selected_version ||
        (*checkpoint)["planning"] != planning)
        throw std::runtime_error("chat checkpoint changed during restore");
    current.normalize_interrupted_state();
    current.restore_planning_snapshot(planning);
    if (!current.set_active_conversation(conversation_id) || !current.set_viewed_conversation(conversation_id))
        throw std::runtime_error("chat disappeared during restore");
    ChangeEntry change;
    change.kind = "restore";
    change.actor = "person";
    change.label = "Restored this chat's saved project setup";
    change.from = from_version;
    change.to = selected_version;
    current.add_change(std::move(change), m_config.clock());
    m_document = std::move(current);
    m_draft = m_document.draft();
    m_attached_aux_dir = m_workspace.auxiliary_data_dir();
    m_attached = true;
    m_boundary_pending = false;
    m_dirty = true;
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
    if (!m_config.managed_root.empty()) {
        m_document.set_draft(text);
        commit();
    }
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
    if (m_config.in_memory) {
        m_blobs[relative_path] = bytes;
        return true;
    }
    if (!m_attached)
        return false;
    return write_file(fs::path(jusprin_data_dir()) / fs::path(relative_path), bytes);
}

std::string ProjectPersistence::read_attachment_blob(const std::string& relative_path) const
{
    if (relative_path.empty())
        return {};
    if (m_config.in_memory) {
        const auto blob = m_blobs.find(relative_path);
        return blob == m_blobs.end() ? std::string{} : blob->second;
    }
    if (!m_attached)
        return {};
    return read_file(fs::path(jusprin_data_dir()) / fs::path(relative_path));
}

void ProjectPersistence::remove_attachment_dir(const std::string& relative_dir)
{
    if (relative_dir.empty())
        return;
    if (m_config.in_memory) {
        for (auto blob = m_blobs.begin(); blob != m_blobs.end();)
            blob = blob->first.rfind(relative_dir, 0) == 0 ? m_blobs.erase(blob) : std::next(blob);
        return;
    }
    if (!m_attached)
        return;
    // Published semantic versions can still reference an attachment after a
    // staged message is removed from the live document. Reachability cleanup
    // belongs to the version store, not this mutable conversation surface.
    if (!m_config.managed_root.empty())
        return;
    std::error_code ec;
    fs::remove_all(fs::path(jusprin_data_dir()) / fs::path(relative_dir), ec);
}

} // namespace Slic3r::GUI::JusPrin::Agent
