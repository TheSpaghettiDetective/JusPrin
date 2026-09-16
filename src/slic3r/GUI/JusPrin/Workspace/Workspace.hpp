#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

class ProjectSessionId
{
public:
    constexpr ProjectSessionId() = default;
    explicit constexpr ProjectSessionId(std::uint64_t value) : m_value(value) {}

    explicit constexpr operator bool() const { return m_value != 0; }
    constexpr std::uint64_t value() const { return m_value; }

    friend constexpr bool operator==(ProjectSessionId lhs, ProjectSessionId rhs) { return lhs.m_value == rhs.m_value; }
    friend constexpr bool operator!=(ProjectSessionId lhs, ProjectSessionId rhs) { return !(lhs == rhs); }
    friend constexpr bool operator<(ProjectSessionId lhs, ProjectSessionId rhs) { return lhs.m_value < rhs.m_value; }

private:
    std::uint64_t m_value{0};
};

// IDs are stable only inside one project session. Keeping the session in the
// value lets a command distinguish an unknown current-session ID from an ID
// invalidated by project replacement, even if Orca later reuses its raw ID.
template<class Tag> class ProjectScopedId
{
public:
    constexpr ProjectScopedId() = default;
    constexpr ProjectScopedId(ProjectSessionId session, std::uint64_t value) : m_session(session), m_value(value) {}

    explicit constexpr operator bool() const { return static_cast<bool>(m_session) && m_value != 0; }
    constexpr ProjectSessionId session() const { return m_session; }
    constexpr std::uint64_t value() const { return m_value; }

    friend constexpr bool operator==(ProjectScopedId lhs, ProjectScopedId rhs)
    {
        return lhs.m_session == rhs.m_session && lhs.m_value == rhs.m_value;
    }
    friend constexpr bool operator!=(ProjectScopedId lhs, ProjectScopedId rhs) { return !(lhs == rhs); }
    friend constexpr bool operator<(ProjectScopedId lhs, ProjectScopedId rhs)
    {
        return lhs.m_session < rhs.m_session || (lhs.m_session == rhs.m_session && lhs.m_value < rhs.m_value);
    }

private:
    ProjectSessionId m_session;
    std::uint64_t    m_value{0};
};

struct PlateIdTag;
struct ObjectIdTag;
using PlateId  = ProjectScopedId<PlateIdTag>;
using ObjectId = ProjectScopedId<ObjectIdTag>;

struct ObjectTransform
{
    std::array<double, 3> position{};
    std::array<double, 3> rotation{};
    std::array<double, 3> scale{{1.0, 1.0, 1.0}};

    friend bool operator==(const ObjectTransform& lhs, const ObjectTransform& rhs)
    {
        return lhs.position == rhs.position && lhs.rotation == rhs.rotation && lhs.scale == rhs.scale;
    }
};

struct WorkspaceObject
{
    ObjectId                    id;
    std::string                 name;
    std::vector<ObjectTransform> instances;
};

// Time and material for one plate's current slice. OrcaSlicer has no
// background slicing, so before the first Slice there is no honest number:
// the estimate is absent rather than zero, and consumers omit the row.
struct SliceEstimate
{
    std::uint32_t print_time_seconds{0};
    double        material_grams{0.0};
    // Material cost is filament price times grams, and the price lives in the
    // filament profile as a field most people never fill in. When it is unset
    // Orca reports zero, which is not a price -- has_cost says so, and
    // consumers drop the clause rather than printing a zero.
    double        material_cost{0.0};
    bool          has_cost{false};
};

// What the estimate on a plate is worth right now. The number itself is kept
// across all three: a slice in flight or invalidated does not make the last
// honest figure worthless, and blanking it kills the comparison the reader is
// usually making. Only "never sliced" has no estimate at all.
enum class EstimateStatus : std::uint8_t {
    Current,     // this plate holds the slice the estimate came from
    Recomputing, // a slice is running; the estimate is the one it will replace
    Stale        // something invalidated the slice; the estimate predates it
};

struct WorkspacePlate
{
    PlateId                      id;
    std::string                  name;
    bool                         active{false};
    // True when this plate holds a currently valid slice result. Derived from
    // the authoritative plate state on every snapshot, never cached.
    bool                         sliced{false};
    std::vector<WorkspaceObject> objects;
    // The plate's estimate: current, being recomputed, or stale. Absent only
    // when this plate has never been sliced in this session.
    std::optional<SliceEstimate> estimate;
    EstimateStatus               estimate_status{EstimateStatus::Current};
    // Which of the user's actions invalidated the slice, when that is known.
    // Empty when it is not: the card would rather say nothing than guess.
    std::string                  invalidated_by;
};

