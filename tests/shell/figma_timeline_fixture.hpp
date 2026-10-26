#pragma once

#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"

namespace JusPrinTest {

namespace Agent = Slic3r::GUI::JusPrin::Agent;

struct FigmaTimelineFixture
{
    std::string background_conversation_id;
    std::string conversation_id;
};

FigmaTimelineFixture seed_figma_timeline_start(Agent::ProjectStateDocument& document);

// A print as the app writes one: after a Sent event it ran from.
std::string add_print(Agent::ProjectStateDocument& document, Agent::PhysicalPrintRecord record,
                      const std::string& timestamp);

} // namespace JusPrinTest
