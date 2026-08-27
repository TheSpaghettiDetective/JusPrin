#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace Slic3r::GUI {

enum class ProjectStateChangeReason : std::uint32_t {
    None      = 0,
    Selection = 1u << 0,
    Objects   = 1u << 1,
    History   = 1u << 2,
    Transform = 1u << 3,
    Plates    = 1u << 4,
    Project   = 1u << 5
};

constexpr ProjectStateChangeReason operator|(ProjectStateChangeReason lhs, ProjectStateChangeReason rhs)
{
    return static_cast<ProjectStateChangeReason>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

inline ProjectStateChangeReason& operator|=(ProjectStateChangeReason& lhs, ProjectStateChangeReason rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

struct ProjectStateChanged
{
    std::uint64_t            sequence{0};
    std::uint64_t            project_session{1};
    ProjectStateChangeReason reasons{ProjectStateChangeReason::None};
    bool                     project_replaced{false};
};

using ProjectStateChangedCallback = std::function<void(const ProjectStateChanged&)>;

class ProjectStateSubscription
{
public:
    ProjectStateSubscription() = default;
    ProjectStateSubscription(const ProjectStateSubscription&) = delete;
    ProjectStateSubscription& operator=(const ProjectStateSubscription&) = delete;
    ProjectStateSubscription(ProjectStateSubscription&& other) noexcept : m_unsubscribe(std::move(other.m_unsubscribe)) {}
    ProjectStateSubscription& operator=(ProjectStateSubscription&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_unsubscribe = std::move(other.m_unsubscribe);
        }
        return *this;
    }
    ~ProjectStateSubscription() { reset(); }

    void reset()
    {
        if (m_unsubscribe) {
            auto unsubscribe = std::move(m_unsubscribe);
            unsubscribe();
        }
    }

private:
    explicit ProjectStateSubscription(std::function<void()> unsubscribe) : m_unsubscribe(std::move(unsubscribe)) {}
    std::function<void()> m_unsubscribe;

    friend class ProjectStateObserverHub;
};

class ProjectStateTransaction
{
public:
    ProjectStateTransaction() = default;
    ProjectStateTransaction(const ProjectStateTransaction&) = delete;
    ProjectStateTransaction& operator=(const ProjectStateTransaction&) = delete;
    ProjectStateTransaction(ProjectStateTransaction&& other) noexcept : m_finish(std::move(other.m_finish)) {}
    ProjectStateTransaction& operator=(ProjectStateTransaction&& other) noexcept
    {
        if (this != &other) {
            finish();
            m_finish = std::move(other.m_finish);
        }
        return *this;
    }
    ~ProjectStateTransaction() { finish(); }

    void finish()
    {
        if (m_finish) {
            auto finish = std::move(m_finish);
            finish();
        }
    }

private:
    explicit ProjectStateTransaction(std::function<void()> finish) : m_finish(std::move(finish)) {}
    std::function<void()> m_finish;

    friend class ProjectStateObserverHub;
};

// Synchronous, GUI-thread observer seam for committed project changes. Nested
// transactions merge all low-level reasons into one authoritative event.
class ProjectStateObserverHub
{
public:
    ProjectStateObserverHub() : m_state(std::make_shared<State>()) {}
    ProjectStateObserverHub(const ProjectStateObserverHub&) = delete;
    ProjectStateObserverHub& operator=(const ProjectStateObserverHub&) = delete;
    ~ProjectStateObserverHub()
    {
        m_state->alive = false;
        m_state->observers.clear();
    }

    ProjectStateSubscription subscribe(ProjectStateChangedCallback callback)
    {
        const std::uint64_t id = m_state->next_observer_id++;
        m_state->observers.emplace(id, std::move(callback));
        std::weak_ptr<State> state = m_state;
        return ProjectStateSubscription([state, id]() {
            if (auto locked = state.lock())
                locked->observers.erase(id);
        });
    }

    std::uint64_t project_session() const { return m_state->project_session; }

    ProjectStateTransaction transaction()
    {
        ++m_state->transaction_depth;
        std::weak_ptr<State> state = m_state;
        return ProjectStateTransaction([state]() {
            if (auto locked = state.lock()) {
                if (locked->transaction_depth > 0)
                    --locked->transaction_depth;
                if (locked->transaction_depth == 0)
                    dispatch_pending(*locked);
            }
        });
    }

    void publish(ProjectStateChangeReason reasons, bool project_replaced = false)
    {
        if (reasons == ProjectStateChangeReason::None)
            return;
        m_state->pending_reasons |= reasons;
        m_state->pending_project_replaced = m_state->pending_project_replaced || project_replaced;
        if (m_state->transaction_depth == 0) {
            std::shared_ptr<State> state = m_state;
            dispatch_pending(*state);
        }
    }

private:
    struct State
    {
        std::uint64_t                                      sequence{0};
        std::uint64_t                                      project_session{1};
        std::uint64_t                                      next_observer_id{1};
        std::size_t                                        transaction_depth{0};
        ProjectStateChangeReason                           pending_reasons{ProjectStateChangeReason::None};
        bool                                               pending_project_replaced{false};
        bool                                               alive{true};
        std::map<std::uint64_t, ProjectStateChangedCallback> observers;
    };

    static void dispatch_pending(State& state)
    {
        if (state.pending_reasons == ProjectStateChangeReason::None)
            return;

        if (state.pending_project_replaced)
            ++state.project_session;
        const ProjectStateChanged change{++state.sequence, state.project_session, state.pending_reasons,
                                         state.pending_project_replaced};
        state.pending_reasons          = ProjectStateChangeReason::None;
        state.pending_project_replaced = false;

        std::vector<std::uint64_t> observer_ids;
        observer_ids.reserve(state.observers.size());
        for (const auto& observer : state.observers)
            observer_ids.emplace_back(observer.first);
        for (const std::uint64_t id : observer_ids) {
            if (!state.alive)
                break;
            const auto observer = state.observers.find(id);
            if (observer == state.observers.end())
                continue;
            ProjectStateChangedCallback callback = observer->second;
            callback(change);
        }
    }

    std::shared_ptr<State> m_state;
};

} // namespace Slic3r::GUI
