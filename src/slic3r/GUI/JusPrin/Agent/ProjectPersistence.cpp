#include "ProjectPersistence.hpp"
#include "ToolExecution.hpp"
#include "slic3r/GUI/JusPrin/Support/Files.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

// Last: it defines the L marker.
#include "ActivityTitle.hpp"
#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

namespace fs = std::filesystem;
using Workspace::WorkspaceChangeReasons;

constexpr const char* kJusPrinDirName  = "JusPrin";
constexpr const char* kStateFileName   = "state.json";

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
        throw std::runtime_error("could not open " + path.u8string() + ": " + std::strerror(errno));
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
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

// What put the plan in doubt: what changed and who changed it ("Filament
// changed to Generic ABS by hand"), in English for the model and in the
// person's language for the setup card. A preset's library suffix
// (" @System", " @BBL A1M") is not the person's word and is dropped. The
// two texts are built as an action's title is (ActivityTitle.hpp).
ActivityTitle plan_doubt_reason(const Workspace::WorkspaceEdit& edit)
{
    const bool        by_hand = edit.actor == Workspace::EditActor::Person;
    // A client names itself; "another app" is the app's own word for one that did not.
    const ActivityTitle who = edit.client.empty() ? title_of(L("another app")) : same_title(edit.client);
    const auto short_preset = [](const std::string& name) { return name.substr(0, name.find(" @")); };
    // An undo step's or a setting's name, in OrcaSlicer's English and as its catalog shows it.
    // An empty text is never looked up: the catalog's empty entry is its header.
    const ActivityTitle label{edit.label, edit.label.empty() ? edit.label : translated(edit.label.c_str())};
    // The first sentence when the person made the change, the second when an app did.
    const auto either = [&](const char* person, const char* app, const auto&... args) {
        return by_hand ? title_of(person, args...) : title_of(app, args..., who);
    };
    switch (edit.kind) {
    case Workspace::EditKind::Preset: {
        const std::string preset = short_preset(edit.label);
        if (edit.location == "printer")
            // TRN %1% is a printer preset's name; %2%, in the second form, the app that changed it.
            return either(L("Printer changed to %1% by hand"), L("Printer changed to %1% by %2%"), preset);
        if (edit.location == "filament")
            // TRN %1% is a filament preset's name; %2%, in the second form, the app that changed it.
            return either(L("Filament changed to %1% by hand"), L("Filament changed to %1% by %2%"), preset);
        // TRN %1% is a process preset's name; %2%, in the second form, the app that changed it.
        return either(L("Print settings changed to %1% by hand"), L("Print settings changed to %1% by %2%"), preset);
    }
    case Workspace::EditKind::Setting:
        if (edit.after.empty())
            // TRN %1% is a setting's name; %2%, in the second form, the app that changed it.
            return either(L("%1% changed by hand"), L("%1% changed by %2%"), label);
        {
            const ActivityTitle value{edit.after, edit.after_shown.empty() ? edit.after : edit.after_shown};
            // TRN %1% is a setting's name, %2% its new value; %3%, in the second form, the app that changed it.
            return either(L("%1% changed to %2% by hand"), L("%1% changed to %2% by %3%"), label, value);
        }
    case Workspace::EditKind::Undo:
        if (edit.label.empty())
            // TRN %1%, in the second form, is the app that undid the last change.
            return either(L("Undone by hand"), L("Undone by %1%"));
        // TRN %1% is the change that was undone; %2%, in the second form, the app that undid it.
        return either(L("%1% undone by hand"), L("%1% undone by %2%"), label);
    case Workspace::EditKind::Redo:
        if (edit.label.empty())
            // TRN %1%, in the second form, is the app that redid the last change.
            return either(L("Redone by hand"), L("Redone by %1%"));
        // TRN %1% is the change that was redone; %2%, in the second form, the app that redid it.
        return either(L("%1% redone by hand"), L("%1% redone by %2%"), label);
    case Workspace::EditKind::Step: break;
    }
    if (edit.label.empty())
        // TRN %1%, in the second form, is the app that changed the project.
        return either(L("The project changed by hand"), L("The project changed by %1%"));
    // TRN %1% is what changed, such as Move; %2%, in the second form, the app that changed it.
    return either(L("%1%, by hand"), L("%1%, by %2%"), label);
}

