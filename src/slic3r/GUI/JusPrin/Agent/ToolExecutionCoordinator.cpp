#include "ToolExecutionCoordinator.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;
using Workspace::WorkspaceChangeReasons;
using Workspace::WorkspaceError;

constexpr const char* kServerName = "jusprin-native";

// Workspace changes that can invalidate a pending proposal. Selection-only
// changes do not: a proposal pins its target by ID, so what the user has
// selected afterwards cannot alter what an approval would execute.
constexpr WorkspaceChangeReasons kInvalidatingReasons = WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Transform |
                                                        WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::History |
                                                        WorkspaceChangeReasons::Project;

const char* workspace_error_code(WorkspaceError error)
{
    switch (error) {
    case WorkspaceError::None: return "none";
    case WorkspaceError::InvalidId: return "invalid_id";
    case WorkspaceError::MissingObject: return "missing_object";
    case WorkspaceError::StaleId: return "stale_id";
    case WorkspaceError::UnsupportedSelection: return "unsupported_selection";
    case WorkspaceError::UnavailableOperation: return "unavailable_operation";
    case WorkspaceError::InvalidArgument: return "invalid_argument";
    case WorkspaceError::NoChange: return "no_change";
    }
    return "unknown";
}

std::optional<Workspace::ObjectId> parse_object_argument(const std::string& arguments_json)
{
    const json arguments = json::parse(arguments_json, nullptr, false);
    if (!arguments.is_object() || !arguments.contains("sessionId") || !arguments["sessionId"].is_string() ||
        !arguments.contains("objectId") || !arguments["objectId"].is_string())
        return std::nullopt;
    try {
        const std::uint64_t session = std::stoull(arguments["sessionId"].get<std::string>());
        const std::uint64_t value   = std::stoull(arguments["objectId"].get<std::string>());
        return Workspace::ObjectId(Workspace::ProjectSessionId(session), value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

ToolExecutionCoordinator::ToolExecutionCoordinator(Workspace::IWorkspace& workspace) : m_workspace(workspace)
{
    m_workspace_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        invalidate_pending(change);
    });
}

const ToolActivity& ToolExecutionCoordinator::propose(const ToolRequest& request, const std::string& correlation_id)
{
    const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();

    ToolActivity activity;
    activity.action_id         = m_action_id_allocator ? m_action_id_allocator() : "t-" + std::to_string(m_next_action_id++);
    activity.correlation_id    = correlation_id;
    activity.server            = kServerName;
    activity.tool              = request.tool;
    activity.title             = request.title;
    activity.arguments_json    = request.arguments_json;
    activity.action_class      = request.action_class;
    activity.requires_approval = approval_required(request.action_class);
    activity.session           = snapshot.session.value();
    activity.expected_revision = snapshot.revision;
    activity.progress_total    = request.run_ticks > 0 ? request.run_ticks : 1;

    m_activities.emplace_back(std::move(activity));
    ToolActivity& stored = m_activities.back();
    notify(stored);
    if (!stored.requires_approval)
        start_running(stored);
    return stored;
}

bool ToolExecutionCoordinator::approve(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr || activity->state != ToolState::Pending)
        return false;

    // The eager invalidation below normally fails a stale proposal before a
    // decision can arrive; the session check remains as a belt for decisions
    // raced against a project replacement.
    if (m_workspace.snapshot().session.value() != activity->session) {
        fail(*activity, "stale_revision", "The project changed after this action was proposed. Ask the Agent again.");
        return false;
    }
    start_running(*activity);
    return true;
}

bool ToolExecutionCoordinator::reject(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr || activity->state != ToolState::Pending)
        return false;
    activity->state = ToolState::Rejected;
    notify(*activity);
    return true;
}

bool ToolExecutionCoordinator::cancel(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr)
        return false;
    // Cancellation is possible while nothing durable has happened: before
    // approval, and while running before the final execution tick. Terminal
    // records never change.
    if (activity->state != ToolState::Pending && activity->state != ToolState::Approved &&
        activity->state != ToolState::Running)
        return false;
    activity->state = ToolState::Cancelled;
    notify(*activity);
    return true;
}

