#include "ToolExecutionCoordinator.hpp"
#include "ToolResults.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

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
                                                        WorkspaceChangeReasons::Project | WorkspaceChangeReasons::Settings;

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
    case WorkspaceError::InvalidSettings: return "invalid_settings";
    case WorkspaceError::StaleSettings: return "stale_workspace";
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

Workspace::PrinterSetupRequest setup_request(const json& arguments)
{
    Workspace::PrinterSetupRequest request;
    if (arguments.contains("printerPreset")) request.printer_preset = arguments["printerPreset"].get<std::string>();
    if (arguments.contains("plateType")) request.plate_type = arguments["plateType"].get<std::string>();
    if (arguments.contains("processPreset")) request.process_preset = arguments["processPreset"].get<std::string>();
    if (arguments.contains("filamentPresets")) request.filament_presets = arguments["filamentPresets"].get<std::vector<std::string>>();
    request.discard_unsaved_edits = arguments.value("unsavedEdits", "") == "discard";
    return request;
}

// What a setup would do, as the card and the staleness check compare it.
json setup_outcome(const Workspace::PrinterSetupPreview& preview)
{
    json filaments = json::array();
    for (const auto& filament : preview.resulting.filaments) filaments.push_back(filament.preset);
    return {{"printer", preview.resulting.preset}, {"plate", preview.resulting.plate_type},
            {"process", preview.process_preset}, {"filaments", std::move(filaments)},
            {"substituted", setup_substitutions_result(preview.substitutions)},
            {"discarded", setup_edits_result(preview.unsaved_edits)}};
}

Workspace::SettingsPatch settings_patch(const json& arguments)
{
    return {arguments.at("changes").get<std::map<std::string, std::string>>()};
}

json stale_settings_details(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot)
{
    return {{"expectedSessionId", arguments.at("expectedSessionId")}, {"expectedRevision", arguments.at("expectedRevision")},
            {"currentSessionId", std::to_string(snapshot.session.value())}, {"currentRevision", snapshot.revision}};
}

} // namespace

ToolExecutionCoordinator::ToolExecutionCoordinator(Workspace::IWorkspace& workspace, const ToolRegistry& registry)
    : m_workspace(workspace), m_registry(registry), m_observers(std::make_shared<ObserverState>())
{
    m_workspace_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        invalidate_pending(change);
    });
}

ToolExecutionCoordinator::~ToolExecutionCoordinator()
{
    m_observers->alive = false;
    m_observers->observers.clear();
}

ToolActivitySubscription ToolExecutionCoordinator::subscribe(ActivityCallback listener)
{
    const std::uint64_t id = m_observers->next_id++;
    m_observers->observers.emplace(id, std::move(listener));
    std::weak_ptr<ObserverState> weak_state = m_observers;
    return ToolActivitySubscription([weak_state, id]() {
        if (auto state = weak_state.lock())
            state->observers.erase(id);
    });
}

