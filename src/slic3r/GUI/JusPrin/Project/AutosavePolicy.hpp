#pragma once

// When the open project is written, and what kind of write. Two tiers, from
// agent-docs/jusprin/autosave-save-cost-findings.md: any write that carries
// meshes blocks the app for about 2.3 µs per triangle of the largest object
// (0.6 s for a Benchy, 4.5 s for a 2,000,000-triangle scan), so it runs only
// at moments the person is not editing -- focus leaving the app, the project
// closing, an explicit Save -- while OrcaSlicer's own backup timer, which
// blocks for 10 to 80 ms, keeps the crash copy current in between.
//
// Checkpoints, the copies a person can go back to, are taken before the
// Agent changes the project, when a project opens, and during manual editing
// once the edits have settled and enough time has passed.
//
// Pure: the owner feeds it events with a monotonic clock in seconds and asks
// what to do. GUI-free, no OrcaSlicer types.

#include <string>

namespace Slic3r::GUI::JusPrin::Project {

class AutosavePolicy
{
public:
    struct Settings
    {
        // A manual checkpoint waits for this long without an edit.
        double settle_seconds{3.0};
        // At most one manual-editing checkpoint per this interval.
        double editing_checkpoint_interval{15 * 60.0};
        // A full save after this long without an edit; 0 turns it off. Off
        // by default: a save that starts when the person pauses is felt the
        // moment they move the mouse again.
        double idle_full_save_seconds{0.0};
    };

    struct Decision
    {
        bool        full_save{false};
        bool        checkpoint{false};
        std::string checkpoint_reason; // "opened", "agent", "editing"
    };

    AutosavePolicy() = default;
    explicit AutosavePolicy(Settings settings) : m_settings(settings) {}

    // A project opened or was replaced: nothing is owed to the old one, and
    // the new one gets its opening checkpoint.
    void project_opened(double now);
    // The project changed in a way a save would carry.
    void edited(double now);
    // The Agent is about to change the project.
    void agent_action_starting(double now);
    void focus_lost(double now);
    void close_requested(double now);

    Decision decide(double now) const;

    // The owner reports what it did; a failed write reports nothing and the
    // decision stands.
    void full_saved(double now);
    void checkpointed(double now);

    bool dirty_since_full_save() const { return m_dirty_full; }
    bool dirty_since_checkpoint() const { return m_dirty_checkpoint; }

private:
    Settings    m_settings;
    bool        m_dirty_full{false};
    bool        m_dirty_checkpoint{false};
    bool        m_full_pending{false};
    std::string m_checkpoint_pending; // reason, empty when none
    double      m_last_edit{-1.0};
    double      m_last_checkpoint{-1.0};
};

} // namespace Slic3r::GUI::JusPrin::Project
