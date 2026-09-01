#pragma once

// The state Plater holds for its project-state seam. Kept in this fork-owned
// header (not in Plater.cpp) so the seam's priv-free member functions can live
// in PlaterProjectState.cpp while Plater.cpp still sees the complete type for
// construction and destruction of its unique_ptr member.

#include "ProjectState.hpp"

#include <optional>
#include <utility>

namespace Slic3r::GUI {

class PlaterProjectState
{
public:
    ProjectStateObserverHub              observers;
    std::optional<std::pair<bool, bool>> last_history_availability;
    bool                                 ready{false};
};

} // namespace Slic3r::GUI
