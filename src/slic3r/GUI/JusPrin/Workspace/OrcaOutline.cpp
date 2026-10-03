// The Plates pane's read model and the commands that move its selection.
// Every fact is read from the Orca owner that holds it -- plate membership
// from PartPlateList, copy and volume flags from the Model, selection from
// Selection -- and every command ends in the path Orca's own UI takes, so
// the pane adds no state of its own and no undo path.

#include "OrcaWorkspaceAdapter.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

#include <wx/glcanvas.h>
#include <wx/thread.h>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

// Raises a canvas event on the GUI thread and returns once its handler has run.
// GLCanvas3D::post_event queues the event, which leaves the state a command was
// meant to change unchanged when the command returns.
void raise_now(GLCanvas3D& canvas, SimpleEvent event)
{
    wxGLCanvas* window = canvas.get_wxglcanvas();
    event.SetEventObject(window);
    window->GetEventHandler()->ProcessEvent(event);
}

// The object list acts on its own selection. When the canvas already held the
// selection a command asks for, Plater::select_object returns early, so the list
// can still be pointing at the plate it last showed. Bringing it up to date is
// what the canvas's own object-select event does.
ObjectList& synchronized_object_list()
{
    ObjectList* list = wxGetApp().obj_list();
    list->update_selections();
    return *list;
}

// The printable switches of every copy of an object, in order.
std::vector<bool> printable_flags(const ModelObject& object)
{
    std::vector<bool> flags;
    for (const ModelInstance* instance : object.instances)
        flags.push_back(instance->printable);
    return flags;
}

VolumeRole role_of(const ModelVolume& volume)
{
    if (volume.is_modifier())
        return VolumeRole::Modifier;
    if (volume.is_negative_volume())
        return VolumeRole::NegativePart;
    if (volume.is_support_enforcer())
        return VolumeRole::SupportEnforcer;
    if (volume.is_support_blocker())
        return VolumeRole::SupportBlocker;
    return VolumeRole::Part;
}

MeshProblem mesh_problem_of(const TriangleMeshStats& stats, int repaired_errors)
{
    MeshProblem problem;
    problem.open_edges      = stats.manifold() ? 0 : static_cast<std::size_t>(stats.open_edges);
    problem.repaired_errors = stats.repaired() ? static_cast<std::size_t>(repaired_errors) : 0;
    return problem;
}

// Orca stores a filament override as a 1-based extruder, with 0 meaning "the
// default". Only a positive value is an override.
std::optional<int> extruder_override(const ModelConfig& config)
{
    if (!config.has("extruder"))
        return std::nullopt;
    const int extruder = config.opt_int("extruder");
    return extruder > 0 ? std::optional<int>(extruder) : std::nullopt;
}

ObjectCustomization customization_of(const ModelObject& object)
{
    ObjectCustomization customization;
    customization.support_painting     = object.is_fdm_support_painted();
    customization.seam_painting        = object.is_seam_painted();
    customization.color_painting       = object.is_mm_painted();
    customization.fuzzy_skin_painting  = object.is_fuzzy_skin_painted();
    customization.variable_layer_height = object.has_custom_layering();
    // The filament override is shown as a filament assignment, not as a
    // customization of the object.
    customization.setting_overrides = object.config.size() - (extruder_override(object.config) ? 1 : 0);
    return customization;
}

} // namespace

