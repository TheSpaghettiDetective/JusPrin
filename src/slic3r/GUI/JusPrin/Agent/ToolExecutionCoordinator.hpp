#pragma once

// Native coordinator for Agent tool execution. Any Agent — the deterministic
// mock today, an MCP adapter later — submits typed tool requests here; the
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
#include <optional>
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

// What a calibration project is opened with: the project it was started
// from, the filament and printer as the person's menus name them (no
// filament from a Home printer card, whose spool is the person's to say),
// and the symptom that led to it, in the person's words.
struct CalibrationHandover
{
    std::string started_from, filament, printer, symptom;
    // A Home card's printer's loaded spools, as it reports them ("PLA
    // #FFFFFF"), for the person to say which one is being calibrated.
    std::vector<std::string> spools;
};

// "Calibration · Gray PETG · Prusa MK4", or "Calibration · Prusa MK4" with no
// filament: the project's one name, on Home, in the title and on its chat.
inline std::string calibration_project_name(const std::string& filament, const std::string& printer)
{
    return "Calibration · " + (filament.empty() ? std::string() : filament + " · ") + printer;
}

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
    // A project made from a file before (ProjectAutosave), which project_open
    // offers to continue. `changed` says whether the file differs from the
    // one the project was made from, when that is known.
    struct EarlierProject
    {
        std::string         id, name;
        bool                current{false};
        std::optional<bool> changed;
    };
    // What a lookup found: the project made from the file before, if any, or
    // that the saved projects could not be read to tell.
    struct EarlierLookup
    {
        EarlierLookup(std::optional<EarlierProject> found = std::nullopt) : project(std::move(found)) {}
        std::optional<EarlierProject> project;
        bool                          unreadable{false};
    };
    // Finds, opens, and tells the owner which file the new project came
    // from. Without them every file opens as a new project.
    void set_earlier_projects(std::function<EarlierLookup(const std::string& path)> find,
                              std::function<bool(const std::string& id)> open,
                              std::function<void(const std::string& path)> opened_from)
    {
        m_find_earlier = std::move(find);
        m_open_earlier = std::move(open);
        m_opened_from  = std::move(opened_from);
    }

    // Hears the load report of a project the in-app Agent opened. Opening
    // one replaces the conversation along with the project, so the call's
    // own result reaches no model: the report goes to the opened project's
    // chat instead, as a person's open does. An MCP client's open is its own.
    // The run a slice_start began has ended in the project it started in,
    // by that call's action id, whether it sliced, failed or was stopped.
    void set_slice_ended(std::function<void(const std::string& action_id)> listener) { m_slice_ended = std::move(listener); }
    // The action id of the slice_start whose run is in flight, or empty.
    std::string slice_in_flight() const { return m_slice_running ? m_slice_handle : std::string(); }
    void set_agent_project_opened(std::function<void(const Workspace::LoadReport&)> listener)
    {
        m_agent_project_opened = std::move(listener);
    }
    // The same for a calibration test the in-app Agent generated: its result
    // reaches no model either, and the test's project opens its chat with
    // the note: which project it started from, and that its settings are
    // temporary.
    // The calibration project calibration_generate makes its test in
    // (handoff 5.1). The host owns what a calibration project is.
    struct CalibrationProject
    {
        // Whether the open project is a calibration project.
        std::function<bool()> is_open;
        // A calibration project the tool opened for the person's own project
        // (the symptom path): the host marks it and posts its handover.
        std::function<void(const CalibrationHandover& handover, bool by_agent)> opened;
        // Runs the generator so that the new project it starts continues the
        // calibration project instead of replacing it.
        std::function<void(const std::function<void()>& generate)> continuing;
        // The generator ran, made or not, for the project's record and the
        // handover turn a project the tool opened is waiting to start.
        std::function<void(bool made, bool by_agent)> finished;
    };
    void set_calibration_project(CalibrationProject hooks) { m_calibration = std::move(hooks); }

    // print_dialog_open's way to OrcaSlicer's send dialog, which the host owns:
    // for a plate's slice, or (`gcode`, a G-code hash or its leading digits)
    // for a recorded G-code's kept file. The host refuses with a tool error
    // code and message, or shows the dialog on its next turn and says which.
    struct PrintDialogRequest
    {
        std::optional<Workspace::PlateId> plate;
        std::string                       gcode;
    };
    struct PrintDialogOutcome
    {
        std::string error; // a ToolErrors code; empty when the dialog opens
        std::string message;
        std::string dialog; // "select_machine" or "print_host"
    };
    void set_print_dialog_opener(std::function<PrintDialogOutcome(const PrintDialogRequest&)> opener)
    {
        m_print_dialog_opener = std::move(opener);
    }

    // The project chat's rule for saving a preset: the person saves it. A
    // settings preview with persistAs from the in-app Agent then carries a
    // proposal (ToolActivity::proposal_json) the page draws as a card with
    // Save preset, and the Agent's own settings_apply_patch with persistAs is
    // refused. Off for the printer panel, whose chats save their own presets.
    void set_saves_need_approval(bool on) { m_saves_need_approval = on; }
    // The person's Save preset on a proposal: applies exactly what the
    // preview proposed, against its session and revision, as an apply whose
    // approves names the preview. Empty when it was queued; otherwise why not.
    std::string approve_proposal(const std::string& preview_action_id);
    // An approved save that succeeded, for the host to confirm in the chat.
    void set_proposal_saved(std::function<void(const ToolActivity& apply)> listener)
    {
        m_proposal_saved = std::move(listener);
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
    const ToolActivity& submit(const ToolRequest& request, const std::string& correlation_id,
                               ToolExecutionPacing pacing = {}, ToolSource source = ToolSource::Agent,
                               const std::string& call_id = {}, const std::string& client = {});

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
    // Whether the run m_slice_handle started is still in flight. A later run
    // is the person's, and activity_cancel does not stop it.
    bool                             m_slice_running{false};
    // The project that run belongs to. Action ids restart with each project,
    // so in another project the handle names nothing.
    Workspace::ProjectSessionId      m_slice_session;
    std::string                      m_executing;
    ExtensionExecutor                m_extension_executor;
    ExtensionPreflight               m_extension_preflight;
    std::function<std::string()>     m_action_id_allocator;
    std::function<std::string(const std::string&)> m_attachment_path_resolver;
    std::function<std::string(const ToolActivity&)> m_version_before;
    std::function<void(const ToolActivity&, const std::string&)> m_version_after;
    std::function<void(const ToolActivity&)> m_document_boundary;
    std::function<EarlierLookup(const std::string&)> m_find_earlier;
    std::function<bool(const std::string&)>                          m_open_earlier;
    std::function<void(const std::string&)>                          m_opened_from;
    std::function<void(const Workspace::LoadReport&)>                m_agent_project_opened;
    CalibrationProject                                               m_calibration;
    std::function<PrintDialogOutcome(const PrintDialogRequest&)>     m_print_dialog_opener;
    std::function<void(const ToolActivity&)>                         m_proposal_saved;
    bool                             m_saves_need_approval{false};
    // The preview an approve_proposal is submitting the apply for.
    std::string                      m_approving;
    std::vector<ToolActivity>        m_activities;
    std::uint64_t                    m_next_action_id{1};
    std::uint64_t                    m_last_invalidating_revision{0};
    // The last change a settings patch cares about: a settings edit or a new
    // project. A model edit between a preview and its apply does not move it.
    std::uint64_t                    m_last_settings_revision{0};
    std::function<void(const std::string&)> m_slice_ended;
};

} // namespace Slic3r::GUI::JusPrin::Agent