const ToolActivity& ToolExecutionCoordinator::propose(const ToolRequest& request, const std::string& correlation_id,
                                                      ToolExecutionPacing pacing, ToolSource source)
{
    const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
    const ToolDefinition* definition = m_registry.find(request.tool);

    ToolActivity activity;
    activity.action_id         = m_action_id_allocator ? m_action_id_allocator() : "t-" + std::to_string(m_next_action_id++);
    activity.correlation_id    = correlation_id;
    activity.source            = source;
    activity.server            = kServerName;
    activity.tool              = request.tool;
    activity.arguments_json    = request.arguments_json;
    activity.session           = snapshot.session.value();
    activity.expected_revision = snapshot.revision;
    activity.progress_total    = pacing.ticks > 0 ? pacing.ticks : 1;

    if (definition != nullptr) {
        activity.title             = definition->title;
        activity.action_class      = definition->action_class;
        activity.requires_approval = m_registry.requires_approval(*definition, request.arguments_json);
    } else {
        activity.title = "Unknown tool request";
    }

    m_activities.emplace_back(std::move(activity));
    ToolActivity& stored = m_activities.back();

    if (definition == nullptr) {
        fail(stored, "unknown_tool", "This build has no tool named \"" + request.tool + "\".");
        return stored;
    }

    ToolValidationResult validation = m_registry.validate_call(*definition, request.arguments_json);
    if (!validation.valid()) {
        fail(stored, validation.error->code, validation.error->message);
        return stored;
    }
    stored.arguments_json = std::move(validation.arguments_json);
    stored.title = m_registry.approval_title(*definition, stored.arguments_json);

    if (definition->handler == ToolHandler::HistoryRestore) {
        const auto arguments = json::parse(stored.arguments_json);
        if (arguments["sessionId"] != std::to_string(snapshot.session.value())) {
            fail(stored, "stale_id", "That history belongs to a project that is no longer open. Read it again.");
            return stored;
        }
        const auto history = m_workspace.history();
        const auto step    = std::find_if(history.steps.begin(), history.steps.end(), [&arguments](const Workspace::HistoryStep& candidate) {
            return std::to_string(candidate.id) == arguments["stepId"];
        });
        if (step == history.steps.end()) {
            fail(stored, "stale_id", "That step is no longer in the history. Read it again.");
            return stored;
        }
        const std::string name = step->label.empty() ? "an unnamed step" : "\xe2\x80\x9c" + step->label + "\xe2\x80\x9d";
        stored.title = (arguments["point"] == "before" ? "Go back to before " : "Go to just after ") + name;
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::PrinterSetup) {
        auto arguments = json::parse(stored.arguments_json);
        const Workspace::PrinterSetupRequest request = setup_request(arguments);
        if (!request.empty()) {
            const auto preview = m_workspace.preview_printer_setup(request);
            if (!preview.valid) {
                fail(stored, preview.issues.front().code, preview.issues.front().message,
                     printer_setup_preview_result(preview, json::array(), snapshot).dump());
                return stored;
            }
            if (!preview.unsaved_edits.empty() && !request.discard_unsaved_edits) {
                fail(stored, "unsaved_edits",
                     "This would drop unsaved preset edits. Ask the user, then call again with unsavedEdits \"discard\".",
                     printer_setup_preview_result(preview, json::array(), snapshot).dump());
                return stored;
            }
            // The approved outcome, compared again before anything is applied.
            arguments["confirmedSetup"] = setup_outcome(preview);
            stored.title = "Set up " + preview.resulting.preset;
            if (request.plate_type) stored.title += ", " + preview.resulting.plate_type;
            if (request.process_preset) stored.title += ", " + preview.process_preset;
            if (request.filament_presets)
                for (std::size_t slot = 0; slot < request.filament_presets->size(); ++slot)
                    stored.title += ", " + (*request.filament_presets)[slot];
            for (const auto& substitution : preview.substitutions)
                stored.title += "; replaces " + substitution.kind + " " + substitution.from;
            for (const auto& edits : preview.unsaved_edits)
                stored.title += "; discards " + std::to_string(edits.count) + " unsaved edits in " + edits.preset;
        } else {
            stored.title = "Record printer facts";
        }
        if (arguments.contains("confirmFacts")) {
            if (m_product_state == nullptr || !m_product_state->has_printer_facts()) {
                fail(stored, "unavailable_operation", "This build cannot record printer facts.");
                return stored;
            }
            for (const auto& fact : arguments["confirmFacts"])
                stored.title += "; " + fact["fact"].get<std::string>() + ": " + fact["value"].get<std::string>();
        }
        stored.arguments_json = arguments.dump();
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ProjectOpen) {
        const auto arguments = json::parse(stored.arguments_json);
        const bool unsaved   = snapshot.setup.project_dirty || snapshot.setup.presets_dirty;
        if (unsaved && arguments.value("unsavedWork", "") != "discard") {
            fail(stored, "unsaved_work",
                 "The open project has unsaved changes. Ask the user; save it first, or call again with unsavedWork \"discard\".");
            return stored;
        }
        if (arguments.contains("path")) {
            // Only the name is looked at before approval; the file is read
            // after it.
            const std::filesystem::path path = std::filesystem::u8path(arguments["path"].get<std::string>());
            std::error_code error;
            if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error)) {
                fail(stored, "invalid_argument", "Give the absolute path of a file that exists.");
                return stored;
            }
            if (path.extension() == ".3mf" && arguments.value("unitConversion", "keep") == "inches") {
                fail(stored, "invalid_argument", "A project keeps its own units; inches applies to model files.");
                return stored;
            }
            stored.title = "Open " + arguments["path"].get<std::string>();
        } else {
            stored.title = "Start a new project";
        }
        if (unsaved)
            stored.title += ", discarding unsaved changes" +
                            (snapshot.setup.project_name.empty() ? std::string() : " to " + snapshot.setup.project_name);
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ProjectSave) {
        auto arguments = json::parse(stored.arguments_json);
        const std::string path = arguments.value("path", snapshot.setup.project_path);
        if (path.empty()) {
            fail(stored, "unavailable_operation", "This project has never been saved. Give the path to save it to.");
            return stored;
        }
        // The path is bound here, before approval, so the card names exactly
        // the file that will be written and a rename in between cannot move it.
        arguments["resolvedPath"] = path;
        stored.arguments_json     = arguments.dump();
        std::error_code error;
        const bool      replaces = std::filesystem::exists(std::filesystem::u8path(path), error);
        stored.title = std::string(replaces ? "Save the project, replacing " : "Save the project to ") + path;
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::SettingsApplyPatch) {
        auto arguments = json::parse(stored.arguments_json);
        if (arguments["expectedSessionId"] != std::to_string(snapshot.session.value()) || arguments["expectedRevision"] != snapshot.revision) {
            fail(stored, "stale_workspace", "The workspace changed. Read and preview again.", stale_settings_details(arguments, snapshot).dump());
            return stored;
        }
        const auto preview = m_workspace.preview_settings(settings_patch(arguments));
        if (!preview.valid) {
            const auto& issue = preview.issues.at(0);
            fail(stored, issue.code, issue.message, settings_preview_result(preview, snapshot).dump());
            return stored;
        }
        arguments["confirmedChanges"] = json::array();
        for (const auto& change : Workspace::settings_confirmation(preview))
            arguments["confirmedChanges"].push_back({{"key", change.key}, {"before", change.before}, {"after", change.after}});
        stored.arguments_json = arguments.dump();
    }

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

