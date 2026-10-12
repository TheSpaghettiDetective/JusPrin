#pragma once

// The state Plater holds for its project-state seam. Kept in this fork-owned
// header (not in Plater.cpp) so the seam's priv-free member functions can live
// in PlaterProjectState.cpp while Plater.cpp still sees the complete type for
// construction and destruction of its unique_ptr member.

#include "ProjectState.hpp"

#include <wx/event.h>

#include <optional>
#include <utility>

namespace Slic3r {
class SlicingProcessCompletedEvent;
}

namespace Slic3r::GUI {

// OrcaSlicer defines the event that ends a slice in Plater.cpp and declares
// it in no header, so nothing outside that file could observe it, and the
// event is the only place the reason for a failed slice exists. Plater.cpp
// includes this header (through the ProjectState.hpp shim) ahead of that
// definition, which gives the event external linkage without a line changed
// in an OrcaSlicer file. If upstream renames or moves the event, this stops
// compiling or linking; it cannot go quietly wrong. PrintIssueMonitor is the
// one listener.
wxDECLARE_EVENT(EVT_PROCESS_COMPLETED, Slic3r::SlicingProcessCompletedEvent);

class PlaterProjectState
{
public:
    ProjectStateObserverHub              observers;
    std::optional<std::pair<bool, bool>> last_history_availability;
    bool                                 ready{false};
};

} // namespace Slic3r::GUI
