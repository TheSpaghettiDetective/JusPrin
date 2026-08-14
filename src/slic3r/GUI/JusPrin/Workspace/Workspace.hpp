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

template<class Tag> class StrongId
{
public:
    constexpr StrongId() = default;
    explicit constexpr StrongId(std::uint64_t value) : m_value(value) {}

    explicit constexpr operator bool() const { return m_value != 0; }
    constexpr std::uint64_t value() const { return m_value; }

    friend constexpr bool operator==(StrongId lhs, StrongId rhs) { return lhs.m_value == rhs.m_value; }
    friend constexpr bool operator!=(StrongId lhs, StrongId rhs) { return !(lhs == rhs); }
    friend constexpr bool operator<(StrongId lhs, StrongId rhs) { return lhs.m_value < rhs.m_value; }

private:
    std::uint64_t m_value{0};
};

struct PlateIdTag;
struct ObjectIdTag;
using PlateId  = StrongId<PlateIdTag>;
using ObjectId = StrongId<ObjectIdTag>;

struct ObjectTransform
{
    std::array<double, 3> position{};
    std::array<double, 3> rotation{};
    std::array<double, 3> scale{{1.0, 1.0, 1.0}};
};

struct WorkspaceObject
{
    ObjectId id;
    std::string name;
    std::vector<ObjectTransform> instances;
};

struct WorkspacePlate
{
    PlateId id;
    std::string name;
    bool active{false};
    std::vector<WorkspaceObject> objects;
};

enum class SelectionStatus : std::uint8_t { None, Objects, Unsupported };

struct WorkspaceSnapshot
{
    std::uint64_t revision{0};
    std::vector<WorkspacePlate> plates;
    std::optional<PlateId> active_plate;
    SelectionStatus selection_status{SelectionStatus::None};
    std::vector<ObjectId> selected_objects;
    bool can_undo{false};
    bool can_redo{false};
};

enum class WorkspaceError : std::uint8_t {
    None,
    InvalidId,
    MissingObject,
    StaleId,
    UnsupportedSelection,
    UnavailableOperation,
    InvalidArgument
};

struct CommandResult
{
    WorkspaceError error{WorkspaceError::None};
    std::string message;
    std::optional<ObjectId> object_id;

    bool succeeded() const { return error == WorkspaceError::None; }

    static CommandResult success(std::optional<ObjectId> id = std::nullopt) { return {WorkspaceError::None, {}, id}; }

    static CommandResult failure(WorkspaceError error, std::string message) { return {error, std::move(message), std::nullopt}; }
};

enum class WorkspaceChangeReasons : std::uint32_t {
    None      = 0,
    Selection = 1u << 0,
    Contents  = 1u << 1,
    History   = 1u << 2,
    Transform = 1u << 3,
    Plates    = 1u << 4
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
    std::uint64_t revision{0};
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

// The hub is GUI-independent. Scheduling a flush on the next safe event-loop
// turn is the responsibility of the concrete adapter.
class WorkspaceChangeHub
{
public:
    WorkspaceChangeHub() : m_state(std::make_shared<State>()) {}

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

    std::optional<WorkspaceChanged> flush()
    {
        if (!has_pending())
            return std::nullopt;

        WorkspaceChanged change{++m_state->revision, m_state->pending};
        m_state->pending = WorkspaceChangeReasons::None;

        std::vector<WorkspaceChangedCallback> callbacks;
        callbacks.reserve(m_state->observers.size());
        for (const auto& observer : m_state->observers)
            callbacks.emplace_back(observer.second);
        for (const auto& callback : callbacks)
            callback(change);
        return change;
    }

private:
    struct State
    {
        std::uint64_t revision{0};
        std::uint64_t next_observer_id{1};
        WorkspaceChangeReasons pending{WorkspaceChangeReasons::None};
        std::map<std::uint64_t, WorkspaceChangedCallback> observers;
    };

    std::shared_ptr<State> m_state;
};

class IWorkspace
{
public:
    virtual ~IWorkspace() = default;

    virtual WorkspaceSnapshot snapshot() const                                 = 0;
    virtual CommandResult select_object(ObjectId id)                           = 0;
    virtual CommandResult rename_object(ObjectId id, const std::string& name)  = 0;
    virtual CommandResult duplicate_object(ObjectId id)                        = 0;
    virtual CommandResult remove_object(ObjectId id)                           = 0;
    virtual CommandResult undo()                                               = 0;
    virtual CommandResult redo()                                               = 0;
    virtual WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) = 0;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
