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
    // Any selected preset carries edits that are not saved.
    bool        presets_dirty{false};

    friend bool operator==(const WorkspaceSetup& lhs, const WorkspaceSetup& rhs)
    {
        return lhs.project_name == rhs.project_name && lhs.project_dirty == rhs.project_dirty &&
               lhs.presets_dirty == rhs.presets_dirty &&
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

// An OrcaSlicer UI job (orient, arrange) the tool system started. The
// handle is the caller's; the state is what Orca's worker reported.
struct WorkspaceJob
{
    std::string           handle;
    std::string           kind;  // orient or arrange
    std::string           state; // running, finished, cancelled, or failed
    std::vector<ObjectId> not_placed; // arrange: objects no plate holds afterwards
};

inline constexpr std::size_t kJobLimit = 16;

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
    std::vector<WorkspaceJob>   jobs; // oldest first
};

// Geometry facts about one object, as object_analyze reports them. Lengths
// are millimetres in the world frame of the object's first instance.
using Vec3 = std::array<double, 3>;

struct MeshPart
{
    std::uint64_t id{0};
    std::string   name;
    std::string   kind; // model, modifier, negative, enforcer, or blocker
    std::size_t   facets{0};
};

struct MeshFacts
{
    std::vector<MeshPart> part_list; // at most 16
    Vec3        size{};
    double      volume{0};
    std::size_t facets{0};
    std::size_t parts{0};
    int         open_edges{0};
    int         edges_fixed{0}, degenerate_facets{0}, facets_removed{0}, facets_reversed{0}, backwards_edges{0};
    std::string units_suspicion; // empty, "inches", or "meters": Orca's own load-time tests
};

// A flat face group or a hole found by Orca's Measure module. A handle names
// the feature at the revision it was computed at and expires after it.
struct FaceFeature
{
    std::string handle;
    double      area{0};
    Vec3        normal{}, center{};
    // The face's extent in its own plane, longer side first, so a person's
    // "the 25 by 6 mm face" can be found.
    std::array<double, 2> size{};
};

// The circular border of a flat face that the face does not cover: the
// mouth of a hole or pocket. `axis` is the face normal, pointing out of the
// material.
struct HoleFeature
{
    std::string handle;
    double      diameter{0};
    Vec3        center{}, axis{};
};

inline constexpr std::size_t kFeatureLimit = 32;

struct ObjectFeatures
{
    std::vector<FaceFeature> faces; // largest first
    std::vector<HoleFeature> holes; // largest first
    bool                     truncated{false};
};

struct InstanceFit
{
    std::size_t            instance{0};
    std::optional<PlateId> plate; // the plate that holds it entirely; absent when none does
    bool                   inside{false};
};

struct ObjectFit
{
    std::vector<InstanceFit> instances;
    std::vector<ObjectId>    overlaps;          // objects whose footprint crosses this one on its plate
    std::vector<ObjectId>    likely_duplicates; // same shape and size
    bool                     truncated{false};
};

struct FeatureMeasurement
{
    std::optional<double> distance;     // between the nearest points
    std::optional<double> plane_distance; // between parallel planes, extended
    std::optional<double> angle;        // degrees
    std::optional<Vec3>   delta;        // x, y, z components
};

// One way to stand the object: a direction to point up, or a face handle
// to put down.
struct OrientationCandidate
{
    std::optional<Vec3> up;
    std::string         face_down;
};

struct OrientationOption
{
    Vec3                     up{};
    double                   unprintability{0}; // Orca's orient cost; lower is better
    double                   overhang{0};       // weighted overhang area, as Orca counts it
    double                   bed_contact{0};    // mm2 touching the bed
    std::vector<std::string> faces_down;        // feature handles that would rest on the bed
};

inline constexpr std::size_t kOrientationLimit = 8;

struct AnalysisRequest
{
    bool mesh{false};
    bool features{false};
    bool fit{false};
    bool orientations{false};
    std::vector<OrientationCandidate> candidates; // empty: Orca's own candidates
    std::optional<std::pair<std::string, std::string>> measure;
};

struct ObjectAnalysis
{
    std::optional<MeshFacts>          mesh;
    std::optional<ObjectFeatures>     features;
    std::optional<ObjectFit>          fit;
    std::optional<FeatureMeasurement> measurement;
    std::optional<std::vector<OrientationOption>> orientations;
};

// One row of workspace_inspect's objects section.
struct ObjectDetails
{
    ObjectId             id;
    std::string          name;
    std::vector<PlateId> plates;
    std::size_t          instances{0}, parts{0}, modifiers{0}, negative_parts{0}, support_volumes{0};
    bool                 printable{true};
    int                  extruder{0};
    Vec3                 size{};
    std::size_t          overrides{0};
};

