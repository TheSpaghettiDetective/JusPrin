#pragma once

// Slices the active plate on JusPrin's own decision, so Prepare's time and
// material estimates and the slice's warnings follow the project without a
// Slice press. There is no preference for it.
//
// OrcaSlicer's "Auto slice after changes" preference is not used. It reacts
// only to settings changes and only while Orca's Preview tab is shown
// (Plater::priv::schedule_auto_reslice_if_needed), and the shell never shows
// that tab, so inside the shell it does nothing whatever its value.
//
// A slice starts once the project has been unchanged for the quiet period and
// nothing is in the way (AutoSlicePolicy.hpp). An Agent turn holds it: a turn
// makes many edits in a row, and its end is when the result is wanted. It
// starts through the same toolbar event the header's Slice posts, so the run
// is OrcaSlicer's ordinary one; an edit stops it as it stops any other.
//
// A run started here is quiet: the chat gets no entry for it, and a slice
// asked for by the person or the Agent takes it over (running() turns false).
//
// GUI thread only.

#include "AutoSlicePolicy.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace Slic3r {
class SlicingProcessCompletedEvent;
namespace GUI {
class Plater;
struct SimpleEvent;
}
} // namespace Slic3r

namespace Slic3r::GUI::JusPrin {

class AutoSlice final
{
public:
    // `allowed` answers whether the shell may slice now: the project is shown
    // in Prepare and no Agent turn is running.
    AutoSlice(Plater& plater, Workspace::IWorkspace& workspace, std::function<bool()> allowed);
    ~AutoSlice();

    AutoSlice(const AutoSlice&)            = delete;
    AutoSlice& operator=(const AutoSlice&) = delete;

    // Called often by the shell's runtime timer; cheap when there is nothing to do.
    void tick();
    // The slice in flight is one this started and nobody has asked for since.
    bool running() const;
    // What the last tick read, for diagnosing a slice that did not start.
    const AutoSliceFacts& last_facts() const { return m_facts; }
    // On by default. Integration scenarios that press Slice themselves turn
    // it off so their run is not raced by this one.
    void set_enabled(bool enabled) { m_enabled = enabled; }

private:
    AutoSliceFacts facts();
    void           on_slice_requested(SimpleEvent& event);
    void           on_process_completed(SlicingProcessCompletedEvent& event);

    using Clock = std::chrono::steady_clock;

    Plater&                         m_plater;
    std::function<bool()>           m_allowed;
    Workspace::WorkspaceSubscription m_subscription;
    bool                            m_enabled{true};
    // Bumped by every edit, and by a slice that stopped being valid, which
    // covers a change that reached the print without a workspace event.
    std::uint64_t                   m_changes{0};
    Clock::time_point               m_last_change{Clock::now()};
    std::uint64_t                   m_plate{0};
    bool                            m_plate_sliced{false};
    // The plate and change count of the last slice anyone asked for.
    std::optional<std::pair<std::uint64_t, std::uint64_t>> m_tried;
    bool                            m_posting{false};
    bool                            m_automatic{false};
    AutoSliceFacts                  m_facts;
};

} // namespace Slic3r::GUI::JusPrin