// One process setting whose value in force differs from the preset it came
// from. The list is how far the project has drifted from stock: its size is
// the count the setup card rests on, and its contents are what the card shows
// when it is opened.
struct PresetDelta
{
    std::string key;    // config key, stable across languages
    std::string label;  // the setting's own UI label, already localized
    std::string preset; // value the preset carries
    std::string value;  // value in force
};

enum class SelectionStatus : std::uint8_t { None, Objects, Unsupported };

// Compact project and machine setup facts for consumers (the Agent context)
// that must describe the workspace without reaching into Orca types. Values
// are read fresh from their authoritative owners at snapshot time.
struct WorkspaceSetup
{
    std::string project_name;
    bool        project_dirty{false};
    // Where the project was last opened from or saved to, UTF-8; empty for a
    // project that has never been saved.
    std::string project_path;
    std::string printer_preset;
    std::string filament_preset;
    std::string process_preset;
    bool        process_preset_dirty{false};

    friend bool operator==(const WorkspaceSetup& lhs, const WorkspaceSetup& rhs)
    {
        return lhs.project_name == rhs.project_name && lhs.project_dirty == rhs.project_dirty &&
               lhs.project_path == rhs.project_path && lhs.printer_preset == rhs.printer_preset && lhs.filament_preset == rhs.filament_preset &&
               lhs.process_preset == rhs.process_preset && lhs.process_preset_dirty == rhs.process_preset_dirty;
    }
};

// What the slicer is doing right now. OrcaSlicer runs one background slicing
// process for the whole application, not one per plate, so "running" is a
// property of the workspace and the plate it is working on is a separate
// question. During a slice-all the plate moves as the run advances.
struct WorkspaceSlicing
{
    bool                   running{false};
    std::optional<PlateId> plate;
    // 0-100, or absent when the owner reports no figure.
    std::optional<int>     percent;
};

struct WorkspaceSnapshot
{
    ProjectSessionId            session;
    std::uint64_t               revision{0};
    WorkspaceSetup              setup;
    std::vector<WorkspacePlate> plates;
    std::optional<PlateId>      active_plate;
    SelectionStatus             selection_status{SelectionStatus::None};
    std::vector<ObjectId>       selected_objects;
    bool                        can_undo{false};
    bool                        can_redo{false};
    // Empty when the process preset is untouched, which is also the state a
    // non-FFF printer reports.
    std::vector<PresetDelta>    preset_deltas;
    // ISO 4217 code from the machine's regional settings, or empty when the OS
    // does not say. A cost is a bare number without it, so consumers drop the
    // money rather than denominate it in a guess.
    std::string                 currency;
    WorkspaceSlicing            slicing;
};

// The three preset families a print is chosen from. SLA has no place here
// until the product has one.
enum class PresetKind : std::uint8_t { Printer, Filament, Process };

// One preset as Orca currently sees it. `compatible` is Orca's own cached
// verdict, never re-derived: deriving it writes to the preset's config and
// can change the selection.
struct PresetEntry
{
    std::string name;   // canonical, what a selection takes
    std::string label;  // the alias Orca shows, when it has one
    std::string vendor; // the profile bundle it shipped in; empty for a user preset
    bool        system{false};
    bool        selected{false};
    bool        compatible{true};
};

struct PresetQuery
{
    PresetKind  kind{PresetKind::Process};
    std::string text;
    bool        compatible_only{true};
    std::size_t limit{25};
    std::string cursor;
};

struct PresetListResult
{
    std::vector<PresetEntry> items;
    std::string              next_cursor;
    std::size_t              total{0};
    bool                     truncated{false};
};

// A physical printer this application knows about, as it last reported itself.
// Fields it has not reported are absent rather than guessed: a nozzle of zero
// would be a claim, and this is a record of what was heard.
struct PrinterDevice
{
    std::string id;
    std::string name;
    std::string model;
    std::string connection; // lan, cloud, or empty when it has not said
    // offline, idle, or printing -- what this application can observe, not a
    // claim about the machine.
    std::string activity{"offline"};
    std::optional<double>    nozzle_diameter;
    std::vector<std::string> materials;
    // Milliseconds since the epoch, absent when the device has never reported.
    std::optional<std::int64_t> observed_at_ms;
};

