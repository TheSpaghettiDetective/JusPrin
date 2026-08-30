#include "DeterministicMockAgent.hpp"

#include <sstream>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

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

    reply << " I can explain any of this in more detail; changing the project arrives in a later JusPrin release.";
    return reply.str();
}

} // namespace

DeterministicMockAgent::Reply DeterministicMockAgent::reply_for(const std::string&                  user_text,
                                                                int                                 attempt,
                                                                const Workspace::WorkspaceSnapshot& context)
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
    reply.chunks = chunk_words(describe_workspace(context));
    return reply;
}

} // namespace Slic3r::GUI::JusPrin::Agent