void ToolExecutionCoordinator::clear()
{
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(),
                                      [this](const ToolActivity& activity) { return activity.action_id != m_executing; }),
                       m_activities.end());
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

void ToolExecutionCoordinator::forget_terminal_activities(const std::vector<std::string>& message_ids)
{
    const auto belongs_to_chat = [&](const ToolActivity& activity) {
        return std::find(message_ids.begin(), message_ids.end(), activity.correlation_id) != message_ids.end();
    };
    for (const auto& activity : m_activities)
        if (belongs_to_chat(activity) && !tool_state_terminal(activity.state))
            throw std::logic_error("Cannot forget an in-flight tool activity");
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(), [&](const ToolActivity& activity) {
        return belongs_to_chat(activity);
    }), m_activities.end());
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
    // Approval and the execution tick are separate GUI events. Recheck the
    // last content/history change here; selection-only changes still cannot
    // redirect a proposal pinned to an object ID and do not invalidate it.
    // Every mutation is rechecked, not only the ones that waited for a card: a
    // computation-only action skips approval, not staleness.
    if (activity.action_class != ActionClass::ReadOnly &&
        (m_workspace.snapshot().session.value() != activity.session ||
         m_last_invalidating_revision > activity.expected_revision)) {
        fail(activity, "stale_revision", "The project changed before this action could execute. Propose it again.");
        return;
    }
    // Whatever this action changes is the Agent's, in the change log.
    const Workspace::IWorkspace::AgentEdit agent_edit(m_workspace);
    m_executing = activity.action_id;
    struct Finished { std::string& id; ~Finished() { id.clear(); } } finished{m_executing};
    const ToolDefinition* definition = m_registry.find(activity.tool);
    if (definition == nullptr) {
        fail(activity, "unknown_tool", "This build has no tool named \"" + activity.tool + "\".");
        return;
    }

    if (definition->handler == ToolHandler::DuplicateObject) {
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

    if (definition->handler == ToolHandler::ImportModel) {
        const json arguments = json::parse(activity.arguments_json, nullptr, false);
        const std::string attachment_id =
            arguments.is_object() ? arguments.value("attachmentId", std::string()) : std::string();
        if (attachment_id.empty()) {
            fail(activity, "invalid_arguments", "The import action does not identify an attachment.");
            return;
        }
        const std::string path = m_attachment_path_resolver ? m_attachment_path_resolver(attachment_id) : std::string();
        if (path.empty()) {
            fail(activity, "unavailable_operation", "The attached model is no longer available to import.");
            return;
        }
        const Workspace::CommandResult result = m_workspace.import_model(path);
        if (!result.succeeded()) {
            fail(activity, workspace_error_code(result.error), result.message);
            return;
        }
        const Workspace::WorkspaceSnapshot after = m_workspace.snapshot();
        json result_json{{"revision", after.revision}, {"imported", true}};
        if (result.object_id)
            result_json["newObjectId"] = std::to_string(result.object_id->value());
        activity.result_json = result_json.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::InspectSelection) {
        const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
        activity.result_json = selection_inspection(snapshot).dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::WorkspaceInspect) {
        const auto arguments = json::parse(activity.arguments_json);
        InspectSections sections;
        if (arguments.contains("sections")) {
            const auto asked = [&arguments](const char* name) {
                const auto& sections = arguments["sections"];
                return std::any_of(sections.begin(), sections.end(),
                                   [name](const json& value) { return value == name; });
            };
            sections = {asked("summary"), asked("intent"), asked("plan"), asked("slicing"), asked("history"), asked("printer"), asked("project")};
        }
        if ((sections.intent || sections.plan || sections.printer) && m_product_state == nullptr) {
            fail(activity, "unavailable_operation", "This build cannot read the intent or the plan.");
            return;
        }
        json result = workspace_inspection(m_workspace.snapshot(), sections);
        if (sections.intent) result["intent"] = intent_section_result(m_product_state->print_intent());
        if (sections.plan) result["plan"] = plan_section_result(m_product_state->plan());
        if (sections.slicing) result["slicing"] = slicing_section_result(m_workspace.snapshot(), m_slice_handle);
        if (sections.printer) {
            const auto configured = m_workspace.configured_printer();
            const auto devices    = m_workspace.printers();
            const auto facts      = m_product_state->has_printer_facts() ?
                                        m_product_state->printer_facts(printer_fact_key(configured, devices)) :
                                        std::vector<Workspace::PrinterFact>{};
            result["printer"] = printer_section_result(configured, devices, facts);
        }
        if (sections.project) result["project"] = project_section_result(m_workspace.snapshot(), m_workspace.project_details());
        if (sections.history) result["history"] = history_section_result(m_workspace.snapshot(), m_workspace.history());
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::HistoryRestore) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto restored  = m_workspace.restore_history(std::stoull(arguments["stepId"].get<std::string>()),
                                                           arguments["point"] == "before" ? Workspace::HistoryPoint::Before :
                                                                                            Workspace::HistoryPoint::After);
        if (!restored.succeeded()) {
            fail(activity, workspace_error_code(restored.error), restored.message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        // What Orca's history does not carry, so the agent can say it stayed.
        json kept = json::array();
        if (!snapshot.preset_deltas.empty())
            kept.push_back(std::to_string(snapshot.preset_deltas.size()) + " edited process settings");
        if (m_product_state != nullptr && !m_product_state->print_intent().empty())
            kept.push_back("the print intent");
        if (m_product_state != nullptr && !m_product_state->plan().headline.empty())
            kept.push_back("the plan");
        activity.result_json = json{{"history", history_section_result(snapshot, m_workspace.history())},
                                    {"notReversed", {{"items", std::move(kept)}, {"truncated", false}}},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ProjectOpen) {
        const auto arguments = json::parse(activity.arguments_json);
        Workspace::ProjectOpenRequest request;
        request.path                  = arguments.value("path", "");
        request.new_project           = arguments.value("new", false);
        request.load_project_settings = arguments.value("loadProjectSettings", "project") == "project";
        const std::string units       = arguments.value("unitConversion", "keep");
        request.units                 = units == "inches"        ? Workspace::UnitChoice::Inches :
                                        units == "convertIfTiny" ? Workspace::UnitChoice::ConvertIfTiny :
                                                                   Workspace::UnitChoice::Keep;
        request.scale_oversized       = arguments.value("oversized", "keep") == "scaleToFit";
        request.discard_unsaved       = arguments.value("unsavedWork", "") == "discard";
        // The conversation belongs to the project being closed; it goes to
        // its recovery state before the project does.
        if (m_product_state != nullptr)
            m_product_state->flush_to_project();
        std::vector<Workspace::LoadDecision> decisions;
        const std::string action_id = activity.action_id;
        const auto opened = m_workspace.open_project(request, decisions);
        // Replacing the project clears the other activities, which may move
        // this one: find it again rather than trust the reference.
        ToolActivity& current = *find_mutable(action_id);
        json asked = json::array();
        for (const auto& decision : decisions)
            if (asked.size() < 32) asked.push_back({{"question", decision.question}, {"answer", decision.answer}});
        if (!opened.succeeded()) {
            fail(current, workspace_error_code(opened.error), opened.message, json{{"decisions", asked}}.dump());
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        std::size_t objects = 0;
        for (const auto& plate : snapshot.plates) objects += plate.objects.size();
        current.result_json = json{{"projectName", snapshot.setup.project_name}, {"path", snapshot.setup.project_path},
                                    {"plateCount", snapshot.plates.size()}, {"objectCount", objects},
                                    {"decisions", std::move(asked)},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        current.state = ToolState::Succeeded;
        notify(current);
        return;
    }

    if (definition->handler == ToolHandler::ProjectSave) {
        const std::string path = json::parse(activity.arguments_json).at("resolvedPath").get<std::string>();
        // The conversation and the rest of the product state travel inside the
        // project file, so what is still pending goes to disk first.
        if (m_product_state != nullptr)
            m_product_state->flush_to_project();
        const auto saved = m_workspace.save_project(path);
        if (!saved.succeeded()) {
            fail(activity, workspace_error_code(saved.error), saved.message);
            return;
        }
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"path", path}, {"saved", true}, {"projectDirty", snapshot.setup.project_dirty},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrinterSetupPreview) {
        const auto preview = m_workspace.preview_printer_setup(setup_request(json::parse(activity.arguments_json)));
        const auto devices = m_workspace.printers();
        json mismatches = json::array();
        if (preview.valid) {
            const auto facts = m_product_state != nullptr && m_product_state->has_printer_facts() ?
                                   m_product_state->printer_facts(printer_fact_key(preview.resulting, devices)) :
                                   std::vector<Workspace::PrinterFact>{};
            mismatches = printer_section_result(preview.resulting, devices, facts)["mismatches"];
        }
        activity.result_json = printer_setup_preview_result(preview, mismatches, m_workspace.snapshot()).dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrinterSetup) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::PrinterSetupRequest request = setup_request(arguments);
        Workspace::PrinterSetupPreview applied;
        if (!request.empty()) {
            if (setup_outcome(m_workspace.preview_printer_setup(request)) != arguments.at("confirmedSetup")) {
                fail(activity, "stale_workspace", "The presets changed since this setup was proposed. Preview it again.");
                return;
            }
            const auto result = m_workspace.apply_printer_setup(request, applied);
            if (!result.succeeded()) {
                fail(activity, workspace_error_code(result.error), result.message);
                return;
            }
        } else {
            applied.resulting = m_workspace.configured_printer();
        }
        const auto devices = m_workspace.printers();
        const std::string key = printer_fact_key(applied.resulting, devices);
        if (arguments.contains("confirmFacts")) {
            std::vector<Workspace::FactConfirmation> confirmations;
            for (const auto& fact : arguments["confirmFacts"])
                confirmations.push_back({fact["fact"].get<std::string>(), fact["value"].get<std::string>(),
                                         std::chrono::hours(fact.value("hours", std::uint64_t(24)))});
            m_product_state->confirm_printer_facts(key, confirmations);
        }
        const auto facts = m_product_state != nullptr && m_product_state->has_printer_facts() ?
                               m_product_state->printer_facts(key) : std::vector<Workspace::PrinterFact>{};
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"printer", printer_section_result(m_workspace.configured_printer(), devices, facts)},
                                    {"processPreset", m_workspace.current_process_preset()},
                                    {"substituted", setup_substitutions_result(applied.substitutions)},
                                    {"discarded", setup_edits_result(applied.unsaved_edits)},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrinterList) {
        const auto snapshot = m_workspace.snapshot();
        bool       truncated = false;
        json       items     = json::array();
        for (const Workspace::PrinterDevice& device : m_workspace.printers()) {
            if (items.size() == 25) { truncated = true; break; }
            items.push_back(printer_device_result(device));
        }
        activity.result_json = json{{"items", std::move(items)}, {"truncated", truncated},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PresetsList) {
        const auto arguments = json::parse(activity.arguments_json);
        const std::string& kind = arguments.at("kind").get_ref<const std::string&>();
        Workspace::PresetQuery query;
        query.kind = kind == "printer" ? Workspace::PresetKind::Printer :
                     kind == "filament" ? Workspace::PresetKind::Filament : Workspace::PresetKind::Process;
        query.text            = arguments.value("query", "");
        query.compatible_only = arguments.value("compatibleOnly", true);
        query.limit           = arguments.value("limit", std::size_t(25));
        query.cursor          = arguments.value("cursor", "");
        activity.result_json  = presets_list_result(m_workspace.list_presets(query), m_workspace.snapshot()).dump();
        activity.state        = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SliceReportRead) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        // The active plate is what "check this print" means when the caller
        // names none, and the name promises the current one.
        std::optional<Workspace::PlateId> plate = snapshot.active_plate;
        if (arguments.contains("plateId"))
            plate = Workspace::PlateId(snapshot.session, std::stoull(arguments["plateId"].get<std::string>()));
        if (!plate) {
            fail(activity, "unavailable_operation", "This project has no plate to report on.");
            return;
        }
        SliceReportSections sections;
        if (arguments.contains("sections")) {
            const auto asked = [&arguments](const char* name) {
                const auto& sections = arguments["sections"];
                return std::any_of(sections.begin(), sections.end(),
                                   [name](const json& value) { return value == name; });
            };
            sections = {asked("summary"), asked("findings"), asked("material")};
        }
        activity.result_json = slice_report_result(m_workspace.slice_report(*plate), *plate, snapshot, sections).dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SliceStart) {
        const auto arguments = json::parse(activity.arguments_json);
        std::optional<Workspace::PlateId> plate;
        if (arguments.contains("plateId"))
            plate = Workspace::PlateId(Workspace::ProjectSessionId(activity.session),
                                       std::stoull(arguments["plateId"].get<std::string>()));
        const auto started = m_workspace.start_slice(plate, arguments.value("preempt", false));
        if (!started.succeeded()) {
            fail(activity, started.error == Workspace::WorkspaceError::UnavailableOperation ? "unavailable_operation" :
                           started.error == Workspace::WorkspaceError::StaleId ? "stale_id" : "invalid_id",
                 started.message);
            return;
        }
        // The run is Orca's, and it outlives this call: the handle is how the
        // caller finds it again in the slicing section, which is where the
        // result appears when the slicer is done.
        m_slice_handle       = activity.action_id;
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"handle", activity.action_id}, {"started", true},
                                    {"slicing", slicing_section_result(snapshot, m_slice_handle)},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::IntentUpdate || definition->handler == ToolHandler::PlanSet) {
        if (m_product_state == nullptr) {
            fail(activity, "unavailable_operation", "This build cannot record the intent or the plan.");
            return;
        }
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        json       result{{"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision},
                          // Neither record is in Orca's undo stack: say so where
                          // every other mutation says it.
                          {"projectUndo", false}};
        if (definition->handler == ToolHandler::IntentUpdate) {
            std::vector<IntentField> fields;
            for (const auto& field : arguments.at("fields")) {
                IntentField written;
                written.field    = field.at("field").get<std::string>();
                written.value    = field.value("value", "");
                written.question = field.value("question", "");
                // The card showed the interpreted answer and the user approved
                // it, so an answer is confirmed unless the agent says it only
                // assumed it.
                written.provenance = field.value("assumed", false) || written.value.empty() ?
                    Provenance::AgentInferred : Provenance::UserConfirmed;
                fields.push_back(std::move(written));
            }
            result["intent"] = intent_section_result(m_product_state->set_print_intent(fields));
        } else {
            PlanRecord plan;
            plan.headline = arguments.at("headline").get<std::string>();
            for (const auto& decision : arguments.value("decisions", json::array()))
                plan.decisions.push_back({decision.at("topic").get<std::string>(),
                                          decision.at("statement").get<std::string>(),
                                          decision.value("confidence", ""), decision.value("alternative", "")});
            for (const auto& line : arguments.value("assumptions", json::array()))
                plan.assumptions.push_back(line.get<std::string>());
            for (const auto& line : arguments.value("risks", json::array()))
                plan.risks.push_back(line.get<std::string>());
            result["plan"] = plan_section_result(m_product_state->set_plan(std::move(plan)));
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsSearch) {
        const auto args = json::parse(activity.arguments_json);
        const auto result = m_workspace.search_settings({args.at("query").get<std::string>(), args.value("limit", std::size_t(10)), args.value("cursor", "")});
        if (result.error) {
            fail(activity, result.error->code, result.error->message, setting_issue_result(*result.error).dump());
            return;
        }
        activity.result_json = settings_search_result(result, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsGet) {
        const auto args = json::parse(activity.arguments_json);
        const auto result = m_workspace.read_settings(args.at("keys").get<std::vector<std::string>>());
        if (result.error) {
            fail(activity, result.error->code, result.error->message, setting_issue_result(*result.error).dump());
            return;
        }
        activity.result_json = settings_read_result(result, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsPreviewPatch) {
        const auto result = m_workspace.preview_settings(settings_patch(json::parse(activity.arguments_json)));
        if (!result.issues.empty() && result.issues.front().code == "workspace_unavailable") {
            fail(activity, result.issues.front().code, result.issues.front().message);
            return;
        }
        activity.result_json = settings_preview_result(result, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsApplyPatch) {
        const auto args = json::parse(activity.arguments_json);
        const auto before = m_workspace.snapshot();
        if (args.at("expectedSessionId") != std::to_string(before.session.value()) || args.at("expectedRevision") != before.revision) {
            fail(activity, "stale_workspace", "The workspace changed. Read and preview again.", stale_settings_details(args, before).dump());
            return;
        }
        std::vector<Workspace::SettingChange> confirmed;
        for (const auto& change : args.at("confirmedChanges"))
            confirmed.push_back({change.at("key"), change.at("before"), change.at("after")});
        Workspace::SettingsPreview applied;
        const auto result = m_workspace.apply_settings(settings_patch(args), confirmed, applied);
        if (result.error == WorkspaceError::StaleSettings) {
            fail(activity, "stale_workspace", result.message, stale_settings_details(args, m_workspace.snapshot()).dump());
            return;
        }
        if (result.error == WorkspaceError::InvalidSettings) {
            const auto& issue = applied.issues.at(0);
            fail(activity, issue.code, issue.message, settings_preview_result(applied, m_workspace.snapshot()).dump());
            return;
        }
        if (!result.succeeded() && result.error != WorkspaceError::NoChange) {
            fail(activity, workspace_error_code(result.error), result.message);
            return;
        }
        activity.result_json = settings_apply_result(applied, m_workspace.snapshot(), result.succeeded()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::RecordBuild || definition->handler == ToolHandler::RecordExportCopy ||
        definition->handler == ToolHandler::RecordPhysicalPrint) {
        if (!m_extension_executor) {
            fail(activity, "execution_failed", "The registered tool executor is unavailable.");
            return;
        }
        ExtensionResult result = m_extension_executor(definition->handler, activity);
        if (result.handled) {
            if (result.error) {
                fail(activity, std::move(result.error->code), std::move(result.error->message));
                return;
            }
            activity.result_json = std::move(result.result_json);
            activity.state       = ToolState::Succeeded;
            notify(activity);
            return;
        }
        fail(activity, "execution_failed", "The registered tool executor did not handle the request.");
        return;
    }

    fail(activity, "execution_failed", "The registered tool has no executable handler.");
}

void ToolExecutionCoordinator::fail(ToolActivity& activity, std::string code, std::string message, std::string details_json)
{
    activity.state = ToolState::Failed;
    activity.error = ToolError{std::move(code), std::move(message), std::move(details_json)};
    notify(activity);
}

void ToolExecutionCoordinator::notify(const ToolActivity& activity)
{
    std::shared_ptr<ObserverState> state = m_observers;
    std::vector<std::uint64_t> ids;
    ids.reserve(state->observers.size());
    for (const auto& observer : state->observers)
        ids.push_back(observer.first);

    // Look each observer up immediately before invocation so callbacks may
    // safely unsubscribe themselves or another observer during dispatch.
    for (const std::uint64_t id : ids) {
        if (!state->alive)
            break;
        const auto observer = state->observers.find(id);
        if (observer == state->observers.end())
            continue;
        ActivityCallback callback = observer->second;
        callback(activity);
    }
}

void ToolExecutionCoordinator::invalidate_pending(const Workspace::WorkspaceChanged& change)
{
    // Pending proposals fail eagerly. Approved/running proposals recheck this
    // revision at execution, without invalidating a command on its own event.
    if ((change.reasons & kInvalidatingReasons) == WorkspaceChangeReasons::None)
        return;
    m_last_invalidating_revision = change.revision;
    for (ToolActivity& activity : m_activities) {
        if (activity.state != ToolState::Pending)
            continue;
        fail(activity, "stale_revision", "The project changed after this action was proposed. Ask the Agent again.");
    }
}

} // namespace Slic3r::GUI::JusPrin::Agent
