#pragma once

// Tells fork listeners that OrcaSlicer was asked to slice the current plate
// and started nothing. Plater::reslice, which every Slice press, shortcut,
// Preview switch and tool call reaches, calls it when its validation refused
// the plate (OrcaSlicer has just pushed the notification saying why) or when
// the last run on the plate failed and nothing has changed since; a slice-all
// calls it when it reaches a plate it cannot slice. The validation that runs
// after an ordinary edit does not call it: nobody asked for a slice then.
// Fork-owned; OrcaSlicer reaches it through ProjectState.hpp.
//
// GUI thread only.

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace Slic3r::GUI {

using SliceDeclinedListeners = std::vector<std::pair<const void*, std::function<void()>>>;

inline SliceDeclinedListeners& slice_declined_listeners()
{
    static SliceDeclinedListeners listeners;
    return listeners;
}

// One listener per owner, so one owner going away leaves the others told.
inline void add_slice_declined_listener(const void* owner, std::function<void()> listener)
{
    slice_declined_listeners().emplace_back(owner, std::move(listener));
}

inline void remove_slice_declined_listener(const void* owner)
{
    SliceDeclinedListeners& listeners = slice_declined_listeners();
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                   [owner](const auto& listener) { return listener.first == owner; }),
                    listeners.end());
}

inline void report_slice_declined()
{
    // A copy: a listener may add or remove one.
    const SliceDeclinedListeners listeners = slice_declined_listeners();
    for (const auto& listener : listeners)
        listener.second();
}

} // namespace Slic3r::GUI
