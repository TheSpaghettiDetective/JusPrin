#pragma once

// Native coordinator for Agent tool execution. Any Agent — the deterministic
// mock today, an MCP adapter later — proposes typed tool requests here; the
// coordinator holds the authoritative activity records and executes actions
// exclusively through the IWorkspace
// contract, so Orca's own commands, history, and events stay in charge.
// GUI-free and deterministic: execution advances only when the owner calls
// pump(), so tests can drive it without timers.

#include "ProductState.hpp"
#include "ToolExecution.hpp"
#include "ToolRegistry.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class ToolActivitySubscription
{
public:
    ToolActivitySubscription() = default;
    ToolActivitySubscription(const ToolActivitySubscription&) = delete;
    ToolActivitySubscription& operator=(const ToolActivitySubscription&) = delete;
    ToolActivitySubscription(ToolActivitySubscription&& other) noexcept : m_unsubscribe(std::move(other.m_unsubscribe)) {}
    ToolActivitySubscription& operator=(ToolActivitySubscription&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_unsubscribe = std::move(other.m_unsubscribe);
        }
        return *this;
    }
    ~ToolActivitySubscription() { reset(); }

    void reset()
    {
        if (m_unsubscribe) {
            auto unsubscribe = std::move(m_unsubscribe);
            unsubscribe();
        }
    }

    explicit operator bool() const { return static_cast<bool>(m_unsubscribe); }

private:
    explicit ToolActivitySubscription(std::function<void()> unsubscribe) : m_unsubscribe(std::move(unsubscribe)) {}
    std::function<void()> m_unsubscribe;

    friend class ToolExecutionCoordinator;
};

// Pump pacing belongs to deterministic test presentation, not to an adapter's
// untrusted ToolRequest or to public tool metadata.
struct ToolExecutionPacing
{
    int ticks{1};
};

class ToolExecutionCoordinator
{
public:
    // Invoked after every observable activity change with the updated record.
    using ActivityCallback = std::function<void(const ToolActivity&)>;

    struct ExtensionResult
    {
        bool                     handled{false};
        std::string              result_json;
        std::optional<ToolError> error;
    };
    // Typed native extensions still execute inside this coordinator and its
    // state machine. The host uses this for durable manufacturing
    // records; future MCP adapters use the same seam, never a WebView path.
    using ExtensionExecutor = std::function<ExtensionResult(ToolHandler, const ToolActivity&)>;

    explicit ToolExecutionCoordinator(Workspace::IWorkspace& workspace,
                                      const ToolRegistry& registry = ToolRegistry::instance());
    ~ToolExecutionCoordinator();

    ToolExecutionCoordinator(const ToolExecutionCoordinator&) = delete;
    ToolExecutionCoordinator& operator=(const ToolExecutionCoordinator&) = delete;

    ToolActivitySubscription subscribe(ActivityCallback listener);
    void set_extension_executor(ExtensionExecutor executor) { m_extension_executor = std::move(executor); }
    // Checks a call to a surface's own tool after the registry has and
    // before it runs. It may restate the call -- the title, or facts added to
    // the arguments -- and returns the
    // error that stops it.
    using ExtensionPreflight = std::function<std::optional<ToolError>(ToolHandler, ToolActivity&)>;
    void set_extension_preflight(ExtensionPreflight preflight) { m_extension_preflight = std::move(preflight); }

    // The store behind the intent and plan tools. The owner holds project
    // storage, so it supplies this; without it those tools report the
    // operation unavailable rather than pretending to record anything.
    void set_product_state(IProductState* state) { m_product_state = state; }

    // Action IDs default to a process-local counter; an owner with persisted
    // state injects its own allocator so IDs stay unique across restarts.
    void set_action_id_allocator(std::function<std::string()> allocator) { m_action_id_allocator = std::move(allocator); }

    // Resolves an attachment ID to the absolute path of its stored blob. The
    // owner (which holds project storage) provides this so import arguments
    // stay opaque IDs — no machine-specific path is persisted or shown.
    void set_attachment_path_resolver(std::function<std::string(const std::string&)> resolver)
    {
        m_attachment_path_resolver = std::move(resolver);
    }
    // The host can durably bracket each printable-project mutation. Empty callbacks
    // preserve GUI-free coordinator behavior in unit tests and task chats.
    void set_version_callbacks(std::function<std::string(const ToolActivity&)> before,
                               std::function<void(const ToolActivity&, const std::string&)> after)
    {
        m_version_before = std::move(before);
        m_version_after = std::move(after);
    }
    // Document-only mutations have a durability boundary without pinning or
    // publishing a model version.
    void set_document_boundary_callback(std::function<void(const ToolActivity&)> callback)
    {
        m_document_boundary = std::move(callback);
    }