// One filament's share of a sliced plate. Lengths and weights are derived the
// way Orca's own preview derives them -- volume per filament times that
// filament's diameter and density -- so a report can never disagree with the
// number on screen.
struct SliceFilamentUse
{
    std::size_t extruder{0};
    double      length_mm{0.0};
    double      grams{0.0};
    double      cost{0.0};
    bool        has_cost{false};
    // Volumes that are not part of the model: purge between filaments and the
    // prime tower. Absent in Orca means zero here.
    double      flushed_mm3{0.0};
    double      tower_mm3{0.0};
    double      support_mm3{0.0};
};

// Something Orca says about this slice. `code` is a stable sentinel where Orca
// has one and empty where the warning is only prose; `object` names the object
// it belongs to and is empty for a warning about the whole plate.
struct SliceFinding
{
    std::string code;
    std::string message;
    bool        critical{false};
    std::string object;
};

// What a sliced plate can say about itself. Absent sections are absent, not
// zero: a report for a plate that has never been sliced says so and stops.
struct SliceReport
{
    bool          valid{false};
    std::uint32_t print_time_seconds{0};
    std::uint32_t prepare_time_seconds{0};
    double        total_grams{0.0};
    double        total_cost{0.0};
    bool          has_cost{false};
    std::vector<SliceFilamentUse> filaments;
    std::uint32_t filament_changes{0};
    std::uint32_t extruder_changes{0};
    std::vector<SliceFinding> findings;
    // Orca's own words for two paths that cross, when it found any.
    std::string   conflict;
    // A toolpath outside the printable area. Recomputed from the build volume
    // rather than read from the result, whose flag is only ever set by a 3mf.
    bool          toolpath_outside{false};
};

enum class WorkspaceError : std::uint8_t {
    None,
    InvalidId,
    MissingObject,
    StaleId,
    UnsupportedSelection,
    UnavailableOperation,
    InvalidArgument,
    NoChange,
    InvalidSettings,
    StaleSettings
};

struct CommandResult
{
    WorkspaceError         error{WorkspaceError::None};
    std::string            message;
    std::optional<ObjectId> object_id;

    bool succeeded() const { return error == WorkspaceError::None; }

    static CommandResult success(std::optional<ObjectId> id = std::nullopt)
    {
        return {WorkspaceError::None, {}, id};
    }

    static CommandResult failure(WorkspaceError error, std::string message)
    {
        return {error, std::move(message), std::nullopt};
    }
};

enum class WorkspaceChangeReasons : std::uint32_t {
    None      = 0,
    Selection = 1u << 0,
    Contents  = 1u << 1,
    History   = 1u << 2,
    Transform = 1u << 3,
    Plates    = 1u << 4,
    Project   = 1u << 5,
    Settings  = 1u << 6
};

