#include "DeterministicMockAgent.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;
using Workspace::SelectionStatus;
using Workspace::WorkspaceObject;
using Workspace::WorkspacePlate;
using Workspace::WorkspaceSnapshot;

bool starts_with(const std::string& text, const char* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

// Split into word-sized chunks so streaming is visible and deterministic.
std::vector<std::string> chunk_words(const std::string& text)
{
    std::vector<std::string> chunks;
    std::istringstream       stream(text);
    std::string              word;
    while (stream >> word)
        chunks.emplace_back(chunks.empty() ? word : " " + word);
    if (chunks.empty())
        chunks.emplace_back(text);
    return chunks;
}

const WorkspaceObject* find_object(const WorkspaceSnapshot& context, Workspace::ObjectId id)
{
    for (const WorkspacePlate& plate : context.plates)
        for (const WorkspaceObject& object : plate.objects)
            if (object.id == id)
                return &object;
    return nullptr;
}

std::string describe_workspace(const WorkspaceSnapshot& context)
{
    std::ostringstream reply;
    const std::string  project = context.setup.project_name.empty() ? "an unsaved project" : "\"" + context.setup.project_name + "\"";
    reply << "You are working on " << project;
    if (context.setup.project_dirty)
        reply << " (unsaved changes)";
    reply << ".";

    if (!context.setup.printer_preset.empty()) {
        reply << " The target printer is " << context.setup.printer_preset;
        if (!context.setup.filament_preset.empty())
            reply << " with " << context.setup.filament_preset;
        reply << ".";
    }

    const WorkspacePlate* active = nullptr;
    for (const WorkspacePlate& plate : context.plates)
        if (plate.active)
            active = &plate;
    reply << " The project has " << context.plates.size() << (context.plates.size() == 1 ? " plate" : " plates") << ".";
    if (active != nullptr) {
        reply << " " << active->name << " is active with " << active->objects.size()
              << (active->objects.size() == 1 ? " object" : " objects");
        if (!active->objects.empty()) {
            reply << ":";
            for (std::size_t i = 0; i < active->objects.size(); ++i)
                reply << (i == 0 ? " " : ", ") << active->objects[i].name;
        }
        reply << "." << (active->sliced ? " It has a valid slice result." : " It has not been sliced yet.");
    }

    switch (context.selection_status) {
    case SelectionStatus::None: reply << " Nothing is selected."; break;
    case SelectionStatus::Unsupported: reply << " The current selection is not an object-level selection."; break;
    case SelectionStatus::Objects: {
        reply << " Selected:";
        bool first = true;
        for (Workspace::ObjectId id : context.selected_objects) {
            const WorkspaceObject* object = find_object(context, id);
            reply << (first ? " " : ", ") << (object != nullptr ? object->name : "an object");
            first = false;
        }
        reply << ".";
        break;
    }
    }

    reply << " I can explain any of this in more detail, or duplicate the selected object for you if you ask.";
    return reply.str();
}

bool contains_word(const std::string& text, const char* word)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered.find(word) != std::string::npos;
}

const WorkspaceObject* first_selected_object(const WorkspaceSnapshot& context)
{
    if (context.selection_status != SelectionStatus::Objects || context.selected_objects.empty())
        return nullptr;
    return find_object(context, context.selected_objects.front());
}

ToolRequest duplicate_request(const WorkspaceSnapshot& context, const WorkspaceObject& object)
{
    ToolRequest request;
    request.tool = "duplicate_object";
    request.arguments_json = json{{"sessionId", std::to_string(context.session.value())},
                                  {"objectId", std::to_string(object.id.value())}}
                                 .dump();
    return request;
}

DeterministicMockAgent::Reply select_something_first()
{
    DeterministicMockAgent::Reply reply;
    reply.chunks = chunk_words("Select the object you want duplicated on the canvas first, then ask me again.");
    return reply;
}

std::string kind_phrase(const std::string& kind)
{
    if (kind == "image")
        return "an image";
    if (kind == "svg")
        return "an SVG";
    if (kind == "pdf")
        return "a PDF";
    if (kind == "gcode")
        return "G-code";
    if (kind == "model")
        return "a 3D model";
    if (kind == "text")
        return "a text file";
    return "a file";
}