    // Drops every record. For project replacement: the records belong to the
    // previous project session and any persisted history keeps its own copy.
    // Forgets every activity except one that is executing: a command that
    // replaces the project is still running while the project it was
    // queued in is torn down, and it has to be able to report.
    void clear();
    const std::string& executing_action_id() const { return m_executing; }
    void forget_if_closed(const std::string& action_id);
    // Chat deletion may forget completed records, never in-flight work.
    void forget_terminal_activities(const std::vector<std::string>& message_ids);

    // Creates a record stamped with the current workspace session and revision,
    // validates it, and starts it immediately.
    // `refusal`, when given, is why the caller will not let this call run:
    // the call is recorded and answered as failed with it, like any other
    // call refused before it starts.
    const ToolActivity& propose(const ToolRequest& request, const std::string& correlation_id,
                                ToolExecutionPacing pacing = {}, ToolSource source = ToolSource::Agent,
                                const std::string& call_id = {}, std::optional<ToolError> refusal = std::nullopt);

    bool cancel(const std::string& action_id);

    // Advances every Running activity by one deterministic tick: progress
    // first, then the native command on the final tick. The owner paces this
    // from a timer; tests call it directly.
    void pump();

    bool any_running() const;

    const std::vector<ToolActivity>& activities() const { return m_activities; }
    const ToolActivity*              find(const std::string& action_id) const;

private:
    ToolActivity* find_mutable(const std::string& action_id);
    void          start_running(ToolActivity& activity);
    void          execute(ToolActivity& activity);
    void          fail(ToolActivity& activity, std::string code, std::string message, std::string details_json = "{}");
    // The ids of the regions whose geometry is still found, and, after a
    // change, those of them that no longer are: what a place, divide or
    // repair result reports as unbound.
    std::set<std::string> bound_regions() const;
    nlohmann::json        regions_unbound(const std::set<std::string>& bound_before) const;
    void          notify(const ToolActivity& activity);
    void          invalidate_pending(const Workspace::WorkspaceChanged& change);

    struct ObserverState
    {
        std::uint64_t                             next_id{1};
        bool                                      alive{true};
        std::map<std::uint64_t, ActivityCallback> observers;
    };

    Workspace::IWorkspace&           m_workspace;
    const ToolRegistry&               m_registry;
    Workspace::WorkspaceSubscription m_workspace_subscription;
    std::shared_ptr<ObserverState>    m_observers;
    IProductState*                   m_product_state{nullptr};
    // The slice_start call behind the run now in flight, so the slicing
    // section can say which handle a reader is watching.
    std::string                      m_slice_handle;
    // Whether the run m_slice_handle started has been seen, and has ended. A
    // later run is the person's, and activity_cancel does not stop it.
    bool                             m_slice_seen_running{false};
    bool                             m_slice_ended{false};
    std::string                      m_executing;
    ExtensionExecutor                m_extension_executor;
    ExtensionPreflight               m_extension_preflight;
    std::function<std::string()>     m_action_id_allocator;
    std::function<std::string(const std::string&)> m_attachment_path_resolver;
    std::function<std::string(const ToolActivity&)> m_version_before;
    std::function<void(const ToolActivity&, const std::string&)> m_version_after;
    std::function<void(const ToolActivity&)> m_document_boundary;
    std::vector<ToolActivity>        m_activities;
    std::uint64_t                    m_next_action_id{1};
    std::uint64_t                    m_last_invalidating_revision{0};
    // The last change a settings patch cares about: a settings edit or a new
    // project. A model edit between a preview and its apply does not move it.
    std::uint64_t                    m_last_settings_revision{0};
    // slice_start calls that return when their run ends, by action id. Such a
    // call stays Running without holding up the others.
    struct SliceWait
    {
        std::optional<Workspace::PlateId>     plate;
        bool                                  seen_running{false};
        std::chrono::steady_clock::time_point started;
    };
    std::map<std::string, SliceWait> m_slice_waits;
    void finish_slice_waits();
};

} // namespace Slic3r::GUI::JusPrin::Agent
