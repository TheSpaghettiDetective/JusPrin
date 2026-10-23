#pragma once

// When JusPrin slices the active plate on its own. AutoSlice reads these facts
// from OrcaSlicer; the decision is kept apart so it can be tested on its own.

#include <chrono>

namespace Slic3r::GUI::JusPrin {

// How long the project must stay unchanged before a slice starts: long
// enough that a move followed by a turn, or a few settings typed in a row,
// make one slice rather than several started and stopped.
inline constexpr std::chrono::milliseconds kAutoSliceQuietPeriod{2000};

struct AutoSliceFacts
{
    // The shell shows the project in Prepare and no Agent turn is running.
    bool allowed{false};
    // Something is in the way: a slice or an OrcaSlicer job is running, an
    // edit is not yet applied to the print, or a gesture or tool is mid-edit.
    bool busy{false};
    // The active plate is editable and OrcaSlicer's own check accepts it.
    bool sliceable{false};
    // The active plate already holds a valid slice.
    bool sliced{false};
    // A slice of this plate was already asked for since the project last
    // changed: one the person cancelled, or one that failed, is not retried.
    bool tried{false};
    std::chrono::steady_clock::duration quiet{};
};

inline bool should_auto_slice(const AutoSliceFacts& facts)
{
    return facts.allowed && !facts.busy && facts.sliceable && !facts.sliced && !facts.tried &&
           facts.quiet >= kAutoSliceQuietPeriod;
}

} // namespace Slic3r::GUI::JusPrin
