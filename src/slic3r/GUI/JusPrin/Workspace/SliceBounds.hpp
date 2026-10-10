#pragma once

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

namespace Slic3r::GUI::JusPrin::Workspace {

// The box around everything a slice extrudes: the moves OrcaSlicer's own
// "path goes beyond the plate" check considers. Undefined for a slice that
// extrudes nothing.
inline BoundingBoxf3 extrusion_bounds(const GCodeProcessorResult& result)
{
    BoundingBoxf3 paths;
    for (const GCodeProcessorResult::MoveVertex& move : result.moves)
        if (move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width != 0.f && move.height != 0.f)
            paths.merge(move.position.cast<double>());
    return paths;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
