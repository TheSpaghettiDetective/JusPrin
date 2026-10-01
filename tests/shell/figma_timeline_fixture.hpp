#pragma once

#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"

namespace JusPrinTest {

namespace Agent = Slic3r::GUI::JusPrin::Agent;

struct FigmaTimelineFixture
{
    std::string background_conversation_id;
    std::string conversation_id;
    Agent::BuildRecord first_build;
};

FigmaTimelineFixture seed_figma_timeline_start(Agent::ProjectStateDocument& document);
void finish_figma_timeline(Agent::ProjectStateDocument& document, const FigmaTimelineFixture& fixture);

} // namespace JusPrinTest