ProjectOutline OrcaWorkspaceAdapter::outline() const
{
    wxASSERT(wxIsMainThread());
    ProjectOutline result;
    result.session = m_session;
    result.can_add_plate = m_plater.can_add_plate();
    if (const PresetBundle* presets = wxGetApp().preset_bundle; presets != nullptr)
        result.filament_count = std::max<std::size_t>(1, presets->filament_presets.size());
    if (const PresetBundle* presets = wxGetApp().preset_bundle; presets != nullptr)
        if (const auto* colours = presets->project_config.option<ConfigOptionStrings>("filament_colour"))
            result.filament_colours = colours->values;

    PartPlateList& plate_list = m_plater.get_partplate_list();
    const int      active     = plate_list.get_curr_plate_index();
    for (int index = 0; index < plate_list.get_plate_count(); ++index) {
        PartPlate* plate = plate_list.get_plate(index);
        OutlinePlate projected;
        projected.id              = PlateId(m_session, plate->id().id);
        projected.name            = plate->get_plate_name().empty() ? "Plate " + std::to_string(index + 1) : plate->get_plate_name();
        projected.active          = index == active;
        projected.sliced          = plate->is_slice_result_valid();
        projected.locked          = plate->is_locked();
        // The test Orca's own plate icon uses to show that a plate carries settings
        // of its own.
        projected.custom_settings = plate->get_bed_type() != BedType::btDefault || plate->get_print_seq() != PrintSequence::ByDefault ||
                                    !plate->get_first_layer_print_sequence().empty() ||
                                    !plate->get_other_layers_print_sequence().empty() || plate->has_spiral_mode_config();
        result.plates.push_back(std::move(projected));
    }

    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const ModelObject& object = *objects[object_index];
        OutlineObject      projected;
        projected.id        = ObjectId(m_session, object.id().id);
        projected.name      = object.name;
        projected.printable = object.printable;
        projected.extruder  = extruder_override(object.config);
        projected.mesh      = mesh_problem_of(object.get_object_stl_stats(), object.get_repaired_errors_count());
        projected.customization = customization_of(object);

        for (std::size_t instance_index = 0; instance_index < object.instances.size(); ++instance_index) {
            OutlineCopy copy;
            copy.id        = InstanceId(m_session, object.instances[instance_index]->id().id);
            copy.printable = object.instances[instance_index]->printable;
            for (int plate = 0; plate < plate_list.get_plate_count(); ++plate)
                if (plate_list.get_plate(plate)->contain_instance(static_cast<int>(object_index), static_cast<int>(instance_index))) {
                    copy.plate = result.plates[static_cast<std::size_t>(plate)].id;
                    break;
                }
            projected.copies.push_back(copy);
        }

        for (std::size_t volume_index = 0; volume_index < object.volumes.size(); ++volume_index) {
            const ModelVolume& volume = *object.volumes[volume_index];
            OutlineVolume      part;
            part.id       = VolumeId(m_session, volume.id().id);
            part.name     = volume.name;
            part.role     = role_of(volume);
            part.mesh     = mesh_problem_of(volume.mesh().stats(), volume.get_repaired_errors_count());
            part.extruder = extruder_override(volume.config);
            projected.volumes.push_back(std::move(part));
        }
        result.objects.push_back(std::move(projected));
    }

    // Below the object level the selection is a set of GL volumes; map each
    // back to the model item it stands for.
    const Selection& selection = m_plater.canvas3D()->get_selection();
    if (!selection.is_empty()) {
        std::set<InstanceId> copies;
        std::set<VolumeId>   volumes;
        for (const unsigned int gl_index : selection.get_volume_idxs()) {
            const GLVolume* gl = selection.get_volume(gl_index);
            if (gl == nullptr || gl->object_idx() < 0 || gl->object_idx() >= static_cast<int>(objects.size()))
                continue;
            const ModelObject& object = *objects[gl->object_idx()];
            if (selection.is_instance_mode() || selection.is_single_full_instance() || selection.is_multiple_full_instance()) {
                if (gl->instance_idx() >= 0 && gl->instance_idx() < static_cast<int>(object.instances.size()))
                    copies.emplace(m_session, object.instances[gl->instance_idx()]->id().id);
            } else if (gl->volume_idx() >= 0 && gl->volume_idx() < static_cast<int>(object.volumes.size())) {
                volumes.emplace(m_session, object.volumes[gl->volume_idx()]->id().id);
            }
        }
        result.selected_copies.assign(copies.begin(), copies.end());
        result.selected_volumes.assign(volumes.begin(), volumes.end());
    }
    return result;
}

CommandResult OrcaWorkspaceAdapter::select_plate(PlateId id)
{
    wxASSERT(wxIsMainThread());
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Plate ID is invalid");
    if (id.session() != m_session)
        return CommandResult::failure(WorkspaceError::StaleId, "Plate ID belongs to an earlier project session");

    PartPlateList& plates = m_plater.get_partplate_list();
    for (int index = 0; index < plates.get_plate_count(); ++index) {
        if (plates.get_plate(index)->id().id != id.value())
            continue;
        if (index == plates.get_curr_plate_index())
            return CommandResult::failure(WorkspaceError::NoChange, "Plate is already active");
        // The call a click on a plate in Orca's own plate list makes (action 0
        // of the hover id); it takes no undo snapshot and returns 0 on success.
        if (m_plater.select_plate_by_hover_id(index * static_cast<int>(PartPlate::GRABBER_COUNT)) != 0)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Plate could not be selected");
        return CommandResult::success();
    }
    return CommandResult::failure(WorkspaceError::MissingObject, "Plate does not exist in the current project session");
}