// Which reason the card keeps while the plan is in doubt: a setup switch
// over the setting edits and steps that follow it, the printer above all.
int plan_doubt_weight(const Workspace::WorkspaceEdit& edit)
{
    if (edit.kind != Workspace::EditKind::Preset)
        return 0;
    return edit.location == "printer" ? 3 : edit.location == "filament" ? 2 : 1;
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
        // No project to follow: this session is its own boundary, and it
        // needs an identity now because ids are minted from it.
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
    entry.actor    = edit.actor == Workspace::EditActor::Person ? "person" : "agent";
    entry.external = edit.actor == Workspace::EditActor::ExternalTool;
    entry.client   = edit.client;
    entry.label  = edit.label;
    // The change log is read by people only: its values are the shown ones.
    entry.from   = edit.before_shown.empty() ? edit.before : edit.before_shown;
    entry.to     = edit.after_shown.empty() ? edit.after : edit.after_shown;
    entry.preset   = edit.preset;
    entry.key      = edit.key;
    entry.location = edit.location;
    const ChangeEntry stored = m_document.add_change(std::move(entry), m_config.clock());
    // The in-app agent's own edits carry out its plan; they do not put it in
    // doubt. A person's edit, Undo and Redo, and an external MCP client's
    // edit do.
    if (edit.actor != Workspace::EditActor::Agent) {
        const ActivityTitle reason = plan_doubt_reason(edit);
        m_document.mark_plan_needs_reassessment(reason.english, plan_doubt_weight(edit), reason.shown);
    }
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
    // A calibration generator's own new project: the same project goes on.
    // The generator can move the project folder more than once (JusPrin's
    // calibration project, then OrcaSlicer's own), so every boundary while
    // it runs continues the project; continuing_project() disarms it.
    if (m_continuing_project) {
        m_dirty = true;
        flush();
        return;
    }
    m_draft.clear();

    if (in_place_reset) {
        // A full in-place reset is a new project boundary: earlier state does
        // not carry into it.
        if (m_config.managed_root.empty())
            fs::remove_all(jusprin_data_dir());
        start_fresh_identity();
        return;
    }

    // An external archive can carry the old auxiliary document. Read it once
    // for migration; managed snapshots never write authoritative state there.
    const fs::path legacy_state = fs::path(m_attached_aux_dir) / kJusPrinDirName / kStateFileName;
    const fs::path state_path = m_config.managed_root.empty() ? fs::path(state_file_path()) : legacy_state;
    const std::string state_text = fs::exists(state_path) ? read_file(state_path) : std::string();
    if (state_text.empty()) {
        start_fresh_identity();
        return;
    }

    ProjectStateDocument loaded;
    const ProjectStateDocument::LoadResult result = loaded.load(state_text);
    if (result != ProjectStateDocument::LoadResult::Loaded) {
        // Keep the file it cannot read, broken or from a newer JusPrin, for
        // inspection, then start over: the model opens with a new chat history.
        // Our own file that does not read is unexpected, and reported.
        const bool newer = result == ProjectStateDocument::LoadResult::Newer;
        fs::rename(legacy_state, legacy_state.string() + (newer ? ".newer" : ".corrupt"));
        start_fresh_identity();
        if (!newer)
            throw std::runtime_error("the chat state saved in the opened project could not be read");
        return;
    }
    m_document = std::move(loaded);

    if (!m_config.managed_root.empty()) {
        // A copied or reopened external archive is an import, even when it
        // carries an identity that already has a managed head on this host.
        if (fs::exists(fs::path(m_config.managed_root) / m_document.project_id() / "HEAD"))
            m_document.fork_identity("p-" + m_config.uuid(), m_config.clock());
        // The attachments, and the pictures the chats' records sent.
        for (const char* folder : {"attachments", kChatRecordsDir}) {
            const fs::path old_folder = fs::path(m_attached_aux_dir) / kJusPrinDirName / folder;
            const fs::path local_folder = fs::path(jusprin_data_dir()) / folder;
            if (!fs::is_directory(old_folder))
                continue;
            for (const auto& entry : fs::recursive_directory_iterator(old_folder)) {
                const fs::path target = local_folder / fs::relative(entry.path(), old_folder);
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

void ProjectPersistence::continuing_project(const std::function<void()>& operation)
{
    if (m_continuing_project)
        throw std::logic_error("a project continuation is already armed");
    m_continuing_project = true;
    struct Disarm {
        bool& armed;
        ~Disarm() { armed = false; }
    } disarm{m_continuing_project};
    operation();
    resolve_pending_boundary();
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
    m_dirty = false;
}

void ProjectPersistence::adopt_managed_state(const std::string& state)
{
    if (m_config.managed_root.empty())
        throw std::logic_error("managed state requires a local project store");
    ProjectStateDocument loaded;
    const ProjectStateDocument::LoadResult result = loaded.load(state);
    if (result == ProjectStateDocument::LoadResult::Newer)
        throw Workspace::NewerFormatError("the project was saved by a newer version of the app");
    if (result != ProjectStateDocument::LoadResult::Loaded || !loaded.has_identity())
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
    if (current.load(current_state) != ProjectStateDocument::LoadResult::Loaded || !current.has_identity())
        throw std::runtime_error("current project document is invalid during model restore");
    current.normalize_interrupted_state();
    ChangeEntry change;
    change.kind = "restore";
    change.actor = "person";
    // Stored in English; the change log translates it where it is shown (AgentHost).
    change.label = L("Restored an earlier project version");
    change.from = from_version;
    change.to = selected_version;
    current.add_change(std::move(change), m_config.clock());
    // A restore brings back another version's setup whole: no edit outweighs it.
    current.mark_plan_needs_reassessment("An earlier model version was restored", 4,
                                         translated(L("An earlier model version was restored")));
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
                                              const std::string& conversation_id)
{
    if (m_config.managed_root.empty())
        throw std::logic_error("chat restore requires a managed project document");
    ProjectStateDocument current;
    if (current.load(current_state) != ProjectStateDocument::LoadResult::Loaded || !current.has_identity())
        throw std::runtime_error("chat checkpoint is unavailable during restore");
    const auto checkpoint = current.chat_checkpoint(conversation_id);
    if (!checkpoint || checkpoint->value("versionId", "") != selected_version)
        throw std::runtime_error("chat checkpoint changed during restore");
    current.normalize_interrupted_state();
    // The chat's intent and plan never left it; making it active makes them
    // the project's again, with the validity they had when it was left.
    if (!current.set_active_conversation(conversation_id) || !current.set_viewed_conversation(conversation_id))
        throw std::runtime_error("chat disappeared during restore");
    // Resuming on the model already in place restores nothing.
    if (from_version != selected_version) {
        ChangeEntry change;
        change.kind = "restore";
        change.actor = "person";
        change.label = L("Restored this chat's saved project setup");
        change.from = from_version;
        change.to = selected_version;
        current.add_change(std::move(change), m_config.clock());
    }
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
}

Workspace::CommandResult ProjectPersistence::export_clean_copy(const std::string& file_path)
{
    return m_workspace.export_project_archive(file_path);
}

std::string ProjectPersistence::attachments_dir() const
{
    return (fs::path(jusprin_data_dir()) / "attachments").string();
}

void ProjectPersistence::write_attachment_blob(const std::string& relative_path, const std::string& bytes)
{
    if (m_config.in_memory) {
        m_blobs[relative_path] = bytes;
        return;
    }
    if (!m_attached)
        throw std::logic_error("an attachment written before a project is attached");
    write_file(fs::path(jusprin_data_dir()) / fs::path(relative_path), bytes);
}

std::string ProjectPersistence::read_attachment_blob(const std::string& relative_path) const
{
    if (relative_path.empty())
        return {};
    if (m_config.in_memory)
        return m_blobs.at(relative_path);
    if (!m_attached)
        throw std::logic_error("an attachment read before a project is attached");
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
    // A chat's record folder is not versioned and goes with the chat.
    if (!m_config.managed_root.empty() && relative_dir.rfind(kChatRecordsDir, 0) != 0)
        return;
    fs::remove_all(fs::path(jusprin_data_dir()) / fs::path(relative_dir));
}

} // namespace Slic3r::GUI::JusPrin::Agent
