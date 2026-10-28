#include "figma_timeline_fixture.hpp"

#include <stdexcept>
#include <utility>

namespace JusPrinTest {

FigmaTimelineFixture seed_figma_timeline_start(Agent::ProjectStateDocument& document)
{
    FigmaTimelineFixture fixture;
    fixture.background_conversation_id = document.active_conversation_id();
    fixture.conversation_id = document.create_conversation("First print", "2026-10-01T12:53:00Z");
    if (!document.set_setup_intent(fixture.conversation_id, "Strong backpack hook with accurate screw holes"))
        throw std::runtime_error("Could not prepare the First print conversation");

    const auto message = [&](Agent::MessageRole role, const std::string& words,
                             const std::string& time, const std::string& reply_to = {}) {
        Agent::ConversationMessage entry;
        entry.id = document.allocate_message_id();
        entry.role = role;
        entry.state = Agent::MessageState::Complete;
        entry.text = words;
        entry.in_reply_to = reply_to;
        document.append_message(fixture.conversation_id, entry, time);
        return entry.id;
    };
    const std::string request = message(Agent::MessageRole::User,
        "This will hold a backpack. Make it strong, but keep the screw holes accurate.",
        "2026-10-01T12:54:00Z");
    message(Agent::MessageRole::Assistant,
        "I laid it on its side so the layers run through the hook, and thickened the walls around both screw holes.",
        "2026-10-01T12:55:00Z", request);
    const auto setup_change = [&](const char* kind, const char* label, const char* from = "", const char* to = "") {
        Agent::ChangeEntry change;
        change.kind = kind;
        change.actor = "agent";
        change.label = label;
        change.from = from;
        change.to = to;
        change.preset = "Strong";
        document.add_change(std::move(change), "2026-10-01T12:56:00Z");
    };
    setup_change("step", "Laid flat");
    setup_change("setting", "Wall loops", "2", "5");
    setup_change("setting", "Sparse infill density", "15%", "35%");
    setup_change("setting", "Sparse infill pattern", "grid", "gyroid");
    setup_change("setting", "Top shell layers", "3", "5");
    setup_change("setting", "Outer wall speed", "200 mm/s", "120 mm/s");
    const std::string support_question = message(Agent::MessageRole::User,
        "Will it need supports?", "2026-10-01T13:02:00Z");
    const std::string support_answer = message(Agent::MessageRole::Assistant,
        "Only beneath the hook tip. They won't touch the wall-facing surface.",
        "2026-10-01T13:03:00Z", support_question);

    // The G-code went to the printer after the answer: the chat draws its
    // one-line marker there (canvas main-05).
    Agent::GcodeRecord gcode;
    gcode.hash                     = Agent::sha256_hex("backpack-hook-first-gcode");
    gcode.plate_name               = "Plate 1";
    gcode.printer                  = "Bambu Lab X1 Carbon 0.4 nozzle";
    gcode.material                 = "Gray PETG";
    gcode.manufacturing_input_hash = Agent::sha256_hex("backpack-hook-first-input");
    gcode.statistics               = {9360.0, 1842.5, 68.0, 1.12, 181};
    Agent::GcodeDestination send;
    send.kind            = Agent::GcodeDestination::Kind::Send;
    send.printer_name    = "Bambu X1C";
    send.printer_id      = "fixture-x1c";
    send.transport       = "bambu_print";
    send.start_requested = true;
    send.delivered_at    = "2026-10-01T13:06:00Z";
    document.add_gcode_destination(gcode, send, "2026-10-01T13:06:00Z");

    const std::string failure_report = message(Agent::MessageRole::User,
        "It shifted off the plate about a third of the way up.", "2026-10-01T14:14:00Z");
    message(Agent::MessageRole::Assistant,
        "That's adhesion, not strength. I added a brim, slowed the first layers and ran the plate hotter.",
        "2026-10-01T14:15:00Z", failure_report);
    const auto setting = [&](const char* label, const char* from, const char* to) {
        Agent::ChangeEntry change;
        change.kind = "setting";
        change.actor = "agent";
        change.label = label;
        change.from = from;
        change.to = to;
        change.preset = "0.20 mm Standard";
        document.add_change(std::move(change), "2026-10-01T14:16:00Z");
    };
    setting("Brim", "none", "5 mm");
    setting("First-layer speed", "40 mm/s", "18 mm/s");
    setting("Plate temp", "60 °C", "70 °C");
    setting("Layer height", "0.20 mm", "0.20 mm");
    setting("First-layer line width", "0.42 mm", "0.48 mm");
    setting("First-layer acceleration", "500 mm/s²", "300 mm/s²");
    setting("Brim gap", "0.10 mm", "0 mm");
    setting("First-layer flow", "100%", "105%");
    setting("Initial layer fan", "40%", "0%");
    setting("First-layer travel speed", "100 mm/s", "70 mm/s");
    setting("Bed adhesion type", "none", "outer brim");
    setting("Elephant foot compensation", "0.15 mm", "0.10 mm");
    return fixture;
}

std::string add_print(Agent::ProjectStateDocument& document, const PrintFixture& print, const std::string& timestamp)
{
    static int gcodes = 0;
    Agent::GcodeRecord gcode;
    gcode.hash                      = Agent::sha256_hex("fixture-gcode-" + std::to_string(++gcodes));
    gcode.plate_name                = print.plate_name;
    gcode.printer                   = print.printer;
    gcode.material                  = print.material;
    gcode.statistics.material_grams = print.material_grams;
    gcode.statistics.material_cost  = print.material_cost;
    Agent::GcodeDestination send;
    send.kind            = Agent::GcodeDestination::Kind::Send;
    send.printer_name    = print.printer;
    send.transport       = "bambu_print";
    send.start_requested = true;
    Agent::GcodePrint printed;
    printed.destination_id = document.add_gcode_destination(gcode, send, timestamp);
    printed.printer_name   = print.printer;
    printed.started_at     = print.started_at;
    const std::string id   = document.add_gcode_print(gcode.hash, printed, timestamp);
    if (!print.finished_at.empty())
        document.finish_gcode_print(gcode.hash, id, print.finished_at);
    return id;
}

} // namespace JusPrinTest
