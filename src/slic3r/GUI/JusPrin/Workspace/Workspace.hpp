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

struct WorkspacePlate
{
    PlateId                      id;
    std::string                  name;
    bool                         active{false};
    // True when this plate holds a currently valid slice result. Derived from
    // the authoritative plate state on every snapshot, never cached.
    bool                         sliced{false};
    std::vector<WorkspaceObject> objects;
};

enum class SelectionStatus : std::uint8_t { None, Objects, Unsupported };

// Compact project and machine setup facts for consumers (the Agent context)
// that must describe the workspace without reaching into Orca types. Values
// are read fresh from their authoritative owners at snapshot time.
struct WorkspaceSetup
{
    std::string project_name;
    bool        project_dirty{false};
    std::string printer_preset;
    std::string filament_preset;

    friend bool operator==(const WorkspaceSetup& lhs, const WorkspaceSetup& rhs)
    {
        return lhs.project_name == rhs.project_name && lhs.project_dirty == rhs.project_dirty &&
               lhs.printer_preset == rhs.printer_preset && lhs.filament_preset == rhs.filament_preset;
    }
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
};

enum class WorkspaceError : std::uint8_t {
    None,
    InvalidId,
    MissingObject,
    StaleId,
    UnsupportedSelection,
    UnavailableOperation,
    InvalidArgument,
    NoChange
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
    Project   = 1u << 5
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
    virtual WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) = 0;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