namespace {

// The plate's index in Orca's list, which every plate command is addressed by.
int plate_index_of(PartPlateList& plates, std::uint64_t raw_id)
{
    for (int index = 0; index < plates.get_plate_count(); ++index)
        if (plates.get_plate(index)->id().id == raw_id)
            return index;
    return -1;
}

// The plate's slot in the action id Orca's plate icons raise: the plate index
// times GRABBER_COUNT, plus one of these.
unsigned int hover_action_of(PlateAction action)
{
    switch (action) {
    case PlateAction::Delete: return 1;
    case PlateAction::AutoOrient: return 2;
    case PlateAction::Arrange: return 3;
    case PlateAction::ToggleLock: return 4;
    case PlateAction::Settings: return 5;
    case PlateAction::Rename: return PartPlate::PLATE_NAME_HOVER_ID;
    case PlateAction::MoveToFront: return 7;
    case PlateAction::FilamentGrouping: return PartPlate::PLATE_FILAMENT_MAP_ID;
    }
    throw std::logic_error("Unknown plate action");
}

} // namespace

std::vector<PlateAction> OrcaWorkspaceAdapter::plate_actions(PlateId id) const
{
    wxASSERT(wxIsMainThread());
    std::vector<PlateAction> actions;
    if (!id || id.session() != m_session)
        return actions;
    PartPlateList& plates = m_plater.get_partplate_list();
    const int      index  = plate_index_of(plates, id.value());
    if (index < 0)
        return actions;

    actions = {PlateAction::Rename, PlateAction::Arrange, PlateAction::AutoOrient, PlateAction::ToggleLock,
               PlateAction::Settings};
    if (index > 0)
        actions.push_back(PlateAction::MoveToFront);
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets != nullptr && presets->is_bbl_vendor() && presets->get_printer_extruder_count() == 2)
        actions.push_back(PlateAction::FilamentGrouping);
    if (m_plater.can_delete_plate())
        actions.push_back(PlateAction::Delete);
    return actions;
}

CommandResult OrcaWorkspaceAdapter::run_plate_action(PlateId id, PlateAction action)
{
    wxASSERT(wxIsMainThread());
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Plate ID is invalid");
    if (id.session() != m_session)
        return CommandResult::failure(WorkspaceError::StaleId, "Plate ID belongs to an earlier project session");
    PartPlateList& plates = m_plater.get_partplate_list();
    const int      index  = plate_index_of(plates, id.value());
    if (index < 0)
        return CommandResult::failure(WorkspaceError::MissingObject, "Plate does not exist in the current project session");
    const std::vector<PlateAction> offered = plate_actions(id);
    if (std::find(offered.begin(), offered.end(), action) == offered.end())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "That action does not apply to this plate");
    const int hover = index * static_cast<int>(PartPlate::GRABBER_COUNT) + static_cast<int>(hover_action_of(action));
    if (m_plater.select_plate_by_hover_id(hover) != 0)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The plate action could not run");
    return CommandResult::success();
}

std::vector<ObjectAction> OrcaWorkspaceAdapter::object_actions(ObjectId id) const
{
    wxASSERT(wxIsMainThread());
    std::vector<ObjectAction> actions;
    const auto object = resolve(id);
    if (!object)
        return actions;
    const ModelObject& model = *m_plater.model().objects[object->index];
    actions = {ObjectAction::Quantity, ObjectAction::TogglePrintable};
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets != nullptr && presets->filament_presets.size() > 1)
        actions.push_back(ObjectAction::Filament);
    // OrcaSlicer's own conditions for its Repair and Split commands.
    const TriangleMeshStats stats = model.get_object_stl_stats();
    if (model.get_repaired_errors_count() > 0 || !stats.manifold())
        actions.push_back(ObjectAction::Repair);
    if (!model.volumes.empty() && (model.volumes.size() > 1 || model.volumes.front()->is_splittable()))
        actions.push_back(ObjectAction::Split);
    if (model.volumes.size() == 1 && model.volumes.front()->is_splittable())
        actions.push_back(ObjectAction::SplitToParts);
    return actions;
}