// A sentence acknowledging what the user attached, proving the Agent received
// the decoded context (and only a native summary for models).
std::string describe_attachments(const std::vector<DeterministicMockAgent::AttachmentContext>& attachments)
{
    if (attachments.empty())
        return {};
    std::ostringstream out;
    out << "You attached ";
    for (std::size_t i = 0; i < attachments.size(); ++i) {
        const auto& a = attachments[i];
        if (i > 0)
            out << (i + 1 == attachments.size() ? " and " : ", ");
        out << a.name << " (" << kind_phrase(a.kind) << ")";
    }
    out << ". ";
    for (const auto& a : attachments)
        if (a.kind == "model" && !a.summary.empty())
            out << a.summary << " ";
    return out.str();
}

} // namespace

DeterministicMockAgent::Reply DeterministicMockAgent::reply_for(const std::string&                  user_text,
                                                                int                                 attempt,
                                                                const Workspace::WorkspaceSnapshot& context,
                                                                const std::vector<AttachmentContext>& attachments)
{
    Reply reply;
    if (starts_with(user_text, "/fail")) {
        reply.chunks = chunk_words("Let me look at the project. Reading the plate");
        reply.error  = AgentError{"mock_failure", "The Agent service reported a deterministic test failure.", true};
        return reply;
    }
    if (starts_with(user_text, "/flaky")) {
        if (attempt <= 1) {
            reply.chunks = chunk_words("Checking the project state");
            reply.error  = AgentError{"mock_flaky", "The Agent service failed once; retrying will succeed.", true};
        } else {
            reply.chunks = chunk_words("That worked on attempt " + std::to_string(attempt) + ". " + describe_workspace(context));
        }
        return reply;
    }
    if (starts_with(user_text, "/slow")) {
        std::string text = "Taking a slow, careful look at everything on the build plate.";
        for (int i = 1; i <= 30; ++i)
            text += " Step " + std::to_string(i) + " of 30 complete.";
        reply.chunks = chunk_words(text);
        return reply;
    }
    if (starts_with(user_text, "/toolfail")) {
        // A deliberately missing target: the coordinator and workspace handle
        // the failure exactly as they would a real one — nothing here is
        // special-cased downstream.
        ToolRequest request;
        request.tool = "duplicate_object";
        request.arguments_json =
            json{{"sessionId", std::to_string(context.session.value())}, {"objectId", "999999999"}}.dump();
        reply.chunks = chunk_words("I will try to duplicate an object that no longer exists so you can see the failure path.");
        reply.tool           = request;
        reply.tool_run_ticks = 3;
        return reply;
    }
    if (starts_with(user_text, "/toolslow")) {
        const WorkspaceObject* object = first_selected_object(context);
        if (object == nullptr)
            return select_something_first();
        reply.chunks = chunk_words("Duplicating " + object->name + " slowly so you can watch the progress or cancel it.");
        reply.tool           = duplicate_request(context, *object);
        reply.tool_run_ticks = 60;
        return reply;
    }
    if (starts_with(user_text, "/inspect")) {
        ToolRequest request;
        request.tool           = "inspect_selection";
        request.arguments_json = "{}";
        reply.chunks = chunk_words("Inspecting the current selection; read-only actions run without approval.");
        reply.tool   = request;
        return reply;
    }
    if (starts_with(user_text, "/build")) {
        ToolRequest request;
        request.tool           = "record_build";
        request.arguments_json = json{{"slicerVersion", "JusPrin deterministic Phase 6"},
                                      {"configurationProvenance", "Active printer, process, filament, plate, objects, instances, and transforms"},
                                      {"printTimeSeconds", 3720.0},
                                      {"filamentMm", 1842.5},
                                      {"materialGrams", 14.7},
                                      {"materialCost", 0.44},
                                      {"layerCount", 124}}
                                         .dump();
        reply.chunks = chunk_words("I can preserve the active plate's revision, settings, slice statistics, and immutable hashes as a build record.");
        reply.tool           = request;
        reply.tool_run_ticks = 2;
        return reply;
    }
    if (starts_with(user_text, "/export")) {
        ToolRequest request;
        request.tool           = "record_export_copy";
        request.arguments_json = json{{"destination", "Phase 6 demo.gcode"}}.dump();
        reply.chunks = chunk_words("I can add a verified external-copy record linked to the latest build.");
        reply.tool           = request;
        reply.tool_run_ticks = 2;
        return reply;
    }
    if (starts_with(user_text, "/print")) {
        ToolRequest request;
        request.tool           = "record_physical_print";
        request.arguments_json = json{{"outcome", "completed"}}.dump();
        reply.chunks = chunk_words("I can add a completed physical-print fact linked to the latest build. This ledger entry will survive project Revert.");
        reply.tool           = request;
        reply.tool_run_ticks = 3;
        return reply;
    }
    // A sent model attachment is imported through Orca's own importer, as an
    // approved manufacturing change. This mirrors what a future MCP agent would
    // propose; the host resolves the opaque attachment ID to a file path.
    for (const AttachmentContext& attachment : attachments) {
        if (!attachment.importable)
            continue;
        ToolRequest request;
        request.tool           = "import_model";
        request.arguments_json = json{{"sessionId", std::to_string(context.session.value())},
                                      {"attachmentId", attachment.id}}
                                     .dump();
        reply.chunks         = chunk_words(describe_attachments(attachments) +
                                   "Approve the import below and I will add it to the project through OrcaSlicer's own "
                                   "importer; you can undo it afterwards.");
        reply.tool           = request;
        return reply;
    }

    if (contains_word(user_text, "duplicate")) {
        const WorkspaceObject* object = first_selected_object(context);
        if (object == nullptr)
            return select_something_first();
        reply.chunks = chunk_words("I can duplicate " + object->name +
                                   " for you. Approve the action below and I will run it through OrcaSlicer's own "
                                   "duplicate command; you can undo it afterwards.");
        reply.tool           = duplicate_request(context, *object);
        reply.tool_run_ticks = 3;
        return reply;
    }
    reply.chunks = chunk_words(describe_attachments(attachments) + describe_workspace(context));
    return reply;
}

