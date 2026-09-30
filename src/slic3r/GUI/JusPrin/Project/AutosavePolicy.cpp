#include "AutosavePolicy.hpp"

namespace Slic3r::GUI::JusPrin::Project {

void AutosavePolicy::project_opened(double now)
{
    m_dirty_full         = false;
    m_full_pending       = false;
    m_dirty_checkpoint   = true;
    m_checkpoint_pending = "opened";
    m_last_edit          = -1.0;
    m_last_checkpoint    = -1.0;
    (void) now;
}

void AutosavePolicy::edited(double now)
{
    m_dirty_full       = true;
    m_dirty_checkpoint = true;
    m_last_edit        = now;
}

void AutosavePolicy::agent_action_starting(double now)
{
    (void) now;
    // The opening checkpoint, if still owed, is the same moment.
    if (m_dirty_checkpoint && m_checkpoint_pending.empty())
        m_checkpoint_pending = "agent";
}

void AutosavePolicy::focus_lost(double now)
{
    (void) now;
    if (m_dirty_full)
        m_full_pending = true;
}

void AutosavePolicy::close_requested(double now)
{
    (void) now;
    if (m_dirty_full)
        m_full_pending = true;
}

AutosavePolicy::Decision AutosavePolicy::decide(double now) const
{
    Decision decision;
    decision.full_save = m_full_pending;
    if (!decision.full_save && m_settings.idle_full_save_seconds > 0.0 && m_dirty_full && m_last_edit >= 0.0 &&
        now - m_last_edit >= m_settings.idle_full_save_seconds)
        decision.full_save = true;

    if (!m_checkpoint_pending.empty()) {
        decision.checkpoint        = true;
        decision.checkpoint_reason = m_checkpoint_pending;
    } else if (m_dirty_checkpoint && m_last_edit >= 0.0 && now - m_last_edit >= m_settings.settle_seconds &&
               (m_last_checkpoint < 0.0 || now - m_last_checkpoint >= m_settings.editing_checkpoint_interval)) {
        decision.checkpoint        = true;
        decision.checkpoint_reason = "editing";
    }
    return decision;
}

void AutosavePolicy::full_saved(double now)
{
    (void) now;
    m_dirty_full   = false;
    m_full_pending = false;
}

void AutosavePolicy::checkpointed(double now)
{
    m_dirty_checkpoint = false;
    m_checkpoint_pending.clear();
    m_last_checkpoint = now;
}

} // namespace Slic3r::GUI::JusPrin::Project
