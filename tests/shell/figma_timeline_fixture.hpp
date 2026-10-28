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

// A print as the app writes one: a G-code of its own, sent to the printer,
// then the print the printer reported, finished when `finished_at` is set.
struct PrintFixture
{
    std::string plate_name;
    std::string printer;
    std::string material;
    std::string started_at;
    std::string finished_at;
    double      material_grams{0.0};
    double      material_cost{0.0};
};
std::string add_print(Agent::ProjectStateDocument& document, const PrintFixture& print, const std::string& timestamp);

} // namespace JusPrinTest
