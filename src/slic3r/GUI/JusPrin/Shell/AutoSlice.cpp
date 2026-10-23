#include "AutoSlice.hpp"

#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Workspace/PlaterProjectState.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI::JusPrin {

namespace {

// A change the print may be built from. Selection is not one, and a slice
// starting or ending is what this class causes, not a reason to start one.
bool project_change(Workspace::WorkspaceChangeReasons reasons)
{
    using Workspace::WorkspaceChangeReasons;
    using Workspace::has_reason;
    return has_reason(reasons, WorkspaceChangeReasons::Contents) || has_reason(reasons, WorkspaceChangeReasons::History) ||
           has_reason(reasons, WorkspaceChangeReasons::Transform) || has_reason(reasons, WorkspaceChangeReasons::Plates) ||
           has_reason(reasons, WorkspaceChangeReasons::Project) || has_reason(reasons, WorkspaceChangeReasons::Settings);
}

// The move, rotate and scale tools commit each drag as it ends, so a slice
// may start while one is open. Any other tool (painting, cutting, text) is
// held open while the person works in it, so the slice waits for it to close.
bool tool_in_use(GLCanvas3D& canvas)
{
    switch (canvas.get_gizmos_manager().get_current_type()) {
    case GLGizmosManager::Undefined:
    case GLGizmosManager::Move:
    case GLGizmosManager::Rotate:
    case GLGizmosManager::Scale: return canvas.is_dragging() || canvas.is_mouse_dragging();
    default: return true;
    }
}

} // namespace

AutoSlice::AutoSlice(Plater& plater, Workspace::IWorkspace& workspace, std::function<bool()> allowed)
    : m_plater(plater), m_allowed(std::move(allowed))
{
    m_subscription = workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        if (!project_change(change.reasons))
            return;
        ++m_changes;
        m_last_change = Clock::now();
    });
    // Bound after OrcaSlicer's own handlers, so wx calls these first.
    m_plater.Bind(EVT_GLTOOLBAR_SLICE_PLATE, &AutoSlice::on_slice_requested, this);
    m_plater.Bind(EVT_GLTOOLBAR_SLICE_ALL, &AutoSlice::on_slice_requested, this);
    m_plater.Bind(EVT_PROCESS_COMPLETED, &AutoSlice::on_process_completed, this);
}

AutoSlice::~AutoSlice()
{
    m_plater.Unbind(EVT_PROCESS_COMPLETED, &AutoSlice::on_process_completed, this);
    m_plater.Unbind(EVT_GLTOOLBAR_SLICE_ALL, &AutoSlice::on_slice_requested, this);
    m_plater.Unbind(EVT_GLTOOLBAR_SLICE_PLATE, &AutoSlice::on_slice_requested, this);
}

bool AutoSlice::running() const { return m_automatic && m_plater.is_background_process_slicing(); }

AutoSliceFacts AutoSlice::facts()
{
    AutoSliceFacts facts;
    PartPlate* plate = m_plater.get_partplate_list().get_curr_plate();
    if (plate == nullptr)
        return facts;

    const std::uint64_t plate_id = plate->id().id;
    const bool          sliced   = plate->is_slice_result_valid();
    if (plate_id != m_plate) {
        // Arriving on a plate is not an edit, but it waits the quiet period
        // too, so paging through plates does not start a slice on each.
        m_plate       = plate_id;
        m_last_change = Clock::now();
    } else if (m_plate_sliced && !sliced) {
        ++m_changes;
        m_last_change = Clock::now();
    }
    m_plate_sliced = sliced;

    GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    facts.allowed      = m_enabled && m_allowed();
    facts.busy = m_plater.is_background_process_slicing() || !m_plater.get_ui_job_worker().is_idle() ||
                 m_plater.is_background_process_update_scheduled() || canvas == nullptr || !canvas->is_initialized() ||
                 tool_in_use(*canvas);
    facts.sliced    = sliced;
    facts.tried     = m_tried == std::make_pair(plate_id, m_changes);
    facts.quiet     = Clock::now() - m_last_change;
    // The header's own rule for an enabled Slice. PartPlate::can_slice
    // already asks for an object inside the plate; has_printable_instances
    // can lag behind it until the next Plater::update, which the slice runs.
    facts.sliceable = !m_plater.only_gcode_mode() && !m_plater.using_exported_file() && plate->can_slice();
    return facts;
}

void AutoSlice::tick()
{
    if (m_automatic && !m_plater.is_background_process_slicing())
        m_automatic = false; // an edit stopped it, which OrcaSlicer reports with no end event
    m_facts = facts();
    if (!should_auto_slice(m_facts))
        return;

    BOOST_LOG_TRIVIAL(info) << "JusPrin auto slice: plate " << m_plater.get_partplate_list().get_curr_plate_index() + 1;
    m_posting = true;
    m_plater.update(true, true);
    SimpleEvent event(EVT_GLTOOLBAR_SLICE_PLATE);
    m_plater.GetEventHandler()->ProcessEvent(event);
    m_posting   = false;
    m_automatic = m_plater.is_background_process_slicing();
}

void AutoSlice::on_slice_requested(SimpleEvent& event)
{
    event.Skip();
    // Whoever asked, this plate has now been asked for as it stands, so a
    // run that is cancelled or fails is not started again until it changes.
    if (const PartPlate* plate = m_plater.get_partplate_list().get_curr_plate())
        m_tried = std::make_pair(std::uint64_t(plate->id().id), m_changes);
    if (!m_posting)
        m_automatic = false;
}

void AutoSlice::on_process_completed(SlicingProcessCompletedEvent& event)
{
    event.Skip();
    m_automatic = false;
    // A finished slice is recorded here rather than on the next tick: an edit
    // that lands before that tick would otherwise invalidate a slice this
    // class never saw, and the plate would stay "tried" until the next edit.
    if (!event.cancelled() && !event.error())
        if (const PartPlate* plate = m_plater.get_partplate_list().get_curr_plate(); plate != nullptr && plate->id().id == m_plate)
            m_plate_sliced = true;
}

} // namespace Slic3r::GUI::JusPrin
