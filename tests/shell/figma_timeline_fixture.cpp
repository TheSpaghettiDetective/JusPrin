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

    auto& build = fixture.first_build;
    build.project_id = document.project_id();
    build.conversation_id = fixture.conversation_id;
    build.after_message_id = support_answer;
    build.plate_name = "Plate 1";
    build.printer = "Bambu X1C";
    build.material = "Gray PETG";
    build.manufacturing_input_hash = Agent::sha256_hex("backpack-hook-first-input");
    build.output_hash = Agent::sha256_hex("backpack-hook-first-gcode");
    build.slicer_version = "JusPrin";
    build.configuration_provenance = "0.20 mm · 5 walls · 35% gyroid";
    build.sent_at = "2026-10-01T13:04:00Z";
    build.delivery_confirmed_at = "2026-10-01T13:06:00Z";
    build.delivery_location = "the printer's SD";
    build.statistics = {9360.0, 1842.5, 68.0, 1.12, 181};
    const std::string first_build_id = document.add_build(build, "2026-10-01T13:04:00Z");

    Agent::PhysicalPrintRecord failed_print;
    failed_print.started_at = "2026-10-01T13:09:00Z";
    failed_print.ended_at = "2026-10-01T14:13:00Z";
    failed_print.outcome = "failed";
    failed_print.failure = "Layer shift reported near layer 62.";
    failed_print.build_id = first_build_id;
    failed_print.project_id = build.project_id;
    failed_print.conversation_id = fixture.conversation_id;
    failed_print.after_message_id = support_answer;
    failed_print.plate_name = build.plate_name;
    failed_print.printer = build.printer;
    failed_print.material = build.material;
    failed_print.manufacturing_input_hash = build.manufacturing_input_hash;
    failed_print.output_hash = build.output_hash;
    failed_print.gcode_hash = build.output_hash;
    failed_print.stopped_percent = 41;
    failed_print.statistics = build.statistics;
    document.add_physical_print(failed_print, failed_print.ended_at);

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

void finish_figma_timeline(Agent::ProjectStateDocument& document, const FigmaTimelineFixture& fixture)
{
    Agent::BuildRecord second_build = fixture.first_build;
    second_build.after_message_id = document.last_item_id(fixture.conversation_id);
    second_build.manufacturing_input_hash = Agent::sha256_hex("backpack-hook-revised-input");
    second_build.output_hash = Agent::sha256_hex("backpack-hook-revised-gcode");
    second_build.sent_at.clear();
    second_build.delivery_confirmed_at.clear();
    second_build.delivery_location.clear();
    second_build.configuration_provenance = "0.20 mm · 5 walls · 45% gyroid · brim";
    second_build.statistics = {9240.0, 1860.0, 69.0, 1.15, 181};
    const std::string second_build_id = document.add_build(second_build, "2026-10-01T14:22:00Z");
    Agent::ExportedCopyRecord copy;
    copy.build_id = second_build_id;
    copy.conversation_id = fixture.conversation_id;
    copy.after_message_id = second_build.after_message_id;
    copy.destination = "Desktop/Prints/bracket-v3.gcode";
    copy.expected_output_hash = second_build.output_hash;
    copy.observed_output_hash = second_build.output_hash;
    document.add_exported_copy(copy, "2026-10-01T14:24:00Z");
}

} // namespace JusPrinTest