constexpr WorkspaceChangeReasons operator|(WorkspaceChangeReasons lhs, WorkspaceChangeReasons rhs)
{
    return static_cast<WorkspaceChangeReasons>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr WorkspaceChangeReasons operator&(WorkspaceChangeReasons lhs, WorkspaceChangeReasons rhs)
{
    return static_cast<WorkspaceChangeReasons>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

inline WorkspaceChangeReasons& operator|=(WorkspaceChangeReasons& lhs, WorkspaceChangeReasons rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_reason(WorkspaceChangeReasons reasons, WorkspaceChangeReasons reason)
{
    return (reasons & reason) != WorkspaceChangeReasons::None;
}

struct WorkspaceChanged
{
    ProjectSessionId       session;
    std::uint64_t          revision{0};
    WorkspaceChangeReasons reasons{WorkspaceChangeReasons::None};
};

using WorkspaceChangedCallback = std::function<void(const WorkspaceChanged&)>;

// Who made an edit: the person, unless the Agent executor holds an
// IWorkspace::AgentEdit scope while its command runs.
enum class EditActor : std::uint8_t { Person, Agent };

// What kind of edit the change log records.
//   Step     a real undo step: OrcaSlicer's snapshot_modifies_project rule;
//            label is OrcaSlicer's own step name, which may be empty
//   Undo     the undo history moved back; label is the step undone
//   Redo     the undo history moved forward; label is the step redone
//   Setting  one setting's value in force changed; label is its display name
//   Preset   a whole preset was switched; label is the new preset's name
enum class EditKind : std::uint8_t { Step, Undo, Redo, Setting, Preset };

struct WorkspaceEdit
{
    EditKind    kind{EditKind::Step};
    EditActor   actor{EditActor::Person};
    std::string label;
    // Setting only: the value before and after, and the preset it was edited in.
    std::string before, after, preset;
};

using WorkspaceEditCallback = std::function<void(const WorkspaceEdit&)>;

class WorkspaceSubscription
{
public:
    WorkspaceSubscription()                                        = default;
    WorkspaceSubscription(const WorkspaceSubscription&)            = delete;
    WorkspaceSubscription& operator=(const WorkspaceSubscription&) = delete;

    WorkspaceSubscription(WorkspaceSubscription&& other) noexcept : m_unsubscribe(std::move(other.m_unsubscribe)) {}
    WorkspaceSubscription& operator=(WorkspaceSubscription&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_unsubscribe = std::move(other.m_unsubscribe);
        }
        return *this;
    }

    ~WorkspaceSubscription() { reset(); }

    void reset()
    {
        if (m_unsubscribe) {
            auto unsubscribe = std::move(m_unsubscribe);
            unsubscribe();
        }
    }

    explicit operator bool() const { return static_cast<bool>(m_unsubscribe); }

private:
    explicit WorkspaceSubscription(std::function<void()> unsubscribe) : m_unsubscribe(std::move(unsubscribe)) {}
    std::function<void()> m_unsubscribe;

    friend class WorkspaceChangeHub;
    friend class WorkspaceEditHub;
};

// A delivery is safe to queue: it owns no workspace or observer and becomes a
// no-op when its hub is destroyed. Revision advances at commit(), before a
// consumer can observe the committed native state, rather than at delivery.
class WorkspaceChangeDelivery
{
public:
    WorkspaceChangeDelivery() = default;

    void deliver()
    {
        if (m_deliver) {
            auto deliver = std::move(m_deliver);
            deliver();
        }
    }

    explicit operator bool() const { return static_cast<bool>(m_deliver); }

private:
    explicit WorkspaceChangeDelivery(std::function<void()> deliver) : m_deliver(std::move(deliver)) {}
    std::function<void()> m_deliver;

    friend class WorkspaceChangeHub;
};

// This hub is GUI-independent. A concrete workspace mutates authoritative
// state, merges all reasons belonging to that one logical operation, calls
// commit(), and delivers before committing a later mutation. Production uses
// synchronous GUI-thread delivery so a snapshot read by the callback has the
// event's revision. A delivery may be deferred only when the owner guarantees
// that no later mutation can commit first (the teardown test exercises this).
class WorkspaceChangeHub
{
public:
    WorkspaceChangeHub() : m_state(std::make_shared<State>()) {}
    WorkspaceChangeHub(const WorkspaceChangeHub&) = delete;
    WorkspaceChangeHub& operator=(const WorkspaceChangeHub&) = delete;
    ~WorkspaceChangeHub()
    {
        m_state->alive = false;
        m_state->observers.clear();
    }

    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback)
    {
        const std::uint64_t id = m_state->next_observer_id++;
        m_state->observers.emplace(id, std::move(callback));
        std::weak_ptr<State> weak_state = m_state;
        return WorkspaceSubscription([weak_state, id]() {
            if (auto state = weak_state.lock())
                state->observers.erase(id);
        });
    }

    void merge(WorkspaceChangeReasons reasons) { m_state->pending |= reasons; }
    bool has_pending() const { return m_state->pending != WorkspaceChangeReasons::None; }
    std::uint64_t revision() const { return m_state->revision; }

    WorkspaceChangeDelivery commit(ProjectSessionId session)
    {
        if (!has_pending())
            return {};

        WorkspaceChanged change{session, ++m_state->revision, m_state->pending};
        m_state->pending = WorkspaceChangeReasons::None;
        std::weak_ptr<State> weak_state = m_state;
        return WorkspaceChangeDelivery([weak_state, change]() {
            auto state = weak_state.lock();
            if (!state)
                return;

            std::vector<std::uint64_t> observer_ids;
            observer_ids.reserve(state->observers.size());
            for (const auto& observer : state->observers)
                observer_ids.emplace_back(observer.first);

            // Look up each observer immediately before invoking it. This makes
            // removing this or another subscription during dispatch safe.
            for (const std::uint64_t id : observer_ids) {
                if (!state->alive)
                    break;
                const auto observer = state->observers.find(id);
                if (observer == state->observers.end())
                    continue;
                WorkspaceChangedCallback callback = observer->second;
                callback(change);
            }
        });
    }

    std::optional<WorkspaceChanged> publish(ProjectSessionId session, WorkspaceChangeReasons reasons)
    {
        merge(reasons);
        const std::uint64_t next_revision = m_state->revision + 1;
        WorkspaceChangeDelivery delivery = commit(session);
        if (!delivery)
            return std::nullopt;
        WorkspaceChanged change{session, next_revision, reasons};
        delivery.deliver();
        return change;
    }

private:
    struct State
    {
        std::uint64_t                                  revision{0};
        std::uint64_t                                  next_observer_id{1};
        WorkspaceChangeReasons                         pending{WorkspaceChangeReasons::None};
        bool                                            alive{true};
        std::map<std::uint64_t, WorkspaceChangedCallback> observers;
    };

    std::shared_ptr<State> m_state;
};

// Delivers edits synchronously, in the order the workspace detects them. Edits
// are a feed of their own, separate from WorkspaceChanged: they do not
// advance the revision, so recording them can never make a pending Agent
// proposal stale.
class WorkspaceEditHub
{
public:
    WorkspaceEditHub() : m_state(std::make_shared<State>()) {}
    WorkspaceEditHub(const WorkspaceEditHub&) = delete;
    WorkspaceEditHub& operator=(const WorkspaceEditHub&) = delete;
    ~WorkspaceEditHub() { m_state->observers.clear(); }

    WorkspaceSubscription subscribe(WorkspaceEditCallback callback)
    {
        const std::uint64_t id = m_state->next_observer_id++;
        m_state->observers.emplace(id, std::move(callback));
        std::weak_ptr<State> weak_state = m_state;
        return WorkspaceSubscription([weak_state, id]() {
            if (auto state = weak_state.lock())
                state->observers.erase(id);
        });
    }

    void publish(const WorkspaceEdit& edit)
    {
        std::shared_ptr<State> state = m_state;
        std::vector<std::uint64_t> observer_ids;
        for (const auto& observer : state->observers)
            observer_ids.emplace_back(observer.first);
        for (const std::uint64_t id : observer_ids) {
            const auto observer = state->observers.find(id);
            if (observer == state->observers.end())
                continue;
            WorkspaceEditCallback callback = observer->second;
            callback(edit);
        }
    }

private:
    struct State
    {
        std::uint64_t                                  next_observer_id{1};
        std::map<std::uint64_t, WorkspaceEditCallback> observers;
    };

    std::shared_ptr<State> m_state;
};

struct SettingDefinition
{
    std::string key, type, label, category, description, unit;
    std::optional<double> min, max;
    std::vector<std::string> enum_values, enum_labels;
    bool writable{false};
};

struct SettingValue
{
    std::string key, value;
    bool differs_from_preset{false}, differs_from_system{false};
    SettingDefinition definition;
};

struct SettingIssue
{
    std::string key, code, message;
    std::vector<std::string> allowed, suggestions;
    std::optional<double> min, max;
};

struct SettingsQuery { std::string text; std::size_t limit{10}; std::string cursor; };
struct SettingsSearchResult
{
    std::vector<SettingDefinition> items;
    std::string next_cursor;
    bool truncated{false};
    std::optional<SettingIssue> error;
};
struct SettingsReadResult
{
    std::vector<SettingValue> items;
    std::vector<std::string> unknown_keys;
    std::vector<SettingIssue> issues;
    std::optional<SettingIssue> error;
};
struct SettingsPatch { std::map<std::string, std::string> changes; };
struct SettingChange { std::string key, before, after; };
struct SettingsPreview
{
    bool valid{false};
    std::vector<SettingChange> changes;
    std::vector<SettingIssue> issues, warnings;
    std::string process_preset;
    // Predicted secondary changes are approved and read back alongside the
    // explicit patch. They are never accepted as extra writable input keys.
    std::vector<SettingChange> dependencies;
};

class IWorkspace
{
public:
    virtual ~IWorkspace() = default;

    // The production implementation must be called and observed synchronously
    // on the GUI thread. It never mutates Orca from a background thread. Each
    // successful logical change advances revision before its single callback;
    // snapshot() in that callback reports the same revision. The fake is plain
    // C++ and has no GUI-thread restriction.
    virtual WorkspaceSnapshot snapshot() const                                  = 0;
    virtual CommandResult select_object(ObjectId id)                            = 0;
    virtual CommandResult rename_object(ObjectId id, const std::string& name)   = 0;
    virtual CommandResult duplicate_object(ObjectId id)                         = 0;
    virtual CommandResult remove_object(ObjectId id)                            = 0;
    virtual CommandResult undo()                                                = 0;
    virtual CommandResult redo()                                                = 0;

    // Starts Orca's own slicing run: one plate, or every plate when no plate
    // is named. It returns once the run has started, not when it finishes --
    // the result arrives as a Plates change and is read from the snapshot, the
    // way the GUI's own Slice button behaves.
    //
    // Refuses with UnavailableOperation when a slice is already running unless
    // preempt is set, because nothing in Orca records who started a run: a
    // slice in flight may be the person's, and taking it over is a decision
    // the caller must make deliberately rather than by racing.
    virtual CommandResult start_slice(std::optional<PlateId> plate, bool preempt) = 0;

    // What one plate's current slice says about itself. Returns a report whose
    // `valid` is false when the plate holds no current slice, rather than
    // failing: "not sliced yet" is an answer, not an error.
    virtual SliceReport slice_report(PlateId plate) const = 0;

    // A page of presets of one kind, as Orca has them. Read-only in the strict
    // sense: it reports Orca's cached compatibility verdict and never asks for
    // a fresh one, because asking writes to preset configs and can move the
    // selection.
    virtual PresetListResult list_presets(const PresetQuery& query) const = 0;

    // Every physical printer the application knows, reachable or not. A
    // printer it has not heard from is a real answer to "what printers do I
    // have", which is not the same question setup asks.
    virtual std::vector<PrinterDevice> printers() const = 0;
    virtual SettingsSearchResult search_settings(const SettingsQuery& query) const = 0;
    virtual SettingsReadResult read_settings(const std::vector<std::string>& keys) const = 0;
    virtual SettingsPreview preview_settings(const SettingsPatch& patch) const = 0;
    virtual CommandResult apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed,
                                         SettingsPreview& applied) = 0;

    // Directory for consumer-owned files that belong to the open project and
    // travel inside its saved archive. The path changes when the
    // authoritative project is replaced, so consumers must re-resolve it
    // after every Project change rather than caching it.
    virtual std::string auxiliary_data_dir() const = 0;

    // Writes the current authoritative project (model, plates, settings) to a
    // portable project archive at file_path, excluding auxiliary data — so a
    // clean copy carries no consumer files along.
    virtual CommandResult export_project_archive(const std::string& file_path) = 0;

    // Saves the open project to file_path (UTF-8, absolute, ".3mf"), the way
    // the person's own Save does: the file becomes the project's file, the
    // project is marked saved, and auxiliary data travels with it. Unlike
    // export_project_archive this is the project, not a copy of it.
    virtual CommandResult save_project(const std::string& file_path) = 0;

    // Imports a model or project file's geometry into the CURRENT project,
    // adding objects rather than replacing the project. It is a single
    // undoable manufacturing change: the session is unchanged, prior IDs stay
    // valid, revision advances, and a Contents change is published. On success
    // object_id is the first added object (when one can be identified).
    virtual CommandResult import_model(const std::string& file_path) = 0;

    virtual WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) = 0;
    // The change log's feed: every edit, delivered synchronously as the
    // workspace detects it and stamped with the actor in force.
    WorkspaceSubscription subscribe_edits(WorkspaceEditCallback callback) { return m_edits.subscribe(std::move(callback)); }

    // Held by the Agent executor around a command, so the edits the command
    // causes are attributed to the Agent. Everything else is the person's.
    class AgentEdit
    {
    public:
        explicit AgentEdit(IWorkspace& workspace) : m_workspace(workspace) { ++m_workspace.m_agent_edits; }
        ~AgentEdit() { --m_workspace.m_agent_edits; }
        AgentEdit(const AgentEdit&) = delete;
        AgentEdit& operator=(const AgentEdit&) = delete;

    private:
        IWorkspace& m_workspace;
    };

protected:
    void publish_edit(WorkspaceEdit edit)
    {
        edit.actor = m_agent_edits > 0 ? EditActor::Agent : EditActor::Person;
        m_edits.publish(edit);
    }

private:
    WorkspaceEditHub              m_edits;
    int                           m_agent_edits{0};
};

} // namespace Slic3r::GUI::JusPrin::Workspace