// Place one object instance for printing. Every facet is optional; the
// adapter applies them in a fixed order -- units, scale, mirror, rotation,
// position, drop -- under one undo step, then starts auto-orient, which runs
// as Orca's own job.
struct PlacementRequest
{
    std::size_t                          instance{0};
    std::string                          units_fix;   // "", "inches", or "meters"
    std::optional<Vec3>                  scale;       // factors
    std::optional<std::pair<int, double>> scale_to;   // axis 0-2, size in mm; uniform
    std::string                          mirror_axis; // "", "x", "y", or "z"
    std::string                          face_down;   // a face handle from object_analyze
    std::optional<Vec3>                  rotate;      // degrees about the world axes, x then y then z
    std::optional<std::array<double, 2>> position;    // the instance's x, y on the bed
    bool                                 drop_to_bed{false};
    bool                                 auto_orient{false};
};

// Lay out the job: per-object rows, per-plate rows (a row without an id
// adds a plate), and an optional arrange that runs as Orca's own job.
struct LayoutObject
{
    ObjectId                   id;
    std::optional<bool>        enabled;
    std::optional<std::size_t> quantity;
    std::optional<PlateId>     plate;
    std::optional<std::string> name;
    std::optional<int>         extruder;
};

struct LayoutPlate
{
    std::optional<PlateId>     id;
    std::optional<std::string> name;
    std::optional<std::string> bed_type; // untranslated plate name
};

struct LayoutArrange
{
    std::optional<PlateId> plate; // absent: every unlocked plate
    std::optional<double>  spacing;
    std::optional<bool>    rotation;
};

struct LayoutRequest
{
    std::vector<LayoutObject>    objects;
    std::vector<LayoutPlate>     plates;
    std::optional<LayoutArrange> arrange;
};

struct LayoutResult
{
    std::vector<PlateId> added_plates;
    bool                 arranging{false};
};

// What a placement left: the object may be a new one (units conversion
// replaces it), and auto-orient is still running under the job handle.
struct PlacementResult
{
    ObjectId        object;
    ObjectTransform transform;
    Vec3            size{};
    bool            orienting{false};
};

// What the selected presets say the hardware is. The plate type is the
// untranslated name Orca stores, so it compares with what a person states.
struct ConfiguredFilament
{
    std::string preset;
    std::string material;
};

struct ConfiguredPrinter
{
    std::string                     preset;
    std::string                     model; // the device model id a matching printer reports
    std::vector<double>             nozzle_diameters;
    std::string                     plate_type;
    std::vector<ConfiguredFilament> filaments;
};

// Establish the hardware for a job. Every field is optional; what is left
// out stays as it is unless a change it depends on forces Orca to replace it,
// which the preview reports as a substitution.
struct PrinterSetupRequest
{
    std::optional<std::string>              printer_preset; // canonical preset names
    std::optional<std::string>              plate_type;     // untranslated plate name
    std::optional<std::string>              process_preset;
    std::optional<std::vector<std::string>> filament_presets; // from the first slot
    // Orca asks what to do with unsaved edits in a preset it switches away
    // from. There is no dialog here: the caller says so, or the setup is refused.
    bool                                    discard_unsaved_edits{false};

    bool empty() const { return !printer_preset && !plate_type && !process_preset && !filament_presets; }
};

struct SetupIssue
{
    std::string code, message;
};

// kind: printer, plate, process, or filament.
struct SetupSubstitution
{
    std::string kind, from, reason;
};

struct UnsavedEdits
{
    std::string kind, preset;
    std::size_t count{0};
};

struct PrinterSetupPreview
{
    bool                           valid{false};
    std::vector<SetupIssue>        issues;
    ConfiguredPrinter              resulting;
    std::string                    process_preset;
    // What Orca replaces on its own; `resulting` still names the current
    // preset there, because which one Orca picks is its own scoring.
    std::vector<SetupSubstitution> substitutions;
    std::vector<UnsavedEdits>      unsaved_edits;
};

// What the project file says about itself, as OrcaSlicer's Project panel
// shows it, and the files packed beside the model. Every text field is the
// file's own words; nothing here is inferred.
struct ProjectAttachment
{
    std::string   id;     // path inside the attachments folder, '/'-separated
    std::string   folder; // Orca's category folder, such as "Model Pictures"
    std::uint64_t bytes{0};
};

struct ProjectDetails
{
    std::string title, designer, description, license, copyright, origin;
    std::string profile_title, profile_description;
    std::vector<ProjectAttachment> attachments; // sorted by id, at most kAttachmentLimit
    bool attachments_truncated{false};
    // False when edits since the last automatic backup would be lost in a crash.
    bool backup_current{true};
};

inline constexpr std::size_t kAttachmentLimit = 64;

// Replace the open project: a project file, a model file as a new project,
// or an empty project. Every question Orca would ask on the way is an input
// here or answered with the conservative choice and reported.
enum class UnitChoice : std::uint8_t { Keep, ConvertIfTiny, Inches };

struct ProjectOpenRequest
{
    std::string path; // absolute, UTF-8; empty with new_project
    bool        new_project{false};
    bool        load_project_settings{true};
    UnitChoice  units{UnitChoice::Keep};
    bool        scale_oversized{false};
    bool        discard_unsaved{false};
};