bool DeterministicMockAgent::start(const AgentRequest& request)
{
    if (m_busy)
        return false;
    if (request.purpose == AgentRequest::Purpose::ConversationTitle) {
        m_events.emplace_back(AgentEvent::delta("Print setup discussion"));
        m_events.emplace_back(AgentEvent::completed());
        m_busy = true;
        return true;
    }

    std::vector<AttachmentContext> attachments;
    attachments.reserve(request.attachments.size());
    for (const AgentAttachmentContext& source : request.attachments) {
        AttachmentContext target;
        target.id         = source.id;
        target.name       = source.name;
        target.kind       = source.kind;
        target.summary    = source.summary;
        target.importable = source.importable;
        attachments.emplace_back(std::move(target));
    }

    const Reply reply = reply_for(request.user_text, request.attempt, request.workspace, attachments);
    for (const std::string& chunk : reply.chunks)
        m_events.emplace_back(AgentEvent::delta(chunk));
    if (reply.error)
        m_events.emplace_back(AgentEvent::failed(*reply.error));
    else {
        if (reply.tool) {
            AgentToolCall call;
            call.call_id        = "mock-" + request.request_id;
            call.request        = *reply.tool;
            call.await_result   = false;
            call.test_run_ticks = reply.tool_run_ticks;
            m_events.emplace_back(AgentEvent::tool_call(std::move(call)));
        } else {
            m_events.emplace_back(AgentEvent::completed());
        }
    }
    m_busy = true;
    return true;
}

void DeterministicMockAgent::cancel()
{
    m_events.clear();
    m_busy = false;
}

std::optional<AgentEvent> DeterministicMockAgent::poll()
{
    if (m_events.empty())
        return std::nullopt;
    AgentEvent event = std::move(m_events.front());
    m_events.pop_front();
    if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed ||
        (event.kind == AgentEventKind::ToolCall && event.tool && !event.tool->await_result))
        m_busy = false;
    return event;
}

} // namespace Slic3r::GUI::JusPrin::Agent
