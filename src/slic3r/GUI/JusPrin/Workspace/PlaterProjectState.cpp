// Definitions for the parts of Plater's project-state seam that do not need
// Plater::priv. Keeping them in this fork-owned translation unit keeps the
// fork's insertion footprint inside the upstream-owned Plater.cpp small; the
// priv-dependent parts of the seam remain there in one grouped block.

#include "PlaterProjectState.hpp"

#include "slic3r/GUI/Plater.hpp"

#include <utility>

namespace Slic3r { namespace GUI {

ProjectStateSubscription Plater::subscribe_project_state(ProjectStateChangedCallback callback)
{
    return m_project_state->observers.subscribe(std::move(callback));
}

ProjectStateTransaction Plater::project_state_transaction()
{
    return m_project_state->observers.transaction();
}

std::uint64_t Plater::project_state_session() const
{
    return m_project_state->observers.project_session();
}

void Plater::notify_project_state_changed(ProjectStateChangeReason reasons, bool project_replaced)
{
    if (!m_project_state->ready)
        return;

    const std::pair<bool, bool> history_availability{can_undo_project(), can_redo_project()};
    if (!m_project_state->last_history_availability ||
        *m_project_state->last_history_availability != history_availability)
        reasons |= ProjectStateChangeReason::History;
    m_project_state->last_history_availability = history_availability;
    m_project_state->observers.publish(reasons, project_replaced);
}

}} // namespace Slic3r::GUI