// Add the objects of a model file to the open project.
struct ImportRequest
{
    std::string            path; // absolute, UTF-8
    std::optional<PlateId> plate;
    UnitChoice             units{UnitChoice::Keep};
    bool                   scale_oversized{false};
};

// One thing to delete: an object, a part or an instance of one, or a plate.
struct DeleteItem
{
    enum class Kind : std::uint8_t { Object, Part, Instance, Plate };
    Kind          kind{Kind::Object};
    ObjectId      object;
    std::uint64_t part{0};
    std::size_t   instance{0};
    PlateId       plate;
};

// One question Orca asked while opening, and what it was told.
struct LoadDecision
{
    std::string question;
    std::string answer; // yes, no, ok, or cancel
};

// One real step in the project's undo history. `id` is stable while the
// project stays open; a project replacement starts a new history, which is
// why a restore also names the session.
struct HistoryStep
{
    std::uint64_t id{0};
    std::string   label; // OrcaSlicer's own step name, which may be empty
    bool          applied{false};
};

struct WorkspaceHistory
{
    std::vector<HistoryStep> steps; // oldest first; the newest kHistoryLimit
    bool                     truncated{false};
    // False while a tool such as a gizmo keeps its own undo history, during
    // which the project history cannot be moved.
    bool                     restorable{false};
};

inline constexpr std::size_t kHistoryLimit = 32;

// Where a restore leaves a step: undone (before) or done (after).
enum class HistoryPoint : std::uint8_t { Before, After };

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
    // The filament type of each entry in `materials`, such as PLA; empty
    // where the device did not say.
    std::vector<std::string> material_types;
    // The machine the app is working with.
    bool                     selected{false};
    std::optional<int>       progress_percent;
    std::string              job;
    std::optional<double>    nozzle_temperature;
    std::optional<double>    bed_temperature;
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
    StaleSettings,
    // A feature handle was computed at an earlier revision.
    FeatureExpired
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
    // Read for an object: whether the value is the object's own override.
    std::optional<bool> overridden;
};

// What a settings call reads or writes: the process preset, or one object's
// overrides on top of it (Orca's per-object settings, in its ModelConfig).
// The caller checks that the object is in the open project.
struct SettingsTarget
{
    std::optional<ObjectId> object;
};

struct SettingIssue
{
    std::string key, code, message;
    std::vector<std::string> allowed, suggestions;
    std::optional<double> min, max;
};

struct SettingsQuery
{
    std::string text;
    std::size_t limit{10};
    std::string cursor;
    bool        writable_only{false};
    bool        changed_only{false}; // only settings that differ from the saved preset
};
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
struct SettingsPatch
{
    std::map<std::string, std::string> changes;
    SettingsTarget                     target;
};
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
    virtual CommandResult lay_out(const LayoutRequest& request, const std::string& job_handle, LayoutResult& result) = 0;
    virtual CommandResult place_object(ObjectId id, const PlacementRequest& request, const std::string& job_handle,
                                       PlacementResult& result) = 0;
    virtual CommandResult analyze_object(ObjectId id, const AnalysisRequest& request, ObjectAnalysis& result) const = 0;
    virtual std::vector<ObjectDetails> object_details() const = 0;
    virtual ConfiguredPrinter configured_printer() const = 0;
    virtual std::string current_process_preset() const = 0;
    // Read-only: evaluates compatibility without selecting, and leaves every
    // preset and the revision as they were.
    virtual PrinterSetupPreview preview_printer_setup(const PrinterSetupRequest& request) const = 0;
    // Applies in Orca's order -- printer, plate, process, filaments -- and
    // reads the result back into `applied`, substitutions included.
    virtual CommandResult apply_printer_setup(const PrinterSetupRequest& request, PrinterSetupPreview& applied) = 0;
    virtual WorkspaceHistory history() const = 0;
    // Undo or redo until `step` is undone (Before) or done (After).
    virtual CommandResult restore_history(std::uint64_t step, HistoryPoint point) = 0;
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
    virtual SettingsReadResult read_settings(const std::vector<std::string>& keys, const SettingsTarget& target = {}) const = 0;
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
    virtual ProjectDetails project_details() const = 0;
    virtual CommandResult open_project(const ProjectOpenRequest& request, std::vector<LoadDecision>& decisions) = 0;

    // Imports a model or project file's geometry into the CURRENT project,
    // adding objects rather than replacing the project. It is a single
    // undoable manufacturing change: the session is unchanged, prior IDs stay
    // valid, revision advances, and a Contents change is published. On success
    // object_id is the first added object (when one can be identified).
    virtual CommandResult import_objects(const ImportRequest& request, std::vector<LoadDecision>& decisions,
                                         std::vector<ObjectId>& added) = 0;
    virtual CommandResult delete_items(const std::vector<DeleteItem>& items) = 0;

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