CommandResult OrcaWorkspaceAdapter::run_object_action(ObjectId id, ObjectAction action, int slot)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);
    const std::vector<ObjectAction> offered = object_actions(id);
    if (std::find(offered.begin(), offered.end(), action) == offered.end())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "That action does not apply to this object");
    if (action == ObjectAction::Filament) {
        const PresetBundle* presets = wxGetApp().preset_bundle;
        if (slot < 1 || presets == nullptr || slot > static_cast<int>(presets->filament_presets.size()))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "That filament slot does not exist");
    }

    // Orca's object commands act on its selection, as its context menu does.
    if (!m_plater.select_object(object->index))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be selected");
    ObjectList& list = synchronized_object_list();
    const ModelObject& model_object = *m_plater.model().objects[object->index];
    const std::vector<bool> flags_before = printable_flags(model_object);
    switch (action) {
    case ObjectAction::Quantity:
        if (!m_plater.can_increase_instances())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer can't change the quantity of this object now");
        m_plater.set_number_of_copies();
        break;
    case ObjectAction::TogglePrintable:
        list.toggle_printable_state();
        // The list ignores a selection that is not an object or a copy and says
        // nothing, so the effect is what tells whether it ran.
        if (printable_flags(model_object) == flags_before)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not change the printable switch");
        // The object list changes the switch without telling anyone who is
        // not looking at the list.
        publish_change(WorkspaceChangeReasons::Contents);
        break;
    case ObjectAction::Filament:
        list.set_extruder_for_selected_items(slot);
        publish_change(WorkspaceChangeReasons::Contents);
        break;
    case ObjectAction::Repair:
        if (!m_plater.can_fix_through_cgal())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer can't repair this object now");
        list.fix_through_cgal();
        break;
    case ObjectAction::Split:
        if (!m_plater.can_split(true))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer can't split this object");
        m_plater.split_object();
        break;
    case ObjectAction::SplitToParts:
        if (!m_plater.can_split(false))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer can't split this object into parts");
        m_plater.split_volume();
        break;
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::toggle_copy_printable(InstanceId id)
{
    wxASSERT(wxIsMainThread());
    const CommandResult selected = select_copy(id);
    if (!selected.succeeded() && selected.error != WorkspaceError::NoChange)
        return selected;
    ObjectList& list = synchronized_object_list();
    ModelInstance* instance = nullptr;
    for (ModelObject* candidate : m_plater.model().objects)
        for (ModelInstance* item : candidate->instances)
            if (item->id().id == id.value())
                instance = item;
    if (instance == nullptr)
        return CommandResult::failure(WorkspaceError::MissingObject, "Copy does not exist in the current project session");
    const bool before = instance->printable;
    list.toggle_printable_state();
    if (instance->printable == before)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not change the printable switch");
    publish_change(WorkspaceChangeReasons::Contents);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::open_customization(ObjectId id, CustomizationTool tool)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);
    if (!m_plater.select_object(object->index))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be selected");
    GLCanvas3D& canvas = *m_plater.canvas3D();
    if (tool == CustomizationTool::VariableLayerHeight) {
        // The toolbar's layer-height button, which toggles the editor.
        raise_now(canvas, SimpleEvent(EVT_GLTOOLBAR_LAYERSEDITING));
        return CommandResult::success();
    }
    GLGizmosManager::EType type = GLGizmosManager::Undefined;
    switch (tool) {
    case CustomizationTool::SupportPainting: type = GLGizmosManager::FdmSupports; break;
    case CustomizationTool::SeamPainting: type = GLGizmosManager::Seam; break;
    case CustomizationTool::ColorPainting: type = GLGizmosManager::MmSegmentation; break;
    case CustomizationTool::FuzzySkinPainting: type = GLGizmosManager::FuzzySkin; break;
    case CustomizationTool::VariableLayerHeight: break;
    }
    // open_gizmo closes the tool when it is already the open one, so a tool
    // that is open is left open rather than closed by asking for it.
    GLGizmosManager& gizmos = canvas.get_gizmos_manager();
    if (gizmos.get_current_type() == type)
        return CommandResult::failure(WorkspaceError::NoChange, "That tool is already open");
    if (!gizmos.open_gizmo(type))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not open that tool");
    canvas.set_as_dirty();
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::add_plate()
{
    wxASSERT(wxIsMainThread());
    if (!m_plater.can_add_plate())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "No more plates can be added");
    const int before = m_plater.get_partplate_list().get_plate_count();
    // The event the toolbar's Add plate button posts: it takes the undo
    // snapshot, creates and selects the plate, and updates the scene.
    raise_now(*m_plater.canvas3D(), SimpleEvent(EVT_GLTOOLBAR_ADD_PLATE));
    if (m_plater.get_partplate_list().get_plate_count() == before)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Plate could not be added");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::select_copy(InstanceId id)
{
    wxASSERT(wxIsMainThread());
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Copy ID is invalid");
    if (id.session() != m_session)
        return CommandResult::failure(WorkspaceError::StaleId, "Copy ID belongs to an earlier project session");

    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index)
        for (std::size_t instance_index = 0; instance_index < objects[object_index]->instances.size(); ++instance_index) {
            if (objects[object_index]->instances[instance_index]->id().id != id.value())
                continue;
            Selection& selection = m_plater.canvas3D()->get_selection();
            if (selection.is_single_full_instance() && selection.get_object_idx() == static_cast<int>(object_index) &&
                selection.get_instance_idx() == static_cast<int>(instance_index))
                return CommandResult::failure(WorkspaceError::NoChange, "Copy is already selected");
            selection.add_instance(static_cast<unsigned int>(object_index), static_cast<unsigned int>(instance_index));
            if (!selection.is_single_full_instance() || selection.get_object_idx() != static_cast<int>(object_index) ||
                selection.get_instance_idx() != static_cast<int>(instance_index))
                return CommandResult::failure(WorkspaceError::UnavailableOperation, "Copy could not be selected");
            // The event the canvas posts after a pick: Orca's object list,
            // toolbar state and project-state observers all follow from it.
            raise_now(*m_plater.canvas3D(), SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
            return CommandResult::success();
        }
    return CommandResult::failure(WorkspaceError::MissingObject, "Copy does not exist in the current project session");
}

CommandResult OrcaWorkspaceAdapter::select_volume(VolumeId id)
{
    wxASSERT(wxIsMainThread());
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Volume ID is invalid");
    if (id.session() != m_session)
        return CommandResult::failure(WorkspaceError::StaleId, "Volume ID belongs to an earlier project session");

    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const ModelObject& object = *objects[object_index];
        for (std::size_t volume_index = 0; volume_index < object.volumes.size(); ++volume_index) {
            if (object.volumes[volume_index]->id().id != id.value())
                continue;
            // Orca lists volumes only for an object with more than one, and
            // selects them only on a copy that sits on a plate.
            if (!object.is_multiparts())
                return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                              "Orca does not select a volume of a single-part object");
            Selection& selection = m_plater.canvas3D()->get_selection();
            if (selection.is_single_volume_or_modifier() && selection.get_object_idx() == static_cast<int>(object_index) &&
                selection.get_first_volume()->volume_idx() == static_cast<int>(volume_index))
                return CommandResult::failure(WorkspaceError::NoChange, "Volume is already selected");
            const int  instance  = selection.get_object_idx() == static_cast<int>(object_index) && selection.get_instance_idx() >= 0 ?
                                       selection.get_instance_idx() : 0;
            selection.add_volume(static_cast<unsigned int>(object_index), static_cast<unsigned int>(volume_index), instance);
            if (!selection.is_single_volume_or_modifier() || selection.get_object_idx() != static_cast<int>(object_index))
                return CommandResult::failure(WorkspaceError::UnavailableOperation, "Volume could not be selected");
            raise_now(*m_plater.canvas3D(), SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
            return CommandResult::success();
        }
    }
    return CommandResult::failure(WorkspaceError::MissingObject, "Volume does not exist in the current project session");
}

} // namespace Slic3r::GUI::JusPrin::Workspace