void ToolExecutionCoordinator::pump()
{
    for (ToolActivity& activity : m_activities) {
        if (activity.state != ToolState::Running)
            continue;
        if (activity.progress_current + 1 < activity.progress_total) {
            ++activity.progress_current;
            notify(activity);
        } else {
            activity.progress_current = activity.progress_total;
            execute(activity);
        }
        // One activity per tick keeps event ordering deterministic.
        return;
    }
}

bool ToolExecutionCoordinator::any_running() const
{
    for (const ToolActivity& activity : m_activities)
        if (activity.state == ToolState::Running)
            return true;
    return false;
}

const ToolActivity* ToolExecutionCoordinator::find(const std::string& action_id) const
{
    for (const ToolActivity& activity : m_activities)
        if (activity.action_id == action_id)
            return &activity;
    return nullptr;
}

ToolActivity* ToolExecutionCoordinator::find_mutable(const std::string& action_id)
{
    for (ToolActivity& activity : m_activities)
        if (activity.action_id == action_id)
            return &activity;
    return nullptr;
}

void ToolExecutionCoordinator::start_running(ToolActivity& activity)
{
    activity.state = ToolState::Approved;
    notify(activity);
    activity.state = ToolState::Running;
    notify(activity);
}

void ToolExecutionCoordinator::execute(ToolActivity& activity)
{
    if (activity.tool == "duplicate_object") {
        const std::optional<Workspace::ObjectId> id = parse_object_argument(activity.arguments_json);
        if (!id) {
            fail(activity, "invalid_arguments", "The action arguments do not identify an object.");
            return;
        }
        const Workspace::CommandResult result = m_workspace.duplicate_object(*id);
        if (!result.succeeded()) {
            fail(activity, workspace_error_code(result.error), result.message);
            return;
        }
        // Success must agree with authoritative state: the command returned
        // the created object's ID, and the committed revision advanced past
        // the proposal.
        const Workspace::WorkspaceSnapshot after = m_workspace.snapshot();
        json result_json{{"revision", after.revision}};
        if (result.object_id)
            result_json["newObjectId"] = std::to_string(result.object_id->value());
        activity.result_json = result_json.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (activity.tool == "inspect_selection") {
        const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
        json names = json::array();
        for (Workspace::ObjectId selected : snapshot.selected_objects)
            for (const Workspace::WorkspacePlate& plate : snapshot.plates)
                for (const Workspace::WorkspaceObject& object : plate.objects)
                    if (object.id == selected)
                        names.push_back(object.name);
        activity.result_json = json{{"selection", std::move(names)}, {"revision", snapshot.revision}}.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    fail(activity, "unknown_tool", "This build has no tool named \"" + activity.tool + "\".");
}

void ToolExecutionCoordinator::fail(ToolActivity& activity, std::string code, std::string message)
{
    activity.state = ToolState::Failed;
    activity.error = ToolError{std::move(code), std::move(message)};
    notify(activity);
}

void ToolExecutionCoordinator::notify(const ToolActivity& activity)
{
    if (m_listener)
        m_listener(activity);
}

void ToolExecutionCoordinator::invalidate_pending(const Workspace::WorkspaceChanged& change)
{
    // Only Pending proposals go stale eagerly. A Running action re-validates
    // naturally at execution: its pinned target either still resolves or the
    // workspace command reports the stale/missing error.
    if ((change.reasons & kInvalidatingReasons) == WorkspaceChangeReasons::None)
        return;
    for (ToolActivity& activity : m_activities) {
        if (activity.state != ToolState::Pending)
            continue;
        fail(activity, "stale_revision", "The project changed after this action was proposed. Ask the Agent again.");
    }
}

} // namespace Slic3r::GUI::JusPrin::Agent
