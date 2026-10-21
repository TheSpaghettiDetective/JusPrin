#include "OrcaWorkspaceAdapter.hpp"
#include "SliceBounds.hpp"
#include "SliceRequests.hpp"
#include "PlaterProjectState.hpp"
#include "DialogListeners.hpp"
#include "SkippedConfirmations.hpp"
#include "OrcaSettings.hpp"
#include "SettingDisplay.hpp"
#include "OrcaGeometry.hpp"
#include "HostLocale.hpp"
#include "SliceWarnings.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Measure.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Jobs/ArrangeJob.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Printers/PrinterNames.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Search.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/thread.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <set>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

ObjectTransform transform_of(const ModelInstance& instance)
{
    const Vec3d position = instance.get_offset();
    const Vec3d rotation = instance.get_rotation();
    const Vec3d scale    = instance.get_scaling_factor();
    return {{position.x(), position.y(), position.z()}, {rotation.x(), rotation.y(), rotation.z()}, {scale.x(), scale.y(), scale.z()}};
}

WorkspaceObject project_object(ProjectSessionId session, const ModelObject& object, PartPlate& plate, int object_index)
{
    WorkspaceObject projected;
    projected.id   = ObjectId(session, object.id().id);
    projected.name = object.name;
    projected.instances.reserve(object.instances.size());
    for (std::size_t instance_index = 0; instance_index < object.instances.size(); ++instance_index)
        if (plate.contain_instance(object_index, static_cast<int>(instance_index)))
            projected.instances.emplace_back(transform_of(*object.instances[instance_index]));
    return projected;
}

WorkspaceChangeReasons workspace_reasons(ProjectStateChangeReason reasons)
{
    WorkspaceChangeReasons result = WorkspaceChangeReasons::None;
    const auto contains = [reasons](ProjectStateChangeReason reason) {
        return (static_cast<std::uint32_t>(reasons) & static_cast<std::uint32_t>(reason)) != 0;
    };
    if (contains(ProjectStateChangeReason::Selection))
        result |= WorkspaceChangeReasons::Selection;
    if (contains(ProjectStateChangeReason::Objects))
        result |= WorkspaceChangeReasons::Contents;
    if (contains(ProjectStateChangeReason::History))
        result |= WorkspaceChangeReasons::History;
    if (contains(ProjectStateChangeReason::Transform))
        result |= WorkspaceChangeReasons::Transform;
    if (contains(ProjectStateChangeReason::Plates))
        result |= WorkspaceChangeReasons::Plates;
    if (contains(ProjectStateChangeReason::Project))
        result |= WorkspaceChangeReasons::Project;
    if (contains(ProjectStateChangeReason::Settings))
        result |= WorkspaceChangeReasons::Settings;
    return result;
}

bool blank(const std::string& value)
{
    return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
}

// Time and material for one plate, read from the plate's own G-code result the
// same way Orca's preview and cost readouts do: volume per extruder times that
// filament's density and price. Nothing here is computed by JusPrin, so the
// card can never disagree with the number the slicer shows.
std::optional<SliceEstimate> estimate_of(const GCodeProcessorResult& result)
{
    SliceEstimate estimate;
    const auto& statistics = result.print_statistics;
    const auto& mode = statistics.modes[static_cast<std::size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    estimate.has_time = mode.time > 0.f;
    estimate.print_time_seconds = estimate.has_time ? static_cast<std::uint32_t>(mode.time) : 0u;
    estimate.has_material = !statistics.total_volumes_per_extruder.empty();

    double cost = 0.0;
    bool   every_filament_priced = true;
    for (const auto& [extruder, volume] : statistics.total_volumes_per_extruder) {
        // Keep an independent time estimate, but never present a partial
        // weight or cost as the total for a multi-material plate.
        if (extruder >= result.filament_densities.size() ||
            !std::isfinite(result.filament_densities[extruder]) || result.filament_densities[extruder] <= 0.f) {
            estimate.has_material = false;
            estimate.material_grams = 0.0;
            every_filament_priced = false;
            break;
        }
        const double grams = volume * result.filament_densities[extruder] * 0.001;
        estimate.material_grams += grams;
        // A filament profile with no price reports zero, which is not a price.
        if (extruder < result.filament_costs.size() && result.filament_costs[extruder] > 0.f)
            cost += grams * result.filament_costs[extruder] * 0.001;
        else
            every_filament_priced = false;
    }
    // Money is the cost of the whole print or it is not shown. With one spool
    // priced and another not, a "total" would silently leave material out.
    estimate.has_cost      = every_filament_priced && cost > 0.0;
    estimate.material_cost = estimate.has_cost ? cost : 0.0;
    // A slice with neither trustworthy quantity is not an estimate worth a row.
    if (!estimate.has_time && !estimate.has_material) return std::nullopt;
    return estimate;
}

std::string pure_option_key(const std::string& key)
{
    const std::size_t separator = key.find('#');
    return separator == std::string::npos ? key : key.substr(0, separator);
}

SettingsScope settings_scope(Preset::Type type)
{
    return type == Preset::TYPE_FILAMENT ? SettingsScope::Filament :
           type == Preset::TYPE_PRINTER  ? SettingsScope::Printer : SettingsScope::Process;
}

const char* preset_type_name(Preset::Type type)
{
    return type == Preset::TYPE_FILAMENT ? "filament" : type == Preset::TYPE_PRINTER ? "printer" : "process";
}

// The key OptionsSearcher files an option's group and page under (get_key in
// Search.cpp, which keeps it to itself). If Orca changes it the lookup finds
// nothing and the setup page loses its group captions, which the shell
// harness's --setup-differences-capture run checks for.
std::string index_key(const std::string& option, Preset::Type type)
{
    return std::to_string(int(type)) + ";" + option;
}

// Where a setting sits in Orca's settings tabs. Every option group registers
// its options with the searcher as its tab is built, whatever user mode is in
// force, which get_option's index of the visible options is not. An option
// with one value per extruder is registered as key#0.
Search::GroupAndCategory tab_location(Search::OptionsSearcher& searcher, const std::string& key, Preset::Type type)
{
    Search::GroupAndCategory location = searcher.get_group_and_category(index_key(key, type));
    if (location.group.empty())
        location = searcher.get_group_and_category(index_key(key + "#0", type));
    return location;
}

// Every setting whose edited value differs from its selected preset. Deep
// comparisons may report vector entries as key#N; one row describes the
// setting, so those entries collapse back to their pure key and the formatter
// receives the complete serialized vector.
std::vector<PresetDelta> preset_deltas_of(const PresetCollection& presets, const std::vector<std::string>& dirty,
                                          Preset::Type type, Search::OptionsSearcher& searcher)
{
    std::vector<PresetDelta> result;
    std::set<std::string> dirty_keys;
    for (const std::string& reported : dirty)
        dirty_keys.insert(pure_option_key(reported));
    result.reserve(dirty_keys.size());
    const DynamicPrintConfig& edited = presets.get_edited_preset().config;
    const DynamicPrintConfig& saved  = presets.get_selected_preset().config;
    const SettingsScope scope = settings_scope(type);
    for (const std::string& key : dirty_keys) {
        const ConfigOptionDef* definition = print_config_def.get(key);
        const ConfigOption*    before     = saved.option(key);
        const ConfigOption*    after      = edited.option(key);
        if (definition == nullptr || before == nullptr || after == nullptr) continue;
        const std::string& label = definition->full_label.empty() ? definition->label : definition->full_label;
        const SettingDefinition display_definition = setting_definition(key);
        PresetDelta delta{key, label.empty() ? key : label, before->serialize(), after->serialize(),
                          preset_type_name(type), presets.get_edited_preset().name,
                          display_change(display_definition, before->serialize(), after->serialize())};

        const Search::GroupAndCategory location = tab_location(searcher, key, type);
        delta.page  = into_u8(location.category);
        delta.group = into_u8(location.group);
        result.push_back(std::move(delta));
    }
    const auto& ordered = scope_options(scope);
    const auto rank = [&ordered](const std::string& key) {
        const auto found = std::find(ordered.begin(), ordered.end(), key);
        return found == ordered.end() ? ordered.size() : std::size_t(found - ordered.begin());
    };
    std::stable_sort(result.begin(), result.end(), [&rank](const PresetDelta& left, const PresetDelta& right) {
        return rank(left.key) < rank(right.key);
    });
    return result;
}

// The kind a volume is customized as, in the words the catalog uses.
const char* volume_kind(const ModelVolume& volume)
{
    return volume.is_modifier()         ? "modifier" :
           volume.is_support_blocker()  ? "support blocker" :
           volume.is_support_enforcer() ? "support enforcer" :
           volume.is_negative_volume()  ? "negative part" : "part";
}

// What the All settings panel may do to a customization of this kind: a key
// it sets goes back to the project's value, except a height range's layer
// height and filament, which OrcaSlicer keeps on every range (its own
// "Delete Settings" leaves both); a blocker or an enforcer without a key is
// the volume itself, which goes.
OwnSettingAction own_setting_action(const std::string& kind, const std::string& key)
{
    if (key.empty())
        return kind == "support blocker" || kind == "support enforcer" ? OwnSettingAction::Remove : OwnSettingAction::None;
    if (kind == "variable layer height" || (kind == "height range" && (key == "layer_height" || key == "extruder")))
        return OwnSettingAction::None;
    return OwnSettingAction::UseProjectValue;
}

// Everything one object prints with that the project's settings do not say:
// its own settings, its height ranges', its parts' and modifiers', and the
// customizations with no value to quote (paint, a blocker, a variable-height
// profile). `object_index` is what each override's `object` field is set to.
// Returns whether the object has a variable layer height profile.
bool add_object_customizations(const ModelObject& object, std::size_t object_index, std::vector<SetupLocalOverride>& overrides)
{
    const auto add_local = [&overrides, object_index](const std::string& target, const std::string& kind,
                                                      const DynamicPrintConfig& config) {
        for (const std::string& key : config.keys()) {
            const ConfigOption* option = config.option(key);
            const ConfigOptionDef* definition = print_config_def.get(key);
            if (option == nullptr)
                continue;
            // Orca materializes slot 1 on otherwise clean imported objects.
            // It is the inherited/default filament, not a local override.
            if (key == "extruder") {
                if (option->serialize() != "1")
                    overrides.push_back({target, kind, key, option->serialize(), object_index, "Filament slot", option->serialize(),
                                         own_setting_action(kind, key)});
            } else if (definition != nullptr) {
                const SettingDefinition display_definition = setting_definition(key);
                overrides.push_back({target, kind, key, option->serialize(), object_index, display_definition.label,
                                     display_value(display_definition, option->serialize()), own_setting_action(kind, key)});
            }
        }
    };
    const auto add_artifact = [&overrides, object_index](const std::string& target, const std::string& kind,
                                                         const std::string& key = {}) {
        const ConfigOptionDef* definition = key.empty() ? nullptr : print_config_def.get(key);
        overrides.push_back({target, kind, key, {}, object_index,
                             definition == nullptr ? std::string() :
                                 (definition->full_label.empty() ? definition->label : definition->full_label),
                             {}, own_setting_action(kind, key)});
    };

    bool variable_layer_height = false;
    add_local(object.name, "object", object.config.get());
    // has_custom_layering() is also true for a height range, which may
    // change walls and leave the layer height alone; ranges are listed
    // below with the keys they actually set.
    if (!object.layer_height_profile.empty()) {
        variable_layer_height = true;
        add_artifact(object.name, "variable layer height", "layer_height");
    }
    for (const auto& range : object.layer_config_ranges)
        add_local(object.name, "height range", range.second.get());
    for (const ModelVolume* volume : object.volumes) {
        const std::string target = object.name + " / " + volume->name;
        const char* kind = volume_kind(*volume);
        add_local(target, kind, volume->config.get());
        if (volume->is_support_blocker() || volume->is_support_enforcer())
            add_artifact(target, kind);
    }
    if (object.is_fdm_support_painted())
        add_artifact(object.name, "support painting");
    if (object.is_seam_painted())
        add_artifact(object.name, "seam painting");
    if (object.is_mm_painted())
        add_artifact(object.name, "color painting");
    if (object.is_fuzzy_skin_painted())
        add_artifact(object.name, "fuzzy skin painting");
    return variable_layer_height;
}

// Whether a printable copy of the object sits on the plate.
bool prints_on(const ModelObject& object, std::size_t object_index, PartPlate& plate)
{
    if (!object.printable) return false;
    for (std::size_t copy = 0; copy < object.instances.size(); ++copy)
        if (object.instances[copy]->printable && plate.contain_instance(static_cast<int>(object_index), static_cast<int>(copy)))
            return true;
    return false;
}

AppliedSetup applied_setup_of(ProjectSessionId session, Plater& plater, PartPlate& plate)
{
    AppliedSetup result;
    result.plate = PlateId(session, plate.id().id);
    result.spiral_mode = plate.get_spiral_vase_mode();
    const ModelObjectPtrs& objects = plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const ModelObject& object = *objects[index];
        if (!prints_on(object, index, plate)) continue;
        ++result.printable_objects;
        result.objects.push_back(object.name);
        if (add_object_customizations(object, result.objects.size() - 1, result.local_overrides))
            result.variable_layer_height = true;
    }
    return result;
}

// Every setting a preset holds, in Orca's order, under its tab's page and
// group, with the saved preset's value beside each one that differs. The
// selected preset is read with its unsaved edits, as read_settings reads it;
// any other is read as saved, and then nothing differs.
std::vector<CatalogSetting> catalog_settings(const PresetCollection& collection, const Preset& preset, bool selected,
                                             Preset::Type type, Search::OptionsSearcher& searcher)
{
    const SettingsScope       scope  = settings_scope(type);
    const DynamicPrintConfig& config = selected ? collection.get_edited_preset().config : preset.config;
    const DynamicPrintConfig* saved  = selected ? &collection.get_selected_preset().config : nullptr;
    std::set<std::string> dirty;
    if (selected)
        for (const std::string& reported : collection.current_dirty_options(true))
            dirty.insert(pure_option_key(reported));
    std::vector<CatalogSetting> result;
    for (const std::string& key : scope_options(scope)) {
        if (!has_scope_setting(key, scope) || withheld_setting(key)) continue;
        const ConfigOption* option = config.option(key);
        if (option == nullptr) continue;
        const SettingDefinition definition = setting_definition(key);
        CatalogSetting row;
        row.key   = key;
        row.label = definition.label.empty() ? key : definition.label;
        const Search::GroupAndCategory location = tab_location(searcher, key, type);
        row.page    = into_u8(location.category);
        row.group   = into_u8(location.group);
        row.value   = option->serialize();
        row.display = display_value(definition, row.value);
        if (saved != nullptr && dirty.count(key) > 0)
            if (const ConfigOption* before = saved->option(key); before != nullptr && before->serialize() != row.value) {
                row.preset         = before->serialize();
                row.preset_display = display_value(definition, *row.preset);
            }
        result.push_back(std::move(row));
    }
    return result;
}

// A filament's override of a printer setting (filament_retraction_length and
// the rest of Orca's "Setting Overrides" page) that is not set defers to the
// printer: every entry of the nullable vector is nil. The printer's value is
// the one in force, so the row carries it.
void mark_inherited(std::vector<CatalogSetting>& settings, const DynamicPrintConfig& filament_config,
                    const DynamicPrintConfig& printer_config)
{
    static const std::string prefix = "filament_";
    for (CatalogSetting& row : settings) {
        if (row.key.compare(0, prefix.size(), prefix) != 0) continue;
        const auto* vector = dynamic_cast<const ConfigOptionVectorBase*>(filament_config.option(row.key));
        if (vector == nullptr || vector->size() == 0) continue;
        bool all_nil = true;
        for (std::size_t index = 0; index < vector->size() && all_nil; ++index)
            all_nil = vector->is_nil(index);
        if (!all_nil) continue;
        const std::string   printer_key = row.key.substr(prefix.size());
        const ConfigOption* in_force    = printer_config.option(printer_key);
        if (in_force == nullptr || print_config_def.get(printer_key) == nullptr) continue;
        row.inherited         = true;
        row.inherited_display = display_value(setting_definition(printer_key), in_force->serialize());
    }
}

// The settings a plate sets itself. Orca keeps the plate dialog's own state
// beside them (which combo was on Customize) and the filament grouping, which
// are not settings a person reads.
std::vector<CatalogSetting> plate_own_settings(PartPlate& plate, Search::OptionsSearcher& searcher)
{
    std::vector<CatalogSetting> result;
    const DynamicPrintConfig* config = plate.config();
    if (config == nullptr) return result;
    for (const std::string& key : config->keys()) {
        if (key.size() > 16 && key.compare(key.size() - 16, 16, "_sequence_choice") == 0) continue;
        if (key == "other_layers_print_sequence_nums" || key == "filament_map" || key == "filament_map_mode") continue;
        const ConfigOptionDef* definition = print_config_def.get(key);
        const ConfigOption*    option     = config->option(key);
        if (definition == nullptr || option == nullptr) continue;
        const SettingDefinition display_definition = setting_definition(key);
        CatalogSetting row;
        row.key   = key;
        row.label = display_definition.label.empty() ? key : display_definition.label;
        const Search::GroupAndCategory location = tab_location(searcher, key, Preset::TYPE_PLATE);
        row.page    = into_u8(location.category);
        row.group   = into_u8(location.group);
        row.value   = option->serialize();
        row.display = display_value(display_definition, row.value);
        result.push_back(std::move(row));
    }
    return result;
}

// The settings edited between two readings of one preset. A key's value in
// force is its delta's value, or the preset's own value when it has no delta.
// The change log is read by people, so an edit carries its two values as the
// setup card writes values: "Outer brim only" and "8 mm", not outer_only and 8.
std::vector<WorkspaceEdit> setting_edits(const std::vector<PresetDelta>& before, const std::vector<PresetDelta>& after,
                                         const PresetCollection& collection)
{
    const std::string& preset = collection.get_edited_preset().name;
    const auto edit = [&](const PresetDelta& delta, const std::string& was, const std::string& now) {
        const SettingDefinition definition = setting_definition(delta.key);
        return WorkspaceEdit{EditKind::Setting, EditActor::Person, delta.label, display_value(definition, was),
                             display_value(definition, now), preset, delta.key};
    };
    const auto find = [](const std::vector<PresetDelta>& deltas, const std::string& key) {
        const auto found = std::find_if(deltas.begin(), deltas.end(), [&key](const PresetDelta& delta) { return delta.key == key; });
        return found == deltas.end() ? nullptr : &*found;
    };
    std::vector<WorkspaceEdit> edits;
    for (const PresetDelta& delta : after) {
        const PresetDelta* earlier = find(before, delta.key);
        const std::string& was     = earlier != nullptr ? earlier->value : delta.preset;
        if (was != delta.value)
            edits.push_back(edit(delta, was, delta.value));
    }
    // A delta disappears when the setting is put back to the preset's value,
    // and also when the preset is saved with the value in it -- which changes
    // nothing in force. So the value now in force decides.
    for (const PresetDelta& delta : before)
        if (find(after, delta.key) == nullptr) {
            const ConfigOption* option = collection.get_edited_preset().config.option(delta.key);
            const std::string   now    = option != nullptr ? option->serialize() : delta.preset;
            if (now != delta.value)
                edits.push_back(edit(delta, delta.value, now));
        }
    return edits;
}

// Orca's Discard for the preset a tab edits: nothing asks about it after.
void drop_unsaved_edits(Tab* tab)
{
    if (tab != nullptr && tab->get_presets()->current_is_dirty()) {
        tab->get_presets()->discard_current_changes();
        tab->load_current_preset();
    }
}

std::string plate_label(BedType type)
{
    const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
    if (definition == nullptr || type <= btDefault || int(type) - 1 >= int(definition->enum_labels.size()))
        return {};
    return definition->enum_labels[int(type) - 1];
}

} // namespace

OrcaWorkspaceAdapter::OrcaWorkspaceAdapter(Plater& plater) : m_plater(plater)
{
    wxASSERT(wxIsMainThread());
    m_session = ProjectSessionId(m_plater.project_state_session());
    m_project_subscription = m_plater.subscribe_project_state(
        [this](const ProjectStateChanged& change) { on_project_state_changed(change); });
    // Slicing changes what a plate holds without changing the project, so it
    // raises no ProjectStateChanged and consumers would keep reporting the
    // plate unsliced until something unrelated moved. The header already
    // listens to this event; the workspace has to as well, or an estimate only
    // appears after the next click.
    m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &OrcaWorkspaceAdapter::on_slice_status_changed, this);
    // Why a slice did not succeed is known only as it happens: OrcaSlicer
    // refuses a plate inside the call that asked for the slice, and a failed
    // run's error is in its completion event.
    m_plater.Bind(EVT_PROCESS_COMPLETED, &OrcaWorkspaceAdapter::on_process_completed, this);
    add_slice_declined_listener(this, [this] { on_slice_declined(); });
    m_known_slice_state = current_slice_state();
    remember_current_ids();
    remember_history();
    m_first_model_pending = m_plater.model().objects.empty();
    record_settings_edits(/*report=*/false);
    set_file_load_observer(this);
}

OrcaWorkspaceAdapter::~OrcaWorkspaceAdapter()
{
    wxASSERT(wxIsMainThread());
    set_file_load_observer(nullptr);
    m_open_answers.reset();
    m_active_load.reset();
    remove_slice_declined_listener(this);
    m_plater.Unbind(EVT_PROCESS_COMPLETED, &OrcaWorkspaceAdapter::on_process_completed, this);
    m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &OrcaWorkspaceAdapter::on_slice_status_changed, this);
    m_project_subscription.reset();
}

struct OrcaWorkspaceAdapter::ActiveLoad
{
    std::vector<std::string> files;
    bool                     with_settings{false};
    bool                     settings_applied{false};
    std::size_t              objects_before{0};
    ScopedLeastChangeAnswers answers;
};

namespace {
// A printer's bed as its config states it, for the fit facts of a load report.
BedShape bed_shape_of(const DynamicPrintConfig& config)
{
    BedShape bed;
    if (const auto* area = config.option<ConfigOptionPoints>("printable_area"))
        for (const Vec2d& point : area->values)
            bed.area.push_back({point.x(), point.y()});
    if (const auto* excluded = config.option<ConfigOptionPoints>("bed_exclude_area"))
        for (const Vec2d& point : excluded->values)
            bed.excluded.push_back({point.x(), point.y()});
    if (const auto* height = config.option<ConfigOptionFloat>("printable_height"))
        bed.height = height->value;
    return bed;
}

// What a 3MF states about the printer and filaments it was made for, read
// straight from the archive: Orca's importer, even asked for settings only,
// walks every object's geometry, which on a large file costs the load again.
// An archive this cannot open states nothing here; Orca's own messages about
// the load already say what went wrong with it.
LoadedFileFacts read_3mf_facts(const std::string& path)
{
    LoadedFileFacts facts;
    facts.file = path.substr(path.find_last_of("/\\") + 1);
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_reader(&zip, path))
        return facts;
    // A whole small entry, or the start of a large one.
    const auto read = [&zip](const char* name, std::size_t limit) {
        std::string text;
        const int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
        if (index < 0)
            return text;
        mz_zip_reader_extract_iter_state* entry = mz_zip_reader_extract_iter_new(&zip, static_cast<mz_uint>(index), 0);
        if (entry == nullptr)
            return text;
        text.resize(limit);
        text.resize(mz_zip_reader_extract_iter_read(entry, text.data(), limit));
        mz_zip_reader_extract_iter_free(entry);
        return text;
    };
    facts.application = application_from_model_head(read("3D/3dmodel.model", 16 * 1024));
    if (const std::string settings = read("Metadata/project_settings.config", 4 * 1024 * 1024); !settings.empty())
        read_project_settings(settings, facts);
    else if (const std::string prusa = read("Metadata/Slic3r_PE.config", 4 * 1024 * 1024); !prusa.empty())
        read_prusa_config(prusa, facts);
    close_zip_reader(&zip);
    return facts;
}

std::string load_preset_origin(const Preset* preset)
{
    if (preset == nullptr) return "unknown";
    if (preset->is_project_embedded) return "file_embedded";
    if (preset->is_system) return "installed_system";
    if (preset->is_user()) return "user";
    if (preset->is_default) return "default";
    return "other";
}
} // namespace

LoadSelectedSetup OrcaWorkspaceAdapter::selected_load_setup() const
{
    LoadSelectedSetup result;
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return result;
    const Preset& printer = bundle->printers.get_selected_preset();
    result.printer_preset = printer.name;
    result.printer_origin = load_preset_origin(&printer);
    const Preset& process = bundle->prints.get_selected_preset();
    result.process_preset = process.name;
    result.process_origin = load_preset_origin(&process);
    if (!bundle->filament_presets.empty()) {
        result.filament_preset = bundle->filament_presets.front();
        const Preset* filament = bundle->filaments.find_preset(result.filament_preset, false);
        result.filament_origin = load_preset_origin(filament);
        if (filament != nullptr)
            if (const auto* types = filament->config.option<ConfigOptionStrings>("filament_type");
                types != nullptr && !types->values.empty())
                result.material_type = types->values.front();
    }
    return result;
}

void OrcaWorkspaceAdapter::file_load_started(const std::vector<boost::filesystem::path>& files, LoadStrategy strategy)
{
    wxASSERT(wxIsMainThread());
    // A settings file brings no model, and a silent load is one Orca itself
    // does not treat as the person's (it records neither the folder nor the
    // project's file name): crash recovery reloading JusPrin's own backup,
    // and code loading a file for its own purposes. None of them is a file
    // the person brought in, so Orca's dialogs show as they always have.
    if (!(strategy & LoadStrategy::LoadModel) || (strategy & LoadStrategy::Silence) || m_active_load)
        return;
    flush_file_open_messages();
    if (m_finished_loads.empty() && !m_open_answers) {
        m_session_before_loads = m_plater.project_state_session();
        m_setup_before_loads = selected_load_setup();
    }
    m_active_load = std::make_unique<ActiveLoad>();
    for (const boost::filesystem::path& file : files)
        m_active_load->files.push_back(file.string());
    m_active_load->with_settings  = strategy & LoadStrategy::LoadConfig;
    m_active_load->objects_before = m_plater.model().objects.size();
}

void OrcaWorkspaceAdapter::file_settings_applied()
{
    wxASSERT(wxIsMainThread());
    if (m_active_load)
        m_active_load->settings_applied = true;
}

void OrcaWorkspaceAdapter::file_load_finished()
{
    wxASSERT(wxIsMainThread());
    if (!m_active_load)
        return;
    FileLoadRecord record;
    record.files         = std::move(m_active_load->files);
    record.with_settings = m_active_load->with_settings;
    record.settings_applied = m_active_load->settings_applied;
    record.messages      = m_active_load->answers.take_messages();
    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = m_active_load->objects_before; index < objects.size(); ++index)
        m_arrived_objects.push_back(objects[index]->id().id);
    m_active_load.reset();
    m_finished_loads.push_back(std::move(record));
    // A tool takes its report as soon as its call returns. Anything else is
    // reported once the event loop comes round: a project open replaces the
    // project only after this returns, and one drop can run several loads.
    if (!m_open_answers)
        schedule_load_report();
}

void OrcaWorkspaceAdapter::file_open_started()
{
    wxASSERT(wxIsMainThread());
    if (m_tool_loads != 0)
        return;
    if (m_finished_loads.empty()) {
        m_session_before_loads = m_plater.project_state_session();
        m_setup_before_loads = selected_load_setup();
    }
    m_open_answers = std::make_unique<ScopedLeastChangeAnswers>(/*prefer_cancel=*/true);
}

void OrcaWorkspaceAdapter::file_open_finished()
{
    wxASSERT(wxIsMainThread());
    if (!m_open_answers)
        return;
    flush_file_open_messages();
    m_open_answers.reset();
    schedule_load_report();
}

void OrcaWorkspaceAdapter::flush_file_open_messages()
{
    if (!m_open_answers)
        return;
    std::vector<OrcaMessage> messages = m_open_answers->take_messages();
    if (messages.empty())
        return;
    FileLoadRecord outside;
    outside.messages = std::move(messages);
    m_finished_loads.push_back(std::move(outside));
}

bool OrcaWorkspaceAdapter::file_load_message(const std::string& text, const char* source)
{
    if (!m_active_load && !m_open_answers)
        return false;
    OrcaMessage message;
    message.text = text;
    message.source = source;
    if (m_active_load)
        m_active_load->answers.add_message(std::move(message));
    else
        m_open_answers->add_message(std::move(message));
    return true;
}

void OrcaWorkspaceAdapter::schedule_load_report()
{
    if (m_tool_loads != 0 || m_report_scheduled || m_finished_loads.empty())
        return;
    m_report_scheduled = true;
    wxGetApp().CallAfter([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (alive.lock())
            deliver_load_report();
    });
}

LoadReport OrcaWorkspaceAdapter::take_load_report(bool started_by_agent)
{
    LoadReport report;
    report.started_by_agent = started_by_agent;
    report.loads            = std::move(m_finished_loads);
    report.ui_language      = wxGetApp().app_config->get("language");
    report.selected_setup_before = std::move(m_setup_before_loads);
    report.selected_setup_after  = selected_load_setup();
    // Each bed beside its printer, so an object the selected printer cannot
    // hold can be matched to one the person has.
    std::vector<std::pair<std::string, BedShape>> beds;
    for (const Printers::NamedPrinter& named : Printers::named_printers()) {
        const Preset* preset = wxGetApp().preset_bundle->printers.find_preset(named.name, false);
        LoadSetupPrinter printer{named.name, preset != nullptr ? preset->config.opt_string("printer_model") : "", named.nozzle};
        if (preset != nullptr) {
            beds.emplace_back(named.name, bed_shape_of(preset->config));
            printer.bed = bed_size(beds.back().second);
        }
        report.setup_printers.push_back(std::move(printer));
    }
    const BedShape selected_bed = bed_shape_of(wxGetApp().preset_bundle->printers.get_selected_preset().config);
    report.selected_bed         = bed_size(selected_bed);
    if (DeviceManager* devices = wxGetApp().getDeviceManager())
        if (MachineObject* machine = devices->get_selected_machine(); machine != nullptr &&
            PrinterSetup::has_recent_printer_data(*machine))
            if (const Preset* preset = get_printer_preset(machine))
                report.connected_printer = preset->config.opt_string("printer_model");
    m_finished_loads.clear();
    report.project_opened = m_plater.project_state_session() != m_session_before_loads;
    report.project_path   = into_u8(m_plater.get_project_filename(".3mf"));
    if (report.project_opened)
        report.details = project_details();
    const std::set<std::uint64_t> arrived(m_arrived_objects.begin(), m_arrived_objects.end());
    m_arrived_objects.clear();
    for (ObjectDetails& row : object_details()) {
        if (arrived.count(row.id.value()) == 0)
            continue;
        if (report.objects.size() == kReportedObjectLimit) {
            report.objects_truncated = true;
            break;
        }
        report.objects.push_back(std::move(row));
    }
    const ModelObjectPtrs& model_objects = m_plater.model().objects;
    for (const ObjectDetails& row : report.objects)
        for (std::size_t index = 0; index < model_objects.size(); ++index)
            if (model_objects[index]->id().id == row.id.value())
                report.object_facts.push_back(loaded_object_facts(index, selected_bed, beds));
    for (const FileLoadRecord& load : report.loads)
        for (const std::string& file : load.files)
            if (boost::iends_with(file, ".3mf"))
                if (LoadedFileFacts facts = read_3mf_facts(file); !facts.empty())
                    report.file_facts.push_back(std::move(facts));
    return report;
}

LoadedObjectFacts OrcaWorkspaceAdapter::loaded_object_facts(std::size_t model_index, const BedShape& selected_bed,
                                                            const std::vector<std::pair<std::string, BedShape>>& beds) const
{
    const ModelObject& object = *m_plater.model().objects[model_index];
    PartPlateList&     plates = m_plater.get_partplate_list();
    const int          index  = static_cast<int>(model_index);
    LoadedObjectFacts  facts;
    facts.id = ObjectId(m_session, object.id().id);
    // Where it is: the check analyze_object's fit makes, for every copy.
    for (std::size_t instance = 0; instance < object.instances.size(); ++instance) {
        const int plate = plates.find_instance_belongs(index, static_cast<int>(instance));
        if (plate < 0 || plates.get_plate(plate)->check_outside(index, static_cast<int>(instance))) {
            facts.placed_on_bed = false;
            break;
        }
    }
    // Whether it could stand anywhere on each bed. Its outline is the first
    // copy's convex hull, the shape the plate tests against an excluded area.
    if (!object.instances.empty()) {
        std::vector<BedPoint> footprint;
        for (const Point& point : object.instances.front()->convex_hull_2d().points)
            footprint.push_back({unscale<double>(point.x()), unscale<double>(point.y())});
        const double height = object.instance_bounding_box(0).size().z();
        facts.fits_selected_bed = fits_bed(selected_bed, footprint, height);
        for (const auto& [name, bed] : beds)
            if (fits_bed(bed, footprint, height))
                facts.fits_setup_printers.push_back(name);
    }
    // Orca's mesh check, as object_analyze's mesh reports it.
    const TriangleMeshStats stats = object.get_object_stl_stats();
    for (const ModelVolume* volume : object.volumes)
        if (volume->is_model_part())
            facts.triangles += volume->mesh().facets_count();
    facts.shells          = static_cast<std::size_t>(std::max(stats.number_of_parts, 0));
    facts.open_edges      = static_cast<std::size_t>(std::max(stats.open_edges, 0));
    facts.repaired_errors = static_cast<std::size_t>(stats.repaired_errors.edges_fixed + stats.repaired_errors.degenerate_facets +
                                                     stats.repaired_errors.facets_removed + stats.repaired_errors.facets_reversed +
                                                     stats.repaired_errors.backwards_edges);
    Model single;
    single.add_object(object);
    facts.units_suspicion = single.looks_like_saved_in_meters() ? "meters" :
                            single.looks_like_imperial_units()  ? "inches" : "";
    // What the file already prepared for printing.
    const auto any_volume = [&object](bool (ModelVolume::*painted)() const) {
        return std::any_of(object.volumes.begin(), object.volumes.end(),
                           [painted](const ModelVolume* volume) { return (volume->*painted)(); });
    };
    if (any_volume(&ModelVolume::is_fdm_support_painted)) facts.customizations.push_back("supportPainting");
    if (any_volume(&ModelVolume::is_seam_painted)) facts.customizations.push_back("seamPainting");
    if (any_volume(&ModelVolume::is_mm_painted)) facts.customizations.push_back("colourPainting");
    if (any_volume(&ModelVolume::is_fuzzy_skin_painted)) facts.customizations.push_back("fuzzySkinPainting");
    if (!object.layer_height_profile.empty()) facts.customizations.push_back("variableLayerHeight");
    if (!object.layer_config_ranges.empty()) facts.customizations.push_back("layerHeightRanges");
    return facts;
}

void OrcaWorkspaceAdapter::deliver_load_report()
{
    wxASSERT(wxIsMainThread());
    m_report_scheduled = false;
    if (m_finished_loads.empty())
        return;
    const LoadReport report = take_load_report(/*started_by_agent=*/false);
    if (m_load_report_listener)
        m_load_report_listener(report);
}

void OrcaWorkspaceAdapter::set_load_report_listener(std::function<void(const LoadReport&)> listener)
{
    m_load_report_listener = std::move(listener);
}

OrcaWorkspaceAdapter::SliceState OrcaWorkspaceAdapter::current_slice_state() const
{
    SliceState state;
    // Whether a slice is running is part of what the plate holds, not merely
    // progress: starting one on an already-invalid plate changes neither the
    // valid flag nor the result id, so without it that transition publishes
    // nothing and no consumer ever learns the slice began.
    const bool slicing = m_plater.is_background_process_slicing();
    PartPlateList& plates = m_plater.get_partplate_list();
    for (int index = 0; index < plates.get_plate_count(); ++index) {
        PartPlate* plate = plates.get_plate(index);
        if (plate == nullptr) continue;
        const bool sliced = plate->is_slice_result_valid();
        const std::uint64_t result = sliced && !slicing && plate->get_slice_result() ?
            plate->get_slice_result()->id : 0;
        state.emplace(plate->id().id, PlateSlice{sliced, result, slicing});
    }
    return state;
}

void OrcaWorkspaceAdapter::on_slice_status_changed(wxCommandEvent& event)
{
    event.Skip();
    wxASSERT(wxIsMainThread());
    // This event also fires whenever the native toolbar re-evaluates whether
    // Slice and Print are enabled, which happens on ordinary selection and
    // object changes. Publishing on all of those would advance the revision --
    // and add a history entry -- for nothing. Only a real change in what the
    // plates hold is a workspace change.
    SliceState state = current_slice_state();
    if (state == m_known_slice_state)
        return;
    // A plate that held a slice and no longer does was invalidated by whatever
    // the person just did. Recording it here, at the transition, is the only
    // moment the cause is still known.
    for (const auto& [id, now] : state) {
        const auto before = m_known_slice_state.find(id);
        if (before != m_known_slice_state.end() && before->second.sliced && !now.sliced)
            m_invalidated_by[id] = m_last_change_reason;
        else if (now.sliced) {
            m_invalidated_by.erase(id);
            m_slice_failures.erase(id);
        }
    }
    // One process slices for the whole application, so every plate's entry
    // carries the same flag.
    const auto slicing_in = [](const SliceState& slices) {
        return !slices.empty() && slices.begin()->second.slicing;
    };
    const bool was_slicing = slicing_in(m_known_slice_state), slicing = slicing_in(state);
    m_known_slice_state = std::move(state);
    // A run that starts replaces whatever the plate's last slice failed with;
    // one that ended with an error says why now, OrcaSlicer having finished
    // with the plate.
    if (slicing && !was_slicing)
        if (const PartPlate* current = m_plater.get_partplate_list().get_curr_plate())
            m_slice_failures.erase(current->id().id);
    if (!slicing && m_failed_run_plate) {
        record_slice_failure(SliceFailureKind::Failed);
        m_failed_run_plate.reset();
    }
    publish_change(WorkspaceChangeReasons::Slicing);
}

void OrcaWorkspaceAdapter::on_process_completed(SlicingProcessCompletedEvent& event)
{
    event.Skip();
    wxASSERT(wxIsMainThread());
    if (!event.error())
        return;
    if (const PartPlate* current = m_plater.get_partplate_list().get_curr_plate())
        m_failed_run_plate = current->id().id;
}

void OrcaWorkspaceAdapter::on_slice_declined()
{
    wxASSERT(wxIsMainThread());
    // Inside the call that asked for the slice, so a tool reading the
    // snapshot when its request returns finds the reason already there.
    if (record_slice_failure(SliceFailureKind::Refused))
        publish_change(WorkspaceChangeReasons::Slicing);
}

bool OrcaWorkspaceAdapter::record_slice_failure(SliceFailureKind kind)
{
    const PartPlate* current = m_plater.get_partplate_list().get_curr_plate();
    if (current == nullptr)
        return false;
    const std::uint64_t plate = m_failed_run_plate && kind == SliceFailureKind::Failed ? *m_failed_run_plate : current->id().id;
    // The reader speaks for the active plate only, as OrcaSlicer's checks do.
    SliceFailure failure;
    if (m_slice_failure_reader && plate == current->id().id)
        if (std::optional<SliceFailure> read = m_slice_failure_reader())
            failure = std::move(*read);
    // The reader may find the plate's earlier failed run behind a refusal;
    // a run that failed now is a failure whatever else the plate holds.
    if (kind == SliceFailureKind::Failed)
        failure.kind = kind;
    if (failure.message.empty()) {
        failure.kind     = kind;
        failure.severity = "blocker";
        failure.message  = kind == SliceFailureKind::Refused ? "OrcaSlicer did not start slicing this plate." :
                                                               "The slice stopped with an error.";
    }
    const auto known = m_slice_failures.find(plate);
    if (known != m_slice_failures.end() && known->second.same_reason(failure))
        return false;
    failure.sequence        = ++m_next_slice_failure;
    m_slice_failures[plate] = std::move(failure);
    return true;
}

void OrcaWorkspaceAdapter::read_slice_warnings(std::uint64_t plate, std::uint64_t result) const
{
    if (!m_slice_warnings_reader)
        return;
    if (m_slice_warnings_session != m_session) {
        // Another project: nothing it was warned about describes this one.
        m_slice_warnings.clear();
        m_slice_warnings_session = m_session;
    }
    const std::optional<std::vector<SliceWarningFinding>> findings = m_slice_warnings_reader(plate);
    if (!findings)
        return;
    PlateWarnings& record = m_slice_warnings[plate];
    // A warning the plate's last record did not hold is news; the same set
    // read again, or a smaller one, is not.
    std::uint64_t sequence = record.summary ? record.summary->sequence : 0;
    if (!findings->empty() && (has_new_slice_warning(*findings, record.ids) || !record.summary))
        sequence = ++m_next_slice_warnings;
    record.result  = result;
    record.summary = summarize_slice_warnings(*findings, m_session, sequence);
    record.ids.clear();
    for (const SliceWarningFinding& finding : *findings)
        record.ids.insert(finding.id);
}

WorkspaceSnapshot OrcaWorkspaceAdapter::snapshot() const
{
    wxASSERT(wxIsMainThread());
    WorkspaceSnapshot result;
    result.session  = m_session;
    result.revision = m_changes.revision();

    // Read once: regional settings do not change while the app runs, and this
    // runs on every selection change.
    static const std::string currency = host_currency_code();
    result.currency = currency;

    result.setup.project_name  = m_plater.get_project_name().ToUTF8().data();
    result.setup.project_dirty = m_plater.is_project_dirty();
    result.setup.presets_dirty = m_plater.is_presets_dirty();
    result.setup.project_path  = into_u8(m_plater.get_project_filename(".3mf"));
    if (const PresetBundle* presets = wxGetApp().preset_bundle; presets != nullptr) {
        result.setup.printer_preset = presets->printers.get_selected_preset().name;
        if (presets->printers.get_edited_preset().printer_technology() == ptFFF) {
            result.setup.process_preset = presets->prints.get_edited_preset().name;
            Search::OptionsSearcher& searcher = m_plater.sidebar().get_searcher();
            const std::vector<std::string> process_dirty = presets->prints.current_dirty_options();
            result.setup.process_preset_dirty = !process_dirty.empty();
            result.preset_deltas = preset_deltas_of(presets->prints, process_dirty, Preset::TYPE_PRINT, searcher);
            const auto append = [&result, &searcher](const PresetCollection& collection, Preset::Type type) {
                std::vector<PresetDelta> deltas = preset_deltas_of(collection, collection.current_dirty_options(true),
                                                                   type, searcher);
                result.preset_deltas.insert(result.preset_deltas.end(),
                                            std::make_move_iterator(deltas.begin()), std::make_move_iterator(deltas.end()));
            };
            append(presets->filaments, Preset::TYPE_FILAMENT);
            append(presets->printers, Preset::TYPE_PRINTER);
        }
        if (!presets->filament_presets.empty())
            result.setup.filament_preset = presets->filament_presets.front();
    }

    PartPlateList& plate_list = m_plater.get_partplate_list();
    const int active_index = plate_list.get_curr_plate_index();
    // One background process serves the whole application, so "running" is a
    // property of the workspace; during a slice-all the plate it names moves as
    // the run advances.
    result.slicing.running = m_plater.is_background_process_slicing();
    result.jobs            = m_jobs;
    if (result.slicing.running) {
        if (PartPlate* current = plate_list.get_plate(active_index); current != nullptr) {
            result.slicing.plate   = PlateId(m_session, current->id().id);
            const int percent      = current->get_slicing_percent();
            if (percent >= 0 && percent <= 100)
                result.slicing.percent = percent;
        }
    }
    result.plates.reserve(plate_list.get_plate_count());
    for (int index = 0; index < plate_list.get_plate_count(); ++index) {
        PartPlate* plate = plate_list.get_plate(index);
        WorkspacePlate projected_plate;
        projected_plate.id     = PlateId(m_session, plate->id().id);
        projected_plate.name   = plate->get_plate_name().empty() ? "Plate " + std::to_string(index + 1) : plate->get_plate_name();
        projected_plate.active = index == active_index;
        projected_plate.bed_type = plate_label(plate->get_bed_type(true));
        projected_plate.sliced = plate->is_slice_result_valid();
        const std::uint64_t plate_key = plate->id().id;
        const bool this_plate_slicing = result.slicing.running && result.slicing.plate == projected_plate.id;
        if (projected_plate.sliced && !this_plate_slicing && plate->get_slice_result()) {
            projected_plate.estimate        = estimate_of(*plate->get_slice_result());
            projected_plate.estimate_status = EstimateStatus::Current;
            // The figure this plate can currently defend, kept so a slice in
            // flight or an invalidated one still has something to show.
            if (projected_plate.estimate) m_last_estimate[plate_key] = *projected_plate.estimate;
        } else if (const auto remembered = m_last_estimate.find(plate_key); remembered != m_last_estimate.end()) {
            projected_plate.estimate        = remembered->second;
            projected_plate.estimate_status = this_plate_slicing ?
                EstimateStatus::Recomputing : EstimateStatus::Stale;
            if (projected_plate.estimate_status == EstimateStatus::Stale) {
                if (const auto why = m_invalidated_by.find(plate_key); why != m_invalidated_by.end())
                    projected_plate.invalidated_by = why->second;
            }
        }
        if (this_plate_slicing && !projected_plate.estimate)
            projected_plate.estimate_status = EstimateStatus::Recomputing;
        // A run on the plate replaces whatever its last slice failed with,
        // from the moment it starts.
        if (const auto failure = m_slice_failures.find(plate_key); failure != m_slice_failures.end() && !this_plate_slicing)
            projected_plate.slice_failure = failure->second;
        // What the current slice warned about. OrcaSlicer checks only the
        // active plate, so another plate keeps what was read while it was
        // active, for as long as that slice is still its own.
        if (projected_plate.sliced && !this_plate_slicing && plate->get_slice_result()) {
            const std::uint64_t slice = plate->get_slice_result()->id;
            if (projected_plate.active)
                read_slice_warnings(plate_key, slice);
            if (const auto found = m_slice_warnings.find(plate_key); found != m_slice_warnings.end() && found->second.result == slice)
                projected_plate.slice_warnings = found->second.summary;
        }
        const ModelObjectPtrs& objects = m_plater.model().objects;
        for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
            WorkspaceObject object = project_object(m_session, *objects[object_index], *plate, static_cast<int>(object_index));
            if (!object.instances.empty())
                projected_plate.objects.emplace_back(std::move(object));
        }
        if (projected_plate.active)
            result.active_plate = projected_plate.id;
        if (projected_plate.active && wxGetApp().preset_bundle != nullptr &&
            wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF)
            result.applied_setup = applied_setup_of(m_session, m_plater, *plate);
        result.plates.emplace_back(std::move(projected_plate));
    }

    remember_current_ids();

    Selection& selection = m_plater.canvas3D()->get_selection();
    if (selection.is_empty()) {
        result.selection_status = SelectionStatus::None;
    } else if (selection.is_single_full_object()) {
        const int index = selection.get_object_idx();
        if (index >= 0 && index < static_cast<int>(m_plater.model().objects.size())) {
            result.selection_status = SelectionStatus::Objects;
            result.selected_objects.emplace_back(m_session, m_plater.model().objects[index]->id().id);
        } else {
            result.selection_status = SelectionStatus::Unsupported;
        }
    } else if (selection.is_multiple_full_object()) {
        std::set<ObjectId> selected;
        for (const auto& object_instance : selection.get_selected_object_instances()) {
            const int index = object_instance.first;
            if (index >= 0 && index < static_cast<int>(m_plater.model().objects.size()))
                selected.emplace(m_session, m_plater.model().objects[index]->id().id);
        }
        result.selection_status = SelectionStatus::Objects;
        result.selected_objects.assign(selected.begin(), selected.end());
    } else {
        result.selection_status = SelectionStatus::Unsupported;
    }

    result.can_undo = m_plater.can_undo_project();
    result.can_redo = m_plater.can_redo_project();
    return result;
}

CommandResult OrcaWorkspaceAdapter::select_object(ObjectId id)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);

    const WorkspaceSnapshot before = snapshot();
    if (before.selection_status == SelectionStatus::Objects && before.selected_objects == std::vector<ObjectId>{id})
        return CommandResult::failure(WorkspaceError::NoChange, "Object is already selected");
    if (!m_plater.select_object(object->index))
        return CommandResult::failure(WorkspaceError::Failed, "Object could not be selected");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::rename_object(ObjectId id, const std::string& name)
{
    wxASSERT(wxIsMainThread());
    if (blank(name))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Object name cannot be empty");
    const auto object = resolve(id);
    if (!object)
        return id_error(id);
    if (m_plater.model().objects[object->index]->name == name)
        return CommandResult::failure(WorkspaceError::NoChange, "Object already has that name");
    if (!m_plater.rename_object(object->index, name))
        return CommandResult::failure(WorkspaceError::Failed, "Object could not be renamed");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::duplicate_object(ObjectId id)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);

    const int new_index = m_plater.duplicate_object(object->index);
    if (new_index < 0 || new_index >= static_cast<int>(m_plater.model().objects.size()))
        return CommandResult::failure(WorkspaceError::Failed, "Object could not be duplicated");

    const ObjectId new_id(m_session, m_plater.model().objects[new_index]->id().id);
    m_known_object_ids.insert(new_id.value());
    return CommandResult::success(new_id);
}

CommandResult OrcaWorkspaceAdapter::remove_object(ObjectId id)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);
    if (!m_plater.delete_object(object->index))
        return CommandResult::failure(WorkspaceError::Failed, "Object could not be removed");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::undo()
{
    wxASSERT(wxIsMainThread());
    if (!m_plater.can_undo_project() || !m_plater.undo_project())
        return CommandResult::failure(WorkspaceError::Refused, "Nothing to undo");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::redo()
{
    wxASSERT(wxIsMainThread());
    if (!m_plater.can_redo_project() || !m_plater.redo_project())
        return CommandResult::failure(WorkspaceError::Refused, "Nothing to redo");
    return CommandResult::success();
}

namespace {
// Preset::get_printer_type answers for the edited printer whatever preset it
// is called on; this is the same lookup for the preset given.
std::string printer_model_id(const PresetBundle& presets, const Preset& printer)
{
    const std::string model = printer.config.opt_string("printer_model");
    for (const auto& [name, vendor] : presets.vendors)
        for (const auto& candidate : vendor.models)
            if (candidate.name == model)
                return candidate.model_id;
    return {};
}

// The plates a printer offers: every plate, less the ones its model excludes,
// as Sidebar::update_bed_type_list computes them for the selected printer.
std::vector<std::string> supported_plates(const PresetCollection& printers, const Preset& printer)
{
    const VendorProfile::PrinterModel* model = PresetUtils::system_printer_model(printer);
    if (model == nullptr)
        if (const Preset* parent = printers.get_preset_parent(printer))
            model = PresetUtils::system_printer_model(*parent);
    std::vector<std::string> result;
    if (const ConfigOptionDef* definition = print_config_def.get("curr_bed_type"))
        for (const std::string& label : definition->enum_labels)
            if (model == nullptr || std::find(model->not_support_bed_types.begin(), model->not_support_bed_types.end(), label) ==
                                        model->not_support_bed_types.end())
                result.push_back(label);
    return result;
}
} // namespace

std::string OrcaWorkspaceAdapter::current_process_preset() const
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    return presets != nullptr ? presets->prints.get_edited_preset().name : std::string();
}

PrinterSetupPreview OrcaWorkspaceAdapter::preview_printer_setup(const PrinterSetupRequest& request) const
{
    wxASSERT(wxIsMainThread());
    PrinterSetupPreview result;
    const PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        result.issues.push_back({"busy", "The presets are not loaded yet. Try again shortly."});
        return result;
    }
    const auto issue = [&result](std::string code, std::string message) {
        result.issues.push_back({std::move(code), std::move(message)});
    };
    // Const lookups only. PresetBundle::update_compatible would rewrite every
    // preset's flag and may select another preset, so compatibility is asked
    // of the free function against the candidate printer, the way
    // update_compatible_internal asks it.
    const PresetCollection& printers = bundle->printers;
    const Preset*           printer  = &printers.get_edited_preset();
    if (request.printer_preset) {
        const Preset* found = printers.find_preset(*request.printer_preset, false);
        if (found == nullptr || !found->is_visible || found->is_default) {
            issue("unknown_preset", "No installed printer preset is named \"" + *request.printer_preset + "\". If presets_list does not "
                                   "list it, the printer is not installed: ask the person to add it with Add printer on Home.");
            return result;
        }
        printer = found;
    }
    const bool printer_changes = printer->name != printers.get_edited_preset().name;
    const PresetWithVendorProfile printer_profile = printers.get_preset_with_vendor_profile(*printer);
    DynamicPrintConfig extra;
    extra.set_key_value("printer_preset", new ConfigOptionString(printer->name));
    const auto* nozzles = printer->config.option<ConfigOptionFloats>("nozzle_diameter");
    if (nozzles != nullptr)
        extra.set_key_value("num_extruders", new ConfigOptionInt(int(nozzles->values.size())));
    const auto compatible = [&](const PresetCollection& collection, const Preset& preset) {
        return is_compatible_with_printer(collection.get_preset_with_vendor_profile(preset), printer_profile, &extra);
    };
    const std::string printer_name = printer->name;

    const PresetCollection& prints = bundle->prints;
    const Preset*           print  = &prints.get_edited_preset();
    if (request.process_preset) {
        const Preset* found = prints.find_preset(*request.process_preset, false);
        if (found == nullptr || !found->is_visible || found->is_default)
            issue("unknown_preset", "No installed process preset is named \"" + *request.process_preset + "\".");
        else if (!compatible(prints, *found))
            issue("incompatible_preset", "\"" + found->name + "\" is not made for " + printer_name + ".");
        else
            print = found;
    } else if (printer_changes && !compatible(prints, *print)) {
        result.substitutions.push_back({"process", print->name, "not made for " + printer_name + "; OrcaSlicer picks another"});
    }
    if (request.process_preset && print->name == *request.process_preset) {
        // The same refusal settings_apply_patch makes: a process that would
        // open one of Orca's correction dialogs is not applied without one.
        SettingsPreview dialogs;
        check_process_dialogs(print->config, dialogs);
        for (const SettingIssue& found : dialogs.issues)
            issue("incompatible_settings", found.message);
    }

    const PresetCollection&  filaments = bundle->filaments;
    std::vector<std::string> filament_names = bundle->filament_presets;
    if (request.filament_presets) {
        if (request.filament_presets->size() > filament_names.size())
            issue("invalid_arguments", "This project has " + std::to_string(filament_names.size()) + " filament slots.");
        for (std::size_t slot = 0; slot < request.filament_presets->size() && slot < filament_names.size(); ++slot) {
            const std::string& name  = (*request.filament_presets)[slot];
            const Preset*      found = filaments.find_preset(name, false);
            if (found == nullptr || !found->is_visible || found->is_default)
                issue("unknown_preset", "No installed filament preset is named \"" + name + "\".");
            else if (!compatible(filaments, *found))
                issue("incompatible_preset", "\"" + name + "\" is not made for " + printer_name + ".");
            else
                filament_names[slot] = name;
        }
    }
    if (printer_changes)
        for (std::size_t slot = 0; slot < filament_names.size(); ++slot) {
            const bool requested = request.filament_presets && slot < request.filament_presets->size();
            const Preset* current = filaments.find_preset(filament_names[slot], false);
            if (!requested && current != nullptr && !compatible(filaments, *current))
                result.substitutions.push_back({"filament", filament_names[slot], "not made for " + printer_name + "; OrcaSlicer picks another"});
        }

    const std::vector<std::string> plates  = supported_plates(printers, *printer);
    std::string                    current = configured_printer().plate_type;
    std::string                    plate   = current;
    if (request.plate_type) {
        const auto found = std::find_if(plates.begin(), plates.end(), [&request](const std::string& label) {
            return ascii_lower(label) == ascii_lower(*request.plate_type);
        });
        if (found == plates.end()) {
            std::string offered;
            for (const std::string& label : plates) offered += (offered.empty() ? "" : ", ") + label;
            issue("unsupported_plate", printer_name + " offers these plates: " + offered + ".");
        } else {
            plate = *found;
        }
    } else if (printer_changes && !current.empty() && std::find(plates.begin(), plates.end(), current) == plates.end()) {
        result.substitutions.push_back({"plate", current, printer_name + " does not offer it; OrcaSlicer picks another"});
    }

    // What a switch would throw away. A printer switch can replace any of the
    // three, so every edited one is counted; otherwise only the one switched.
    const auto count_edits = [&result](const PresetCollection& collection, const char* kind) {
        const std::vector<std::string> dirty = collection.current_dirty_options();
        if (!dirty.empty())
            result.unsaved_edits.push_back({kind, collection.get_edited_preset().name, dirty.size()});
    };
    if (printer_changes) {
        count_edits(printers, "printer");
        count_edits(prints, "process");
        count_edits(filaments, "filament");
    } else {
        if (print->name != prints.get_edited_preset().name)
            count_edits(prints, "process");
        if (filament_names != bundle->filament_presets)
            count_edits(filaments, "filament");
    }

    result.resulting.preset = printer_name;
    result.resulting.model  = printer_model_id(*bundle, *printer);
    if (nozzles != nullptr)
        result.resulting.nozzle_diameters = nozzles->values;
    result.resulting.plate_type = plate;
    for (const std::string& name : filament_names) {
        ConfiguredFilament filament{name, {}};
        if (const Preset* preset = filaments.find_preset(name, false))
            if (const auto* types = preset->config.option<ConfigOptionStrings>("filament_type"); types && !types->values.empty())
                filament.material = types->values.front();
        result.resulting.filaments.push_back(std::move(filament));
    }
    result.process_preset = print->name;
    result.valid          = result.issues.empty();
    return result;
}

CommandResult OrcaWorkspaceAdapter::apply_printer_setup(const PrinterSetupRequest& request, PrinterSetupPreview& applied)
{
    wxASSERT(wxIsMainThread());
    const PrinterSetupPreview preview = preview_printer_setup(request);
    if (!preview.valid)
        return CommandResult::failure(WorkspaceError::InvalidArgument, preview.issues.front().message);
    PresetBundle* bundle = wxGetApp().preset_bundle;
    const auto tab = [](Preset::Type type) { return wxGetApp().get_tab(type); };

    // Tab::select_preset asks about unsaved edits in a modal. They are
    // dropped first, so nothing asks; the result lists them.
    for (const UnsavedEdits& edits : preview.unsaved_edits)
        drop_unsaved_edits(tab(edits.kind == "printer" ? Preset::TYPE_PRINTER :
                               edits.kind == "process" ? Preset::TYPE_PRINT : Preset::TYPE_FILAMENT));

    const std::string previous_printer = configured_printer().preset;
    const std::string previous_process = current_process_preset();
    const std::string previous_plate   = configured_printer().plate_type;
    const std::vector<std::string> previous_filaments = bundle->filament_presets;
    // A step OrcaSlicer refuses after earlier steps ran leaves those applied,
    // so the failure names them.
    const auto stopped = [&](std::string message) {
        std::vector<std::string> done;
        for (const UnsavedEdits& edits : preview.unsaved_edits)
            done.push_back("the unsaved edits to \"" + edits.preset + "\" were dropped");
        if (configured_printer().preset != previous_printer)
            done.push_back("the printer is now \"" + configured_printer().preset + "\"");
        if (current_process_preset() != previous_process)
            done.push_back("the process is now \"" + current_process_preset() + "\"");
        for (std::size_t slot = 0; slot < previous_filaments.size() && slot < bundle->filament_presets.size(); ++slot)
            if (bundle->filament_presets[slot] != previous_filaments[slot])
                done.push_back("filament " + std::to_string(slot + 1) + " is now \"" + bundle->filament_presets[slot] + "\"");
        for (std::size_t index = 0; index < done.size(); ++index)
            message += (index == 0 ? " Already changed by this call, and not undone: " : "; ") + done[index];
        if (!done.empty())
            message += ".";
        return CommandResult::failure(WorkspaceError::Failed, std::move(message));
    };
    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        // The preview has just found this preset by the same lookup.
        if (request.printer_preset && !SetupCommands::select_printer_preset(m_plater, *request.printer_preset))
            throw std::logic_error("OrcaSlicer did not select the printer preset its preview found: " + *request.printer_preset);
        if (request.plate_type) {
            const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
            int value = 0;
            for (int index = 0; definition != nullptr && index < int(definition->enum_labels.size()); ++index)
                if (definition->enum_labels[index] == preview.resulting.plate_type)
                    value = index + 1;
            if (value == 0 || !SetupCommands::select_bed_type(m_plater, value))
                return stopped("OrcaSlicer did not select the plate.");
        }
        if (request.process_preset && *request.process_preset != current_process_preset()) {
            // The process branch of Plater::priv::on_select_preset.
            tab(Preset::TYPE_PRINT)->select_preset(*request.process_preset);
            m_plater.on_config_change(bundle->full_config());
        }
        if (request.filament_presets)
            for (std::size_t slot = 0; slot < request.filament_presets->size(); ++slot) {
                const std::string& name = (*request.filament_presets)[slot];
                if (slot < bundle->filament_presets.size() && bundle->filament_presets[slot] == name)
                    continue;
                // The same path the header's filament menu takes, so a slot
                // changed by the agent gets Orca's colour rule too.
                if (!SetupCommands::select_filament_preset(m_plater, slot, name))
                    return stopped("OrcaSlicer did not select filament " + std::to_string(slot + 1) + ".");
            }
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Settings);
    }

    // What actually happened, read back rather than predicted.
    applied            = preview;
    applied.resulting  = configured_printer();
    applied.process_preset = current_process_preset();
    applied.substitutions.clear();
    if (!request.process_preset && applied.process_preset != previous_process)
        applied.substitutions.push_back({"process", previous_process, "replaced with " + applied.process_preset});
    if (!request.plate_type && applied.resulting.plate_type != previous_plate)
        applied.substitutions.push_back({"plate", previous_plate, "replaced with " + applied.resulting.plate_type});
    for (std::size_t slot = 0; slot < previous_filaments.size() && slot < bundle->filament_presets.size(); ++slot) {
        const bool requested = request.filament_presets && slot < request.filament_presets->size();
        if (!requested && bundle->filament_presets[slot] != previous_filaments[slot])
            applied.substitutions.push_back({"filament", previous_filaments[slot], "replaced with " + bundle->filament_presets[slot]});
    }
    return CommandResult::success();
}


// Runs an Orca job unchanged and says how it ended. Orca's worker has no
// completion event; finalize runs on the UI thread, and a job dropped from
// the queue by a later replace_job is never finalized, which the destructor
// reports as cancelled.
class ReportingJob final : public Job
{
public:
    using Done = std::function<void(const char*)>;
    ReportingJob(std::unique_ptr<Job> inner, Done done) : m_inner(std::move(inner)), m_done(std::move(done)) {}
    ~ReportingJob() override
    {
        if (!m_reported) m_done("cancelled");
    }
    void process(Ctl& ctl) override { m_inner->process(ctl); }
    void finalize(bool canceled, std::exception_ptr& eptr) override
    {
        m_inner->finalize(canceled, eptr);
        m_reported = true;
        m_done(canceled ? "cancelled" : eptr ? "failed" : "finished");
    }

private:
    std::unique_ptr<Job> m_inner;
    Done                 m_done;
    bool                 m_reported{false};
};

bool OrcaWorkspaceAdapter::start_job(const std::string& handle, const std::string& kind, std::unique_ptr<Job> job)
{
    Worker& worker = m_plater.get_ui_job_worker();
    if (!worker.is_idle())
        return false;
    if (m_jobs.size() == kJobLimit)
        m_jobs.erase(m_jobs.begin());
    m_jobs.push_back({handle, kind, "running", {}});
    std::weak_ptr<bool> alive = m_alive;
    replace_job(worker, std::make_shared<ReportingJob>(std::move(job), [this, alive, handle](const char* state) {
        if (!alive.expired())
            finish_job(handle, state);
    }));
    return true;
}

void OrcaWorkspaceAdapter::finish_job(const std::string& handle, const char* state)
{
    const auto job = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const WorkspaceJob& j) { return j.handle == handle; });
    if (job == m_jobs.end() || job->state != "running")
        return;
    job->state = state;
    if (job->kind == "arrange" && m_arrange_restore) {
        GLCanvas3D::ArrangeSettings& settings = m_plater.canvas3D()->get_arrange_settings();
        settings.distance        = m_arrange_restore->first;
        settings.enable_rotation = m_arrange_restore->second;
        m_arrange_restore.reset();
    }
    if (job->kind == "arrange" && job->state == "finished") {
        // Orca moves what does not fit onto a new plate or off the plates; an
        // instance no plate holds entirely is what did not fit.
        PartPlateList&         plates  = m_plater.get_partplate_list();
        const ModelObjectPtrs& objects = m_plater.model().objects;
        for (std::size_t index = 0; index < objects.size(); ++index)
            for (std::size_t instance = 0; instance < objects[index]->instances.size(); ++instance)
                if (objects[index]->instances[instance]->printable &&
                    plates.find_instance_belongs(static_cast<int>(index), static_cast<int>(instance)) < 0) {
                    job->not_placed.push_back(ObjectId(m_session, objects[index]->id().id));
                    break;
                }
    }
    publish_change(WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::Transform);
}

CommandResult OrcaWorkspaceAdapter::lay_out(const LayoutRequest& request, const std::string& job_handle, LayoutResult& result)
{
    wxASSERT(wxIsMainThread());
    Model&         model  = m_plater.model();
    PartPlateList& plates = m_plater.get_partplate_list();
    const auto plate_index = [&](PlateId id) -> int {
        if (id.session() != m_session)
            return -1;
        for (int index = 0; index < plates.get_plate_count(); ++index)
            if (plates.get_plate(index)->id().id == id.value())
                return index;
        return -1;
    };
    const auto bed_type_of = [](const std::string& label) -> std::optional<BedType> {
        const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
        for (int index = 0; definition != nullptr && index < int(definition->enum_labels.size()); ++index)
            if (ascii_lower(definition->enum_labels[index]) == ascii_lower(label))
                return BedType(index + 1);
        return std::nullopt;
    };
    const int filaments = int(wxGetApp().preset_bundle->filament_presets.size());

    // Everything is checked before anything moves.
    std::vector<std::size_t> indices;
    for (const LayoutObject& row : request.objects) {
        const auto resolved = resolve(row.id);
        if (!resolved)
            return id_error(row.id);
        const ModelObject& object = *model.objects[resolved->index];
        if (row.plate && plate_index(*row.plate) < 0)
            return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
        if (row.extruder && (*row.extruder < 1 || *row.extruder > filaments))
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "This project has " + std::to_string(filaments) + " filaments");
        if (row.quantity && *row.quantity != object.instances.size() && object.is_cut())
            return CommandResult::failure(WorkspaceError::Refused, "OrcaSlicer does not copy a cut object");
        // Orca adds copies only of an object whose copies all print, as they
        // will once this row's enabled is applied.
        const bool prints = row.enabled ? *row.enabled :
                                          std::all_of(object.instances.begin(), object.instances.end(),
                                                      [](const ModelInstance* instance) { return instance->printable; });
        if (row.quantity && *row.quantity > object.instances.size() && !prints)
            return CommandResult::failure(WorkspaceError::Refused,
                                          row.enabled ? object.name + " is disabled by this same row, and OrcaSlicer does not copy a disabled object." :
                                                        object.name + " is disabled, and OrcaSlicer does not copy a disabled object. "
                                                                      "Pass enabled: true in the same row to copy it.");
        if (row.quantity && *row.quantity != object.instances.size() &&
            m_plater.canvas3D()->get_gizmos_manager().is_in_editing_mode())
            return CommandResult::failure(WorkspaceError::Refused,
                                          "An OrcaSlicer editing tool is open, and OrcaSlicer does not change copies while it is. "
                                          "Ask the person to close it.");
        if (row.name && row.name->find_first_not_of(" \t") == std::string::npos)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "An object name cannot be empty");
        indices.push_back(resolved->index);
    }
    for (const LayoutPlate& row : request.plates) {
        if (row.id && plate_index(*row.id) < 0)
            return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
        if (row.bed_type && !bed_type_of(*row.bed_type))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + *row.bed_type + "\" is not a plate type OrcaSlicer knows");
    }
    if (request.arrange && request.arrange->plate && plate_index(*request.arrange->plate) < 0)
        return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
    Worker& worker = m_plater.get_ui_job_worker();
    if (!worker.is_idle())
        return CommandResult::failure(WorkspaceError::Busy, "OrcaSlicer is busy with another job. Try again when it finishes.");
    const int added = int(std::count_if(request.plates.begin(), request.plates.end(), [](const LayoutPlate& row) { return !row.id; }));
    if (added > 0) {
        const int limit = int(PartPlateList::MAX_PLATES_COUNT);
        if (plates.get_plate_count() + added > limit)
            return CommandResult::failure(WorkspaceError::Refused,
                                          "A project holds at most " + std::to_string(limit) + " plates; this one has " +
                                              std::to_string(plates.get_plate_count()) + ", so it can take " +
                                              std::to_string(limit - plates.get_plate_count()) + " more.");
        if (!m_plater.can_add_plate())
            return CommandResult::failure(WorkspaceError::Busy, "OrcaSlicer does not add a plate while it slices. Try again when the slice ends.");
    }

    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        Plater::TakeSnapshot snapshot(&m_plater, "Lay out plates");

        for (const LayoutPlate& row : request.plates) {
            int index = row.id ? plate_index(*row.id) : -1;
            if (!row.id) {
                // The add-plate toolbar action, less its own snapshot.
                if (!m_plater.can_add_plate())
                    throw std::logic_error("OrcaSlicer refused a plate the layout checked it could add");
                index = plates.create_plate();
                result.added_plates.push_back(PlateId(m_session, plates.get_plate(index)->id().id));
            }
            PartPlate& plate = *plates.get_plate(index);
            if (row.name)
                plate.set_plate_name(*row.name);
            if (row.bed_type)
                plate.set_bed_type(*bed_type_of(*row.bed_type));
        }

        for (std::size_t row_index = 0; row_index < request.objects.size(); ++row_index) {
            const LayoutObject& row    = request.objects[row_index];
            const std::size_t   index  = indices[row_index];
            ModelObject&        object = *model.objects[index];
            if (row.enabled) {
                // ObjectList::toggle_printable_state, for the whole object.
                for (ModelInstance* instance : object.instances)
                    instance->printable = *row.enabled;
                if (ObjectList* list = wxGetApp().obj_list())
                    for (std::size_t instance = 0; instance < object.instances.size(); ++instance)
                        list->update_printable_state(static_cast<int>(index), static_cast<int>(instance));
                m_plater.canvas3D()->update_instance_printable_state_for_objects({index});
            }
            if (row.quantity && *row.quantity != object.instances.size()) {
                // Orca's own instance commands work on the selected object.
                m_plater.select_object(index);
                if (*row.quantity > object.instances.size())
                    m_plater.increase_instances(*row.quantity - object.instances.size());
                else
                    m_plater.decrease_instances(object.instances.size() - *row.quantity);
                if (object.instances.size() != *row.quantity)
                    throw std::logic_error("OrcaSlicer did not change the copies of " + object.name + " after the layout's checks passed");
            }
            if (row.plate) {
                const int from = plates.find_instance_belongs(static_cast<int>(index), 0);
                const int to   = plate_index(*row.plate);
                if (from != to) {
                    Vec3d delta = from >= 0 ? Vec3d(plates.get_plate(to)->get_origin() - plates.get_plate(from)->get_origin()) :
                                              Vec3d(plates.get_plate(to)->get_center_origin() - object.instance_bounding_box(0).center());
                    delta.z() = 0;
                    m_plater.select_object(index);
                    Selection& selection = m_plater.canvas3D()->get_selection();
                    TransformationType relative;
                    relative.set_world();
                    relative.set_relative();
                    selection.setup_cache();
                    selection.translate(delta, relative);
                    m_plater.canvas3D()->do_move("");
                }
            }
            if (row.name)
                m_plater.rename_object(index, *row.name);
            if (row.extruder) {
                object.config.set_key_value("extruder", new ConfigOptionInt(*row.extruder));
                if (ObjectList* list = wxGetApp().obj_list())
                    list->object_config_options_changed({&object, nullptr});
                m_plater.changed_object(static_cast<int>(index));
            }
        }
        m_plater.update();
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects | ProjectStateChangeReason::Plates |
                                              ProjectStateChangeReason::Transform);
    }

    if (request.arrange) {
        // Plater::arrange, with the snapshot above and the spacing and
        // rotation asked for, restored when the job ends.
        GLCanvas3D::ArrangeSettings& settings = m_plater.canvas3D()->get_arrange_settings();
        m_arrange_restore = std::make_pair(settings.distance, settings.enable_rotation);
        if (request.arrange->spacing) settings.distance = float(*request.arrange->spacing);
        if (request.arrange->rotation) settings.enable_rotation = *request.arrange->rotation;
        if (request.arrange->plate) {
            m_plater.select_plate(plate_index(*request.arrange->plate));
            m_plater.set_prepare_state(Job::PREPARE_STATE_MENU);
        } else {
            m_plater.set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        }
        if (!start_job(job_handle, "arrange", std::make_unique<ArrangeJob>())) {
            settings.distance        = m_arrange_restore->first;
            settings.enable_rotation = m_arrange_restore->second;
            m_arrange_restore.reset();
            // The worker was idle at the check above, and nothing since starts a job.
            throw std::logic_error("OrcaSlicer's job worker turned busy during a layout");
        }
        result.arranging = true;
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::place_object(ObjectId id, const PlacementRequest& request, const std::string& job_handle,
                                                 PlacementResult& result)
{
    wxASSERT(wxIsMainThread());
    auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    Model& model = m_plater.model();
    if (request.instance >= model.objects[resolved->index]->instances.size())
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The object has no such instance");
    if (request.auto_orient && !m_plater.get_ui_job_worker().is_idle())
        return CommandResult::failure(WorkspaceError::Busy, "OrcaSlicer is busy with another job. Try again when it finishes.");
    if (!request.mirror_axis.empty() && model.objects[resolved->index]->is_cut())
        return CommandResult::failure(WorkspaceError::Refused, "OrcaSlicer does not mirror a cut object");

    // The face must still be the one that was read: resolve it before
    // anything moves.
    std::optional<Vec3d> face_normal;
    if (!request.face_down.empty()) {
        const auto address = parse_feature_handle(request.face_down);
        if (!address || address->object != id.value())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.face_down + "\" is not a face of this object");
        if (address->revision != m_changes.revision())
            return CommandResult::failure(WorkspaceError::FeatureExpired,
                                          "The project changed since \"" + request.face_down + "\" was found. Read the features again.");
        const ModelObject& object = *model.objects[resolved->index];
        if (address->volume >= object.volumes.size() || !object.volumes[address->volume]->is_model_part())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.face_down + "\" is not a face of this object");
        const ModelVolume&       volume = *object.volumes[address->volume];
        const Measure::Measuring measuring(volume.mesh().its);
        if (address->plane >= measuring.get_num_of_planes() ||
            address->feature >= static_cast<int>(measuring.get_plane_features(address->plane).size()) ||
            measuring.get_plane_features(address->plane)[address->feature].get_type() != Measure::SurfaceFeatureType::Plane)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.face_down + "\" is not a face of this object");
        const auto [unused, normal, point] = measuring.get_plane_features(address->plane)[address->feature].get_plane();
        // Selection::flattening_rotate takes the normal in the instance's
        // frame: after the volume matrix, before the instance matrix.
        face_normal = (volume.get_matrix().linear().inverse().transpose() * normal).normalized();
    }

    GLCanvas3D& canvas    = *m_plater.canvas3D();
    Selection&  selection = canvas.get_selection();
    const auto  select = [&]() {
        selection.add_instance(static_cast<unsigned int>(resolved->index), static_cast<unsigned int>(request.instance), true);
        if (ObjectList* list = wxGetApp().obj_list())
            list->update_selections();
    };
    TransformationType world_relative;
    world_relative.set_world();
    world_relative.set_relative();
    world_relative.set_joint();

    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        // One undo step for everything below; the canvas's own snapshots are
        // suppressed inside it.
        Plater::TakeSnapshot snapshot(&m_plater, "Place object");
        select();

        if (!request.units_fix.empty()) {
            // Orca replaces the object with a converted copy at the end of the
            // list; a too-large question on reload is declined, the least
            // change, as anything else it might ask would be.
            ScopedLeastChangeAnswers answers;
            m_plater.convert_unit(request.units_fix == "inches" ? ConversionType::CONV_FROM_INCH : ConversionType::CONV_FROM_METER);
            resolved = ResolvedObject{model.objects.size() - 1};
            select();
        }
        if (request.scale || request.scale_to) {
            Vec3d factors = Vec3d::Ones();
            if (request.scale) {
                factors = Vec3d((*request.scale)[0], (*request.scale)[1], (*request.scale)[2]);
            } else {
                const double current = model.objects[resolved->index]->instance_bounding_box(request.instance).size()[request.scale_to->first];
                if (current <= 0)
                    return CommandResult::failure(WorkspaceError::InvalidArgument, "The object has no size along that axis");
                factors = Vec3d::Constant(request.scale_to->second / current);
            }
            selection.setup_cache();
            selection.scale(factors, world_relative);
            canvas.do_scale("");
        }
        if (!request.mirror_axis.empty()) {
            selection.setup_cache();
            selection.mirror(request.mirror_axis == "x" ? X : request.mirror_axis == "y" ? Y : Z, world_relative);
            canvas.do_mirror("");
        }
        if (face_normal) {
            selection.setup_cache();
            selection.flattening_rotate(*face_normal);
            // The flatten gizmo's own snapshot name: do_rotate keeps a
            // sinking object on the bed only for it.
            canvas.do_rotate("Gizmo-Place on Face");
        }
        if (request.rotate) {
            for (int axis = 0; axis < 3; ++axis) {
                const double degrees = (*request.rotate)[axis];
                if (degrees == 0)
                    continue;
                Vec3d rotation = Vec3d::Zero();
                rotation[axis] = degrees * PI / 180.0;
                selection.setup_cache();
                selection.rotate(rotation, world_relative);
                canvas.do_rotate("");
            }
        }
        if (request.position) {
            const Vec3d offset = model.objects[resolved->index]->instances[request.instance]->get_offset();
            selection.setup_cache();
            selection.translate(Vec3d((*request.position)[0] - offset.x(), (*request.position)[1] - offset.y(), 0), world_relative);
            canvas.do_move("");
        }
        if (request.drop_to_bed)
            selection.drop();
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Transform | ProjectStateChangeReason::Objects);
    }

    const ModelObject& placed = *model.objects[resolved->index];
    result.object    = ObjectId(m_session, placed.id().id);
    result.transform = transform_of(*placed.instances[request.instance]);
    result.size      = vec3(placed.instance_bounding_box(request.instance).size());

    if (request.auto_orient) {
        // Plater::orient, with its snapshot already taken above and the job
        // wrapped so its end is reported under the caller's handle. The job
        // orients the selection, which is this object.
        m_plater.set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        // The worker was idle at the check above, and nothing since starts a job.
        if (!start_job(job_handle, "orient", std::make_unique<OrientJob>()))
            throw std::logic_error("OrcaSlicer's job worker turned busy during a placement");
        result.orienting = true;
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::analyze_object(ObjectId id, const AnalysisRequest& request, ObjectAnalysis& result) const
{
    wxASSERT(wxIsMainThread());
    const auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    Model&             model  = m_plater.model();
    const ModelObject& object = *model.objects[resolved->index];
    if (object.instances.empty())
        return CommandResult::failure(WorkspaceError::Refused, "The object has no instance to measure");
    const std::uint64_t revision = m_changes.revision();

    if (request.mesh) {
        MeshFacts facts;
        const TriangleMeshStats stats = object.get_object_stl_stats();
        facts.size              = vec3(object.instance_bounding_box(0).size());
        facts.volume            = stats.volume;
        // The object-level stats do not add up facets; the parts' meshes do.
        for (const ModelVolume* volume : object.volumes)
            if (volume->is_model_part())
                facts.facets += volume->mesh().facets_count();
        facts.parts             = static_cast<std::size_t>(std::max(stats.number_of_parts, 0));
        facts.open_edges        = stats.open_edges;
        facts.edges_fixed       = stats.repaired_errors.edges_fixed;
        facts.degenerate_facets = stats.repaired_errors.degenerate_facets;
        facts.facets_removed    = stats.repaired_errors.facets_removed;
        facts.facets_reversed   = stats.repaired_errors.facets_reversed;
        facts.backwards_edges   = stats.repaired_errors.backwards_edges;
        for (const ModelVolume* volume : object.volumes) {
            if (facts.part_list.size() == 16)
                break;
            const char* kind = volume->is_model_part()      ? "model" :
                               volume->is_modifier()        ? "modifier" :
                               volume->is_negative_volume() ? "negative" :
                               volume->is_support_enforcer() ? "enforcer" : "blocker";
            facts.part_list.push_back({volume->id().id, volume->name, kind, volume->mesh().facets_count()});
        }
        // Orca's own load-time unit tests work on a whole model; ask them
        // about a model holding only this object, metres first as Orca does.
        Model single;
        single.add_object(object);
        facts.units_suspicion = single.looks_like_saved_in_meters() ? "meters" :
                                single.looks_like_imperial_units()  ? "inches" : "";
        result.mesh = facts;
    }

    if (request.features || request.orientations) {
        ObjectFeatures features;
        for (std::size_t volume_index = 0; volume_index < object.volumes.size(); ++volume_index) {
            const ModelVolume& volume = *object.volumes[volume_index];
            if (!volume.is_model_part())
                continue;
            // The measure gizmo's own construction: one Measuring per part on
            // its untransformed mesh, features moved to the world afterwards.
            const Transform3d            world = world_of(object, volume);
            const indexed_triangle_set&  its   = volume.mesh().its;
            const Measure::Measuring     measuring(its);
            for (int plane = 0; plane < measuring.get_num_of_planes(); ++plane) {
                const std::vector<int>& triangles = measuring.get_plane_triangle_indices(plane);
                double area = 0;
                std::vector<Vec3d> corners;
                for (int index : triangles) {
                    const auto& face = its.indices[index];
                    const Vec3d a = world * its.vertices[face[0]].cast<double>(), b = world * its.vertices[face[1]].cast<double>(),
                                c = world * its.vertices[face[2]].cast<double>();
                    area += 0.5 * (b - a).cross(c - a).norm();
                    corners.insert(corners.end(), {a, b, c});
                }
                const std::vector<Measure::SurfaceFeature>& found = measuring.get_plane_features(plane);
                for (int index = 0; index < static_cast<int>(found.size()); ++index) {
                    Measure::SurfaceFeature feature(found[index]);
                    const FeatureAddress address{revision, id.value(), volume_index, plane, index};
                    if (feature.get_type() == Measure::SurfaceFeatureType::Plane) {
                        feature.translate(world);
                        const auto [unused, normal, point] = feature.get_plane();
                        // A plane whose border could not be walked has no centre.
                        if (!normal.allFinite() || !point.allFinite())
                            continue;
                        // The extent in the face's plane: the tightest box
                        // aligned with one of its edges, so a rectangle
                        // reports its sides rather than a diagonal.
                        const Vec3d unit = normal.normalized();
                        std::array<double, 2> extent{0, 0};
                        double best = std::numeric_limits<double>::max();
                        for (std::size_t edge = 0; edge + 1 < corners.size() && edge < 3 * 64; ++edge) {
                            Vec3d axis = corners[edge + (edge % 3 == 2 ? -2 : 1)] - corners[edge];
                            axis -= unit * unit.dot(axis);
                            if (axis.norm() < 1e-9)
                                continue;
                            axis.normalize();
                            const Vec3d other = unit.cross(axis);
                            double min_u = std::numeric_limits<double>::max(), max_u = -min_u, min_v = min_u, max_v = -min_u;
                            for (const Vec3d& corner : corners) {
                                min_u = std::min(min_u, corner.dot(axis)), max_u = std::max(max_u, corner.dot(axis));
                                min_v = std::min(min_v, corner.dot(other)), max_v = std::max(max_v, corner.dot(other));
                            }
                            if ((max_u - min_u) * (max_v - min_v) < best) {
                                best   = (max_u - min_u) * (max_v - min_v);
                                extent = {std::max(max_u - min_u, max_v - min_v), std::min(max_u - min_u, max_v - min_v)};
                            }
                        }
                        features.faces.push_back({feature_handle(address), area, vec3(unit), vec3(point), extent});
                    } else if (feature.get_type() == Measure::SurfaceFeatureType::Circle) {
                        const auto [local_center, local_radius, local_normal] = feature.get_circle();
                        if (covered_by(its, triangles, local_center))
                            continue; // the rim of a boss or a disc, not a hole
                        feature.translate(world);
                        const auto [center, radius, normal] = feature.get_circle();
                        features.holes.push_back({feature_handle(address), 2 * radius, vec3(center), vec3(normal.normalized())});
                    }
                }
            }
        }
        std::stable_sort(features.faces.begin(), features.faces.end(),
                         [](const FaceFeature& a, const FaceFeature& b) { return a.area > b.area; });
        std::stable_sort(features.holes.begin(), features.holes.end(),
                         [](const HoleFeature& a, const HoleFeature& b) { return a.diameter > b.diameter; });

        if (request.orientations) {
            std::vector<Vec3d> ups;
            for (const OrientationCandidate& candidate : request.candidates) {
                if (candidate.up) {
                    const Vec3d up((*candidate.up)[0], (*candidate.up)[1], (*candidate.up)[2]);
                    if (up.norm() < 1e-9)
                        return CommandResult::failure(WorkspaceError::InvalidArgument, "An up direction cannot be zero");
                    ups.push_back(up);
                    continue;
                }
                const auto address = parse_feature_handle(candidate.face_down);
                if (address && address->object == id.value() && address->revision != revision)
                    return CommandResult::failure(WorkspaceError::FeatureExpired,
                                                  "The project changed since \"" + candidate.face_down + "\" was found. Read the features again.");
                const auto face = std::find_if(features.faces.begin(), features.faces.end(),
                                               [&](const FaceFeature& f) { return f.handle == candidate.face_down; });
                if (face == features.faces.end())
                    return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + candidate.face_down + "\" is not a face of this object");
                ups.push_back(-Vec3d(face->normal[0], face->normal[1], face->normal[2]));
            }
            // OrientJob's own inputs for one instance: the parts in the
            // instance's rotation, the object's or the print's overhang
            // angle, and the canvas's area-or-volume choice.
            orientation::OrientMesh mesh;
            mesh.name = object.name;
            mesh.mesh = object.raw_mesh();
            mesh.mesh.transform(object.instances.front()->get_matrix_no_offset());
            mesh.overhang_angle = object.config.has("support_threshold_angle") ?
                                      object.config.opt_int("support_threshold_angle") :
                                      wxGetApp().preset_bundle->full_config().opt_int("support_threshold_angle");
            orientation::OrientParams params;
            if (m_plater.canvas3D()->get_orient_settings().min_area) {
                // Exactly as OrientJob::process builds it.
                orientation::OrientParamsArea area;
                std::memcpy(static_cast<void*>(&params), &area, sizeof(params));
                params.min_volume = false;
            } else {
                params.min_volume = true;
            }
            std::vector<OrientationOption> options;
            for (const orientation::OrientationScore& score : orientation::score_orientations(mesh, ups, params)) {
                if (options.size() == kOrientationLimit)
                    break;
                OrientationOption option{vec3(score.up), score.unprintability, score.overhang, score.bottom, {}};
                for (const FaceFeature& face : features.faces)
                    if (option.faces_down.size() < kOrientationLimit &&
                        Vec3d(face.normal[0], face.normal[1], face.normal[2]).dot(score.up) < -0.999)
                        option.faces_down.push_back(face.handle);
                options.push_back(std::move(option));
            }
            result.orientations = std::move(options);
        }

        if (features.faces.size() > kFeatureLimit) features.faces.resize(kFeatureLimit), features.truncated = true;
        if (features.holes.size() > kFeatureLimit) features.holes.resize(kFeatureLimit), features.truncated = true;
        if (request.features)
            result.features = std::move(features);
    }

    if (request.fit) {
        ObjectFit       fit;
        PartPlateList&  plates = m_plater.get_partplate_list();
        const int       object_index = static_cast<int>(resolved->index);
        int             home = -1;
        for (std::size_t instance = 0; instance < object.instances.size(); ++instance) {
            if (fit.instances.size() == 16) { fit.truncated = true; break; }
            InstanceFit row;
            row.instance = instance;
            const int plate = plates.find_instance_belongs(object_index, static_cast<int>(instance));
            if (plate >= 0) {
                row.plate  = PlateId(m_session, plates.get_plate(plate)->id().id);
                row.inside = !plates.get_plate(plate)->check_outside(object_index, static_cast<int>(instance));
                if (home < 0) home = plate;
            }
            fit.instances.push_back(row);
        }
        const Polygons footprint{object.instances.front()->convex_hull_2d()};
        const TriangleMeshStats stats = object.get_object_stl_stats();
        const Vec3d size = object.instance_bounding_box(0).size();
        for (std::size_t other = 0; other < model.objects.size(); ++other) {
            if (other == resolved->index)
                continue;
            ModelObject& candidate = *model.objects[other];
            const ObjectId candidate_id(m_session, candidate.id().id);
            if (home >= 0)
                for (std::size_t instance = 0; instance < candidate.instances.size(); ++instance)
                    if (plates.find_instance(static_cast<int>(other), static_cast<int>(instance)) == home &&
                        !intersection(footprint, Polygons{candidate.instances[instance]->convex_hull_2d()}).empty()) {
                        if (fit.overlaps.size() < 16) fit.overlaps.push_back(candidate_id); else fit.truncated = true;
                        break;
                    }
            if (candidate.instances.empty())
                continue;
            // Same facet count, volume within 0.1 % and size within 0.05 mm:
            // the same model loaded twice, not a second instance.
            const TriangleMeshStats other_stats = candidate.get_object_stl_stats();
            if (other_stats.number_of_facets == stats.number_of_facets &&
                std::abs(other_stats.volume - stats.volume) <= 0.001 * std::abs(stats.volume) &&
                (candidate.instance_bounding_box(0).size() - size).cwiseAbs().maxCoeff() < 0.05) {
                if (fit.likely_duplicates.size() < 16) fit.likely_duplicates.push_back(candidate_id); else fit.truncated = true;
            }
        }
        result.fit = std::move(fit);
    }

    if (request.measure) {
        const auto feature_at = [&](const std::string& handle, std::optional<Measure::SurfaceFeature>& out) -> CommandResult {
            const auto address = parse_feature_handle(handle);
            if (!address || address->object != id.value())
                return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + handle + "\" is not a feature of this object");
            if (address->revision != revision)
                return CommandResult::failure(WorkspaceError::FeatureExpired,
                                              "The project changed since \"" + handle + "\" was found. Read the features again.");
            if (address->volume >= object.volumes.size() || !object.volumes[address->volume]->is_model_part())
                return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + handle + "\" is not a feature of this object");
            const ModelVolume&        volume = *object.volumes[address->volume];
            const Measure::Measuring  measuring(volume.mesh().its);
            if (address->plane >= measuring.get_num_of_planes() ||
                address->feature >= static_cast<int>(measuring.get_plane_features(address->plane).size()))
                return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + handle + "\" is not a feature of this object");
            out.emplace(measuring.get_plane_features(address->plane)[address->feature]);
            out->translate(world_of(object, volume));
            return CommandResult::success();
        };
        std::optional<Measure::SurfaceFeature> from, to;
        if (auto found = feature_at(request.measure->first, from); !found.succeeded()) return found;
        if (auto found = feature_at(request.measure->second, to); !found.succeeded()) return found;
        // As the measure gizmo asks it.
        const Measure::MeasurementResult measured = Measure::get_measurement(*from, *to, true);
        FeatureMeasurement measurement;
        if (measured.distance_strict) measurement.distance = measured.distance_strict->dist;
        if (measured.distance_infinite) measurement.plane_distance = measured.distance_infinite->dist;
        if (measured.angle) measurement.angle = measured.angle->angle * 180.0 / PI;
        if (measured.distance_xyz) measurement.delta = vec3(*measured.distance_xyz);
        result.measurement = measurement;
    }
    return CommandResult::success();
}

std::vector<ObjectDetails> OrcaWorkspaceAdapter::object_details() const
{
    wxASSERT(wxIsMainThread());
    std::vector<ObjectDetails> result;
    PartPlateList&         plates  = m_plater.get_partplate_list();
    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const ModelObject& object = *objects[index];
        ObjectDetails row;
        row.id        = ObjectId(m_session, object.id().id);
        row.name      = object.name;
        row.instances = object.instances.size();
        row.printable = object.printable;
        // Orca keeps every object's extruder in its config; that is the
        // extruder field below, not a setting the object overrides.
        row.overrides = object.config.size();
        if (object.config.has("extruder")) {
            row.extruder = object.config.opt_int("extruder");
            --row.overrides;
        }
        for (const ModelVolume* volume : object.volumes) {
            if (volume->is_model_part()) ++row.parts;
            else if (volume->is_modifier()) ++row.modifiers;
            else if (volume->is_negative_volume()) ++row.negative_parts;
            else if (volume->is_support_modifier()) ++row.support_volumes;
        }
        if (!object.instances.empty())
            row.size = vec3(object.instance_bounding_box(0).size());
        for (int plate = 0; plate < plates.get_plate_count(); ++plate)
            for (std::size_t instance = 0; instance < object.instances.size(); ++instance)
                if (plates.get_plate(plate)->contain_instance(static_cast<int>(index), static_cast<int>(instance))) {
                    row.plates.push_back(PlateId(m_session, plates.get_plate(plate)->id().id));
                    break;
                }
        result.push_back(std::move(row));
    }
    return result;
}

ConfiguredPrinter OrcaWorkspaceAdapter::configured_printer() const
{
    wxASSERT(wxIsMainThread());
    ConfiguredPrinter result;
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return result;
    Preset& printer = presets->printers.get_edited_preset();
    result.preset = presets->printers.get_selected_preset().name;
    result.model  = printer_model_id(*presets, printer);
    if (const auto* nozzles = printer.config.option<ConfigOptionFloats>("nozzle_diameter"))
        result.nozzle_diameters = nozzles->values;
    // The plate's own choice, falling back to the project's.
    if (const PartPlate* plate = m_plater.get_partplate_list().get_curr_plate()) {
        result.plate_type = plate_label(plate->get_bed_type(true));
    }
    for (const std::string& name : presets->filament_presets) {
        ConfiguredFilament filament{name, {}};
        if (const Preset* preset = presets->filaments.find_preset(name, false))
            if (const auto* types = preset->config.option<ConfigOptionStrings>("filament_type"); types && !types->values.empty())
                filament.material = types->values.front();
        result.filaments.push_back(std::move(filament));
    }
    return result;
}

CommandResult OrcaWorkspaceAdapter::start_slice(std::optional<PlateId> plate, bool preempt)
{
    wxASSERT(wxIsMainThread());
    // Nothing in Orca records who started a run, so a slice in flight may be
    // the person's. Taking it over is the caller's decision to state.
    if (m_plater.is_background_process_slicing() && !preempt)
        return CommandResult::failure(WorkspaceError::Busy,
                                      "A slice is already running. Wait for it, or ask again with preempt.");

    PartPlateList& plates = m_plater.get_partplate_list();
    if (plate) {
        if (plate->session() != m_session)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate belongs to a project that is no longer open");
        int index = -1;
        for (int candidate = 0; candidate < plates.get_plate_count(); ++candidate) {
            const PartPlate* found = plates.get_plate(candidate);
            if (found != nullptr && found->id().id == plate->value()) {
                index = candidate;
                break;
            }
        }
        if (index < 0)
            return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
        PartPlate* target = plates.get_plate(index);
        if (target == nullptr)
            return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
        // Orca slices the current plate, so selecting it is part of starting
        // the run, exactly as the header's own Slice button does it.
        if (plates.get_curr_plate_index() != index)
            m_plater.select_plate(index);
        // A plate OrcaSlicer's check refused is still asked: the slice event
        // runs the check again and records why. One that does not fit the
        // printer has its Slice button off in OrcaSlicer, so nothing is
        // posted; the refusal is recorded in the words of OrcaSlicer's notice.
        if (!target->can_slice() && !target->is_apply_result_invalid()) {
            on_slice_declined();
            return CommandResult::success();
        }
    }

    // The toolbar events are Orca's own entry points: they carry the slice-all
    // bookkeeping, the Prepare switch, and the auto-preview rule with them.
    // Posting them keeps one owner for slicing rather than a second path.
    m_plater.exit_gizmo();
    m_plater.update(true, true);
    SimpleEvent event(plate ? EVT_GLTOOLBAR_SLICE_PLATE : EVT_GLTOOLBAR_SLICE_ALL);
    m_plater.GetEventHandler()->ProcessEvent(event);
    return CommandResult::success();
}

std::vector<PrinterDevice> OrcaWorkspaceAdapter::printers() const
{
    wxASSERT(wxIsMainThread());
    std::vector<PrinterDevice> result;
    // The fork's own discovery, asked to include what it normally hides:
    // setup offers printers you can connect to now, but "what printers do I
    // have" is a different question and an unreachable one is a real answer.
    for (const PrinterSetup::DiscoveredPrinter& found : PrinterSetup::discover_printers(true)) {
        PrinterDevice device;
        device.id         = found.stable_id;
        device.name       = found.name;
        device.model      = found.device_model_id;
        device.connection = found.connection;
        device.activity   = found.activity == PrinterSetup::PrinterActivity::Printing ? "printing" :
                            found.activity == PrinterSetup::PrinterActivity::Idle     ? "idle" :
                                                                                        "offline";
        // The device reports a float; 0.4f is not 0.4 once widened.
        if (found.nozzle_diameter > 0.)
            device.nozzle_diameter = std::round(found.nozzle_diameter * 100.) / 100.;
        if (found.observed_at_ms != 0)
            device.observed_at_ms = found.observed_at_ms;
        for (const auto& spool : found.spools) {
            device.materials.push_back(spool.name);
            device.material_types.push_back(spool.material);
        }
        device.selected = found.selected;
        if (found.progress_percent >= 0)
            device.progress_percent = found.progress_percent;
        device.job                = found.job;
        device.nozzle_temperature = found.nozzle_temperature;
        device.bed_temperature    = found.bed_temperature;
        result.push_back(std::move(device));
    }
    return result;
}

PresetListResult OrcaWorkspaceAdapter::list_presets(const PresetQuery& query) const
{
    wxASSERT(wxIsMainThread());
    PresetListResult result;
    // Const throughout: the non-const accessors on a collection can select a
    // preset when the current index is out of range, and update_compatible can
    // both rewrite every compatibility flag and change the selection.
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return result;
    const PresetCollection& collection = query.kind == PresetKind::Printer ? presets->printers :
                                         query.kind == PresetKind::Filament ?
                                             static_cast<const PresetCollection&>(presets->filaments) :
                                             static_cast<const PresetCollection&>(presets->prints);
    // What a project actually prints with is the bundle's per-extruder choice,
    // not the filament tab's selection; for the other kinds they are the same.
    const std::string selected = query.kind == PresetKind::Filament && !presets->filament_presets.empty() ?
                                     presets->filament_presets.front() :
                                     collection.get_selected_preset_name();

    const std::string needle = ascii_lower(query.text);
    std::size_t       skipped = 0, offset = 0;
    if (!query.cursor.empty())
        std::from_chars(query.cursor.data(), query.cursor.data() + query.cursor.size(), offset);
    for (const Preset& preset : collection) {
        // A hidden preset is one this installation does not offer; a default
        // is Orca's placeholder, not something anyone prints with.
        if (!preset.is_visible || preset.is_default)
            continue;
        if (query.compatible_only && !preset.is_compatible)
            continue;
        const std::string label = preset.alias.empty() ? preset.name : preset.alias;
        if (!needle.empty() && ascii_lower(preset.name).find(needle) == std::string::npos &&
            ascii_lower(label).find(needle) == std::string::npos)
            continue;
        ++result.total;
        if (skipped++ < offset)
            continue;
        if (result.items.size() >= query.limit) {
            result.truncated = true;
            continue; // keep counting, so the total is the whole answer
        }
        result.items.push_back({preset.name, label, preset.vendor != nullptr ? preset.vendor->name : std::string(),
                                preset.is_system, preset.name == selected, preset.is_compatible});
    }
    if (result.truncated)
        result.next_cursor = std::to_string(offset + result.items.size());
    return result;
}

SliceReport OrcaWorkspaceAdapter::slice_report(PlateId plate, const SliceReportRequest& request) const
{
    wxASSERT(wxIsMainThread());
    SliceReport report;
    if (plate.session() != m_session)
        return report;
    PartPlateList& plates = m_plater.get_partplate_list();
    PartPlate*     target = nullptr;
    for (int index = 0; index < plates.get_plate_count(); ++index)
        if (PartPlate* candidate = plates.get_plate(index);
            candidate != nullptr && candidate->id().id == plate.value())
            target = candidate;
    // A slice in flight is not this plate's result yet, and an invalidated one
    // is not a report: both answer "no current slice".
    if (target == nullptr || !target->is_slice_result_valid() || m_plater.is_background_process_slicing() ||
        target->get_slice_result() == nullptr)
        return report;

    const GCodeProcessorResult& result     = *target->get_slice_result();
    const auto&                 statistics = result.print_statistics;
    const auto&                 mode = statistics.modes[static_cast<std::size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    report.valid                = true;
    report.print_time_seconds   = mode.time > 0.f ? static_cast<std::uint32_t>(mode.time) : 0u;
    report.prepare_time_seconds = mode.prepare_time > 0.f ? static_cast<std::uint32_t>(mode.prepare_time) : 0u;
    report.filament_changes     = statistics.total_filament_changes;
    report.extruder_changes     = statistics.total_extruder_changes;

    const auto volume_of = [](const std::map<std::size_t, double>& volumes, std::size_t extruder) {
        const auto found = volumes.find(extruder);
        return found == volumes.end() ? 0.0 : found->second;
    };
    bool every_filament_priced = true;
    for (const auto& [extruder, volume] : statistics.total_volumes_per_extruder) {
        SliceFilamentUse use;
        use.extruder = extruder;
        // Without a diameter or a density the length and the weight would be
        // invented, so they stay zero and the row says only what is known.
        if (extruder < result.filament_diameters.size() && result.filament_diameters[extruder] > 0.f)
            use.length_mm = volume / (PI * sqr(0.5 * double(result.filament_diameters[extruder])));
        if (extruder < result.filament_densities.size())
            use.grams = volume * result.filament_densities[extruder] * 0.001;
        if (extruder < result.filament_costs.size() && result.filament_costs[extruder] > 0.f) {
            use.cost     = use.grams * result.filament_costs[extruder] * 0.001;
            use.has_cost = true;
        } else {
            every_filament_priced = false;
        }
        use.flushed_mm3 = volume_of(statistics.flush_per_filament, extruder);
        use.tower_mm3   = volume_of(statistics.wipe_tower_volumes_per_extruder, extruder);
        use.support_mm3 = volume_of(statistics.support_volumes_per_extruder, extruder);
        report.total_grams += use.grams;
        report.total_cost += use.cost;
        report.filaments.push_back(use);
    }
    // Money is the cost of the whole print or it is not shown, the same rule
    // the setup card applies to the estimate.
    report.has_cost = every_filament_priced && report.total_cost > 0.0;
    if (!report.has_cost)
        report.total_cost = 0.0;

    // Orca's own plate-level warnings, with its own words for them, shown
    // where Orca's send dialog shows them: never the bed-temperature one, the
    // timelapse ones only for a print that records a timelapse, and the
    // traditional-timelapse one once although the processor records it twice
    // (1000C003 is its copy for older A-series firmware).
    for (const GCodeProcessorResult::SliceWarning& warning : result.warnings) {
        if (warning.msg == BED_TEMP_TOO_HIGH_THAN_FILAMENT || warning.error_code == "1000C003")
            continue;
        auto mutable_warning = warning;
        const std::string text = Plater::get_slice_warning_string(mutable_warning).ToUTF8().data();
        if (text.empty())
            continue; // Orca deliberately has no words for this one
        const bool timelapse = warning.msg == NOT_SUPPORT_TRADITIONAL_TIMELAPSE || warning.msg == SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER;
        report.findings.push_back({warning.error_code, text, warning.level >= 2 && !timelapse, {}, timelapse ? "timelapse" : ""});
    }

    // The step warnings, which live on the print and its objects rather than on
    // the result. Only the current ones: invalidating a step leaves its
    // warnings behind, marked stale.
    if (const Print* print = target->fff_print(); print != nullptr) {
        const auto collect = [&report](const PrintStateBase::StateWithWarnings& state, const std::string& object) {
            for (const PrintStateBase::Warning& warning : state.warnings)
                if (warning.current && report.findings.size() < 32)
                    report.findings.push_back({{}, warning.message,
                                               warning.level == PrintStateBase::WarningLevel::CRITICAL, object});
        };
        for (int step = 0; step < psCount; ++step)
            collect(print->step_state_with_warnings(static_cast<PrintStep>(step)), {});
        for (const PrintObject* object : print->objects()) {
            const std::string name = object->model_object() != nullptr ? object->model_object()->name : std::string();
            for (int step = 0; step < posCount; ++step)
                collect(object->step_state_with_warnings(static_cast<PrintObjectStep>(step)), name);
        }
        report.conflict = print->get_conflict_string();
    }

    // The result's own toolpath_outside flag is only ever written by a 3mf, so
    // it would be stale on a plate just sliced. Ask the build volume instead,
    // which is what the canvas does before it draws the same warning.
    const BoundingBoxf3 paths = extrusion_bounds(result);
    if (paths.defined)
        report.toolpath_outside = !m_plater.build_volume().all_paths_inside(result, paths);
    check_slice(*target, request, report);
    return report;
}

ProjectDetails OrcaWorkspaceAdapter::project_details() const
{
    wxASSERT(wxIsMainThread());
    ProjectDetails result;
    const Model& model = m_plater.model();
    // The fields ProjectPanel::on_reload reads.
    if (model.design_info)
        result.designer = model.design_info->Designer;
    if (model.model_info) {
        result.title       = model.model_info->model_name;
        result.description = model.model_info->description;
        result.license     = model.model_info->license;
        result.copyright   = model.model_info->copyright;
        result.origin      = model.model_info->origin;
    }
    if (model.profile_info) {
        result.profile_title       = model.profile_info->ProfileTile;
        result.profile_description = model.profile_info->ProfileDescription;
    }
    // A read-only walk: ProjectPanel::Reload would create the default folders.
    // JusPrin's own state and Orca's thumbnail cache are not attachments.
    const boost::filesystem::path root(auxiliary_data_dir());
    boost::system::error_code error;
    for (boost::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!boost::filesystem::is_regular_file(it->path(), error))
            continue;
        const boost::filesystem::path relative = it->path().lexically_relative(root);
        const std::string first = relative.begin()->string();
        if (first == "JusPrin" || first == ".thumbnails")
            continue;
        result.attachments.push_back({relative.generic_string(), first, boost::filesystem::file_size(it->path(), error)});
    }
    std::sort(result.attachments.begin(), result.attachments.end(),
              [](const ProjectAttachment& a, const ProjectAttachment& b) { return a.id < b.id; });
    if (result.attachments.size() > kAttachmentLimit) {
        result.attachments.resize(kAttachmentLimit);
        result.attachments_truncated = true;
    }
    // Asked with saved=false it is a query; saved=true would record a save.
    result.backup_current = m_plater.up_to_date(false, true);
    return result;
}

namespace {
// Marks a tool's own open or import, whose loads it reports itself.
class ToolLoad
{
public:
    explicit ToolLoad(int& count) : m_count(count) { ++m_count; }
    ~ToolLoad() { --m_count; }
    ToolLoad(const ToolLoad&)            = delete;
    ToolLoad& operator=(const ToolLoad&) = delete;

private:
    int& m_count;
};

// What the tool's own safety net answered outside the load itself, as a
// record of its own so the Agent reads it with the rest.
void add_outside_messages(LoadReport& report, std::vector<OrcaMessage> messages)
{
    if (messages.empty())
        return;
    FileLoadRecord outside;
    outside.messages = std::move(messages);
    report.loads.push_back(std::move(outside));
}
} // namespace

CommandResult OrcaWorkspaceAdapter::open_project(const ProjectOpenRequest& request, LoadReport& report)
{
    wxASSERT(wxIsMainThread());
    const boost::filesystem::path path(request.path);
    const std::string extension = boost::algorithm::to_lower_copy(path.extension().string());
    if (!request.new_project) {
        static const std::set<std::string> openable{".3mf", ".stl", ".obj", ".step", ".stp", ".amf", ".drc"};
        boost::system::error_code error;
        if (!path.is_absolute() || !boost::filesystem::is_regular_file(path, error))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Open an absolute path to a file that exists");
        if (openable.count(extension) == 0)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "OrcaSlicer opens .3mf, .stl, .obj, .step, .amf and .drc files");
    }
    const ToolLoad tool_load(m_tool_loads);
    // Inside the load, the file listener answers and records; this catches
    // anything Orca asks around it.
    ScopedLeastChangeAnswers outside;

    // Orca's new project releases the open one through the autosave hook,
    // which saves it, edited presets included, or cancels when it cannot. A
    // model file opens as a new project named after it, as New and Import do.
    const bool model_file = !request.new_project && extension != ".3mf";
    const bool released   = m_plater.new_project(true, true, model_file ? from_u8(path.stem().string()) : wxString()) != wxID_CANCEL;
    bool       loaded     = released;
    // Orca asks about edited presets in UnsavedChangesDialog; once saved
    // they are dropped, so it is never asked.
    for (Preset::Type type : {Preset::TYPE_PRINTER, Preset::TYPE_PRINT, Preset::TYPE_FILAMENT})
        if (released) drop_unsaved_edits(wxGetApp().get_tab(type));
    if (released && extension == ".3mf") {
        // "<loadall>" is Orca's own way to open a project with its settings
        // without asking how ("<silence>" would also leave the project
        // without its file name); geometry only is the answer the person
        // would otherwise give in ProjectDropDialog, set for this one load.
        if (request.load_project_settings) {
            m_plater.load_project(from_u8(request.path), "<loadall>");
        } else {
            AppConfig&        config   = *wxGetApp().app_config;
            const std::string previous = config.get(SETTING_PROJECT_LOAD_BEHAVIOUR);
            config.set(SETTING_PROJECT_LOAD_BEHAVIOUR, OPTION_PROJECT_LOAD_BEHAVIOUR_LOAD_GEOMETRY);
            m_plater.load_project(from_u8(request.path), "-");
            config.set(SETTING_PROJECT_LOAD_BEHAVIOUR, previous);
        }
        boost::system::error_code error;
        loaded = boost::filesystem::equivalent(into_path(m_plater.get_project_filename(".3mf")), path, error);
    } else if (released && model_file) {
        LoadStrategy strategy = LoadStrategy::LoadModel;
        if (request.units == UnitChoice::Inches)
            strategy = strategy | LoadStrategy::ImperialUnits;
        loaded = !m_plater.load_files(std::vector<boost::filesystem::path>{path}, strategy).empty();
    }

    report = take_load_report(/*started_by_agent=*/true);
    // A model file opens into the new project made just before its load.
    report.project_opened = true;
    add_outside_messages(report, outside.take_messages());
    if (!released)
        return CommandResult::failure(WorkspaceError::Failed, "The open project could not be saved, so it stays open");
    if (!loaded)
        return CommandResult::failure(WorkspaceError::Failed, "OrcaSlicer did not open that file");
    return CommandResult::success();
}

std::string OrcaWorkspaceAdapter::auxiliary_data_dir() const
{
    wxASSERT(wxIsMainThread());
    // The model's auxiliary temp dir is extracted from and repacked into the
    // project 3MF by Orca's own load/store paths, and its path changes when a
    // project is loaded or created — resolve it fresh on every call.
    return m_plater.model().get_auxiliary_file_temp_path();
}

CommandResult OrcaWorkspaceAdapter::export_project_archive(const std::string& file_path)
{
    wxASSERT(wxIsMainThread());
    // Exporting renders plate thumbnails; before the canvas has initialized
    // its GL state (early startup), those render calls go through unloaded
    // function pointers and crash. Refuse honestly instead.
    GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    if (canvas == nullptr || !canvas->is_initialized())
        return CommandResult::failure(WorkspaceError::Busy,
                                      "The canvas is not ready to render the archive's thumbnails yet");
    // SkipAuxiliary keeps consumer files out of the archive; the remaining
    // strategy matches an ordinary project save.
    const SaveStrategy strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                                  SaveStrategy::SkipAuxiliary;
    if (m_plater.export_3mf(into_path(from_u8(file_path)), strategy) < 0)
        return CommandResult::failure(WorkspaceError::Failed, "The project archive could not be written");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::import_objects(const ImportRequest& request, LoadReport& report,
                                                   std::vector<ObjectId>& added)
{
    wxASSERT(wxIsMainThread());
    const boost::filesystem::path path(request.path);
    boost::system::error_code error;
    if (!path.is_absolute() || !boost::filesystem::is_regular_file(path, error) || boost::filesystem::file_size(path, error) == 0)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The model file does not exist");
    static const std::set<std::string> importable{".3mf", ".stl", ".obj", ".step", ".stp", ".amf", ".drc"};
    if (importable.count(boost::algorithm::to_lower_copy(path.extension().string())) == 0)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "OrcaSlicer imports .stl, .obj, .step, .amf, .drc and .3mf files");
    PartPlateList& plates = m_plater.get_partplate_list();
    int plate = -1;
    if (request.plate) {
        for (int index = 0; index < plates.get_plate_count(); ++index)
            if (request.plate->session() == m_session && plates.get_plate(index)->id().id == request.plate->value())
                plate = index;
        if (plate < 0)
            return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
    }

    std::vector<size_t> indices;
    const ToolLoad tool_load(m_tool_loads);
    ScopedLeastChangeAnswers outside;
    {
        // One coalesced, undoable change. LoadModel is the additive,
        // geometry-only strategy Import uses; a new object lands on the
        // current plate, so the target plate is selected first.
        ProjectStateTransaction transaction = m_plater.project_state_transaction();
        m_plater.take_snapshot("Import model");
        if (plate >= 0)
            m_plater.select_plate(plate);
        LoadStrategy strategy = LoadStrategy::LoadModel;
        if (request.units == UnitChoice::Inches)
            strategy = strategy | LoadStrategy::ImperialUnits;
        indices = m_plater.load_files(std::vector<boost::filesystem::path>{path}, strategy);
    }
    report = take_load_report(/*started_by_agent=*/true);
    add_outside_messages(report, outside.take_messages());
    if (indices.empty())
        return CommandResult::failure(WorkspaceError::Failed, "OrcaSlicer did not import that file");
    for (size_t index : indices) {
        const ObjectId id(m_session, m_plater.model().objects[index]->id().id);
        m_known_object_ids.insert(id.value());
        added.push_back(id);
    }
    return CommandResult::success(added.front());
}

CommandResult OrcaWorkspaceAdapter::delete_items(const std::vector<DeleteItem>& items)
{
    wxASSERT(wxIsMainThread());
    Model&         model  = m_plater.model();
    PartPlateList& plates = m_plater.get_partplate_list();
    std::vector<ItemForDelete> subitems;
    std::vector<std::size_t>   objects;
    std::vector<int>           plate_indices;
    std::map<std::size_t, std::pair<std::size_t, std::size_t>> removing; // object -> (model parts, instances)

    // Everything is checked first. Orca reports some refusals from these
    // paths with an error it shows after the call returns, which no answer
    // can reach, so the same conditions are refused here.
    for (const DeleteItem& item : items) {
        if (item.kind == DeleteItem::Kind::Plate) {
            int index = -1;
            for (int candidate = 0; candidate < plates.get_plate_count(); ++candidate)
                if (item.plate.session() == m_session && plates.get_plate(candidate)->id().id == item.plate.value())
                    index = candidate;
            if (index < 0)
                return CommandResult::failure(WorkspaceError::NotFound, "That plate is not in the open project. Read workspace_inspect for the plate ids.");
            plate_indices.push_back(index);
            continue;
        }
        const auto resolved = resolve(item.object);
        if (!resolved)
            return id_error(item.object);
        const ModelObject& object = *model.objects[resolved->index];
        if (item.kind == DeleteItem::Kind::Object) {
            objects.push_back(resolved->index);
        } else if (item.kind == DeleteItem::Kind::Part) {
            const auto volume = std::find_if(object.volumes.begin(), object.volumes.end(),
                                             [&](const ModelVolume* v) { return v->id().id == item.part; });
            if (volume == object.volumes.end())
                return CommandResult::failure(WorkspaceError::NotFound, "That part is not in " + object.name);
            if (object.is_cut() && ((*volume)->is_model_part() || (*volume)->is_negative_volume()))
                return CommandResult::failure(WorkspaceError::Refused,
                                              "OrcaSlicer does not delete the solid parts of a cut object");
            if ((*volume)->is_model_part())
                ++removing[resolved->index].first;
            subitems.emplace_back(itVolume, int(resolved->index), int(volume - object.volumes.begin()));
        } else {
            if (item.instance >= object.instances.size())
                return CommandResult::failure(WorkspaceError::InvalidArgument, object.name + " has no such copy");
            ++removing[resolved->index].second;
            subitems.emplace_back(itInstance, int(resolved->index), int(item.instance));
        }
    }
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
    // Parts and copies of an object that goes whole need no separate step.
    subitems.erase(std::remove_if(subitems.begin(), subitems.end(), [&](const ItemForDelete& item) {
                       return std::binary_search(objects.begin(), objects.end(), std::size_t(item.obj_idx));
                   }),
                   subitems.end());
    for (const auto& [index, counts] : removing) {
        if (std::binary_search(objects.begin(), objects.end(), index))
            continue;
        const ModelObject& object = *model.objects[index];
        const auto parts = std::count_if(object.volumes.begin(), object.volumes.end(), [](const ModelVolume* v) { return v->is_model_part(); });
        if (counts.first >= std::size_t(parts))
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "That would remove every solid part of " + object.name + "; delete the object instead");
        if (counts.second >= object.instances.size())
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "That would remove every copy of " + object.name + "; delete the object instead");
    }
    std::sort(plate_indices.begin(), plate_indices.end());
    plate_indices.erase(std::unique(plate_indices.begin(), plate_indices.end()), plate_indices.end());
    if (!plate_indices.empty() && plate_indices.size() >= std::size_t(plates.get_plate_count()))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "A project keeps at least one plate");

    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        Plater::TakeSnapshot snapshot(&m_plater, "Delete items");
        // The card named a cut object's broken correspondence already. Orca
        // asks before deleting a cut object and, on Delete, invalidates the
        // cut; doing that first is the same change, with nothing left to ask.
        for (std::size_t index : objects)
            if (model.objects[index]->is_cut())
                wxGetApp().obj_list()->invalidate_cut_info_for_object(int(index));
        // Nothing else should ask. If Orca does, the least change is taken
        // and logged, never shown in the middle of a tool call.
        ScopedLeastChangeAnswers unexpected;
        if (!subitems.empty()) {
            // The object list deletes in reverse order, so it gets them sorted.
            std::sort(subitems.begin(), subitems.end(), [](const ItemForDelete& a, const ItemForDelete& b) {
                return std::tie(a.obj_idx, a.type, a.sub_obj_idx) < std::tie(b.obj_idx, b.type, b.sub_obj_idx);
            });
            wxGetApp().obj_list()->delete_from_model_and_list(subitems);
        }
        for (auto index = objects.rbegin(); index != objects.rend(); ++index)
            if (!m_plater.delete_object(*index))
                // Orca refuses only an index out of range, or a cut object
                // whose question is declined; the cut info is cleared above.
                throw std::logic_error("OrcaSlicer did not delete an object the call checked");
        // A deleted plate's objects move to another plate, as in Orca.
        for (auto index = plate_indices.rbegin(); index != plate_indices.rend(); ++index)
            m_plater.delete_plate(*index);
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects | ProjectStateChangeReason::Plates);
        for (const OrcaMessage& message : unexpected.messages())
            BOOST_LOG_TRIVIAL(warning) << "delete_items: OrcaSlicer asked \"" << message.title << "\" (" << message.text
                                       << "), answered " << message.answer;
    }
    return CommandResult::success();
}

WorkspaceSubscription OrcaWorkspaceAdapter::subscribe(WorkspaceChangedCallback callback)
{
    wxASSERT(wxIsMainThread());
    return m_changes.subscribe(std::move(callback));
}

std::optional<OrcaWorkspaceAdapter::ResolvedObject> OrcaWorkspaceAdapter::resolve(ObjectId id) const
{
    if (!id || id.session() != m_session)
        return std::nullopt;
    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index)
        if (objects[index]->id().id == id.value())
            return ResolvedObject{index};
    return std::nullopt;
}

CommandResult OrcaWorkspaceAdapter::id_error(ObjectId id) const
{
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Object ID is invalid");
    if (id.session() != m_session)
        return CommandResult::failure(WorkspaceError::StaleId,
                                      "That object id is from a project that is no longer open. Read workspace_inspect again.");
    if (m_known_object_ids.count(id.value()) > 0)
        return CommandResult::failure(WorkspaceError::StaleId,
                                      "That object was removed from the project since it was read. Read workspace_inspect again.");
    return CommandResult::failure(WorkspaceError::NotFound,
                                  "That object is not in the open project. Read workspace_inspect for the current ids.");
}

void OrcaWorkspaceAdapter::on_project_state_changed(const ProjectStateChanged& change)
{
    wxASSERT(wxIsMainThread());
    const auto touched = [&](ProjectStateChangeReason reason) {
        return (static_cast<std::uint32_t>(change.reasons) & static_cast<std::uint32_t>(reason)) != 0;
    };
    if (change.project_replaced) {
        m_session = ProjectSessionId(change.project_session);
        m_known_object_ids.clear();
        // Plate ids restart with the project; what the last one's slices
        // failed with says nothing about this one's.
        m_slice_failures.clear();
        m_failed_run_plate.reset();
        // Jobs belong to the project that started them; the new project's
        // action ids, which name them, start again.
        m_jobs.clear();
        // A new or opened project starts its own history and presets; nothing
        // in it is an edit.
        remember_history();
        m_first_model_pending = m_plater.model().objects.empty();
        record_settings_edits(/*report=*/false);
    } else {
        record_history_edits();
        // Undo and redo restore the configuration they saved; that is part of
        // the undo already recorded, so the baseline just follows it.
        if (touched(ProjectStateChangeReason::Settings))
            record_settings_edits(/*report=*/!m_plater.inside_snapshot_capture());
    }
    remember_current_ids();

    const WorkspaceChangeReasons reasons = workspace_reasons(change.reasons);
    // An undo step alone is no workspace change, and must not
    // clear the reason a slice about to be invalidated will report.
    if (reasons == WorkspaceChangeReasons::None)
        return;
    // Orca invalidates the slice as a consequence of this change and only then
    // posts EVT_SLICE_STATUS_CHANGED, so the reason is still the newest one
    // when the slice watcher runs. Naming only the actions a person would
    // recognise; anything else leaves the card saying nothing rather than
    // attributing the loss to something the reader did not do.
    if (touched(ProjectStateChangeReason::Transform))     m_last_change_reason = "you moved the object";
    else if (touched(ProjectStateChangeReason::Objects))  m_last_change_reason = "the objects changed";
    else if (touched(ProjectStateChangeReason::Settings)) m_last_change_reason = "a setting changed";
    else                                                  m_last_change_reason.clear();

    publish_change(reasons);
}

void OrcaWorkspaceAdapter::remember_history()
{
    const UndoRedo::Stack&                  stack     = m_plater.undo_redo_stack_main();
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    // The uncaptured topmost placeholder holds the timestamp the next step
    // will be recorded under.
    m_first_unreported_step = snapshots.empty()             ? 0 :
                              snapshots.back().is_topmost() ? snapshots.back().timestamp :
                                                              snapshots.back().timestamp + 1;
    m_seen_active_step = stack.active_snapshot_time();
}

void OrcaWorkspaceAdapter::record_history_edits()
{
    const UndoRedo::Stack&                  stack     = m_plater.undo_redo_stack_main();
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    bool recorded = false;
    // Orca takes a load's undo step before the objects arrive, so the step
    // may be seen while the project is still empty or after: either way it
    // belongs to the project's making.
    const bool making_project = m_first_model_pending;
    if (!m_plater.model().objects.empty())
        m_first_model_pending = false;
    for (const UndoRedo::Snapshot& snapshot : snapshots) {
        if (snapshot.timestamp < m_first_unreported_step || snapshot.is_topmost())
            continue;
        recorded = true;
        // A project separator is a project boundary, reported as one.
        if (!making_project && snapshot.snapshot_data.snapshot_type != UndoRedo::SnapshotType::ProjectSeparator &&
            UndoRedo::snapshot_modifies_project(snapshot))
            publish_edit({EditKind::Step, EditActor::Person, snapshot.name});
    }
    const std::size_t active = stack.active_snapshot_time();
    if (!recorded && active != m_seen_active_step) {
        // The active position moved without a new step: undo or redo. The
        // steps between the two positions were undone or redone; the first
        // real one names the move.
        const std::size_t from = std::min(active, m_seen_active_step);
        const std::size_t to   = std::max(active, m_seen_active_step);
        const auto named = std::find_if(snapshots.begin(), snapshots.end(), [from, to](const UndoRedo::Snapshot& snapshot) {
            return snapshot.timestamp >= from && snapshot.timestamp < to && !snapshot.is_topmost() &&
                   UndoRedo::snapshot_modifies_project(snapshot);
        });
        if (named != snapshots.end())
            publish_edit({active < m_seen_active_step ? EditKind::Undo : EditKind::Redo, EditActor::Person, named->name});
    }
    remember_history();
}

void OrcaWorkspaceAdapter::record_settings_edits(bool report)
{
    PresetBundle& presets = *wxGetApp().preset_bundle;
    const std::vector<std::pair<const PresetCollection*, Preset::Type>> collections{
        {static_cast<const PresetCollection*>(&presets.prints), Preset::TYPE_PRINT},
        {static_cast<const PresetCollection*>(&presets.filaments), Preset::TYPE_FILAMENT},
        {static_cast<const PresetCollection*>(&presets.printers), Preset::TYPE_PRINTER}};
    std::vector<PresetReading> now;
    Search::OptionsSearcher& searcher = m_plater.sidebar().get_searcher();
    for (const auto& [collection, type] : collections)
        now.push_back({collection->get_edited_preset().name,
                       preset_deltas_of(*collection, collection->current_dirty_options(type != Preset::TYPE_PRINT),
                                        type, searcher)});
    if (report && m_presets.size() == now.size())
        for (std::size_t index = 0; index < now.size(); ++index) {
            if (m_presets[index].name != now[index].name) {
                WorkspaceEdit edit{EditKind::Preset, EditActor::Person, now[index].name};
                edit.location = collections[index].second == Preset::TYPE_PRINTER ? "printer"
                              : collections[index].second == Preset::TYPE_FILAMENT ? "filament" : "process";
                publish_edit(std::move(edit));
                continue;
            }
            for (const WorkspaceEdit& edit : setting_edits(m_presets[index].deltas, now[index].deltas,
                                                           *collections[index].first))
                publish_edit(edit);
        }
    m_presets = std::move(now);
}

void OrcaWorkspaceAdapter::publish_change(WorkspaceChangeReasons reasons)
{
    m_changes.merge(reasons);
    WorkspaceChangeDelivery delivery = m_changes.commit(m_session);
    delivery.deliver();
}

void OrcaWorkspaceAdapter::remember_current_ids() const
{
    for (const ModelObject* object : m_plater.model().objects)
        m_known_object_ids.insert(object->id().id);
}

namespace {

bool fff_printer()
{
    const PresetBundle* bundle = wxGetApp().preset_bundle;
    return bundle != nullptr && bundle->printers.get_edited_preset().printer_technology() == ptFFF;
}

bool preset_scope(SettingsScope scope) { return scope == SettingsScope::Filament || scope == SettingsScope::Printer; }

// The preset a settings call reads and writes. `selected` is the one Orca is
// editing now: its values include unsaved edits, and its tab is where a change
// goes. The process preset in use is always that one.
struct PresetOwner
{
    PresetCollection* collection{nullptr};
    const Preset*     preset{nullptr};
    bool              selected{false};
};

PresetOwner preset_owner(const SettingsTarget& target)
{
    PresetOwner owner;
    owner.collection = &scope_collection(target.scope);
    if (!preset_scope(target.scope)) {
        owner.preset   = &owner.collection->get_selected_preset();
        owner.selected = true;
        return owner;
    }
    owner.preset   = scope_preset(target.scope, target.preset);
    owner.selected = owner.preset != nullptr && owner.collection->get_selected_preset_name() == owner.preset->name;
    return owner;
}

// The values a call reads: the edited copy of the preset Orca is editing, or
// the preset as saved.
const DynamicPrintConfig& owner_config(const PresetOwner& owner)
{
    return owner.selected ? owner.collection->get_edited_preset().config : owner.preset->config;
}

SettingIssue unavailable(SettingsScope scope)
{
    return preset_scope(scope) ? SettingIssue{"", "workspace_unavailable", "No FFF printer is selected. Ask the person to select one."} :
                                 SettingIssue{"", "workspace_unavailable", "No FFF process preset is selected. Ask the person to select one."};
}

// A preset the call named that is not there. The names that are: the
// project's filaments, or the printer in use.
SettingIssue unknown_preset(const SettingsTarget& target)
{
    SettingIssue issue{"", "unknown_preset", std::string("No ") + scope_name(target.scope) + " preset is named \"" + target.preset + "\"."};
    const PresetBundle& bundle = *wxGetApp().preset_bundle;
    if (target.scope == SettingsScope::Printer)
        issue.suggestions.push_back(bundle.printers.get_selected_preset_name());
    else
        for (const std::string& name : bundle.filament_presets)
            if (std::find(issue.suggestions.begin(), issue.suggestions.end(), name) == issue.suggestions.end())
                issue.suggestions.push_back(name);
    return issue;
}

// Profile files share a directory, so names that differ only in case are the
// same file on Windows and macOS.
bool preset_name_in_use(const PresetCollection& collection, const std::string& name)
{
    return std::any_of(collection.begin(), collection.end(), [&name](const Preset& preset) { return boost::iequals(preset.name, name); });
}

// SavePresetDialog::Item::update's reserved names.
bool preset_name_reserved(const PresetCollection& collection, const std::string& name)
{
    return name.find(PresetCollection::get_suffix_modified()) != std::string::npos || name == "Default Setting" ||
           name == PresetBundle::ORCA_DEFAULT_FILAMENT_PLACEHOLDER || name == "Default Printer" ||
           collection.get_preset_name_by_alias(name) != name;
}

std::string copy_name(const PresetCollection& collection, const Preset& preset)
{
    return Printers::copy_name(preset.name, [&collection](const std::string& name) { return preset_name_in_use(collection, name); });
}

// The project slot a filament preset is used in, to edit it there as the
// sidebar's edit button does.
std::optional<std::size_t> filament_slot(const std::string& name)
{
    const auto& slots = wxGetApp().preset_bundle->filament_presets;
    const auto  found = std::find(slots.begin(), slots.end(), name);
    return found == slots.end() ? std::nullopt : std::optional<std::size_t>(std::size_t(found - slots.begin()));
}

// persistAs, checked where Orca's Save would check it, and against what the
// save would have to do on the way.
void check_persist(const SettingsPatch& patch, const PresetOwner& owner, SettingsPreview& result)
{
    const SettingsScope scope = patch.target.scope;
    const Preset&       preset = *owner.preset;
    if (!patch.persist_as) {
        // An unsaved change lives in the edited copy of the preset Orca is
        // editing; any other preset changes only by being saved.
        if (!owner.selected)
            result.issues.push_back({"", "not_selected", "\"" + preset.name + "\" is not the " + scope_name(scope) +
                                                             " preset being edited, so a change to it is saved: pass persistAs."});
        return;
    }
    const std::string&      name       = *patch.persist_as;
    const PresetCollection& collection = *owner.collection;
    if (name == preset.name) {
        if (!preset.can_overwrite()) {
            const std::string copy = copy_name(collection, preset);
            result.issues.push_back({"", "read_only_preset",
                                     "\"" + preset.name + "\" comes with OrcaSlicer and cannot be overwritten. Save the change "
                                     "as a copy: pass persistAs \"" + copy + "\".", {}, {copy}});
        }
    } else if (preset_name_in_use(collection, name)) {
        result.issues.push_back({"", "name_taken", "Another " + std::string(scope_name(scope)) + " preset is already named \"" + name + "\".",
                                 {}, {copy_name(collection, preset)}});
    } else if (const auto problem = Printers::check_printer_name(
                   name, [&collection](const std::string& candidate) { return preset_name_reserved(collection, candidate); },
                   [](const std::string&) { return false; });
               problem != Printers::NameProblem::None) {
        result.issues.push_back({"", "invalid_preset_name",
                                 problem == Printers::NameProblem::Empty ? "A preset name cannot be empty." :
                                 problem == Printers::NameProblem::EdgeSpace ? "A preset name cannot start or end with a space." :
                                 problem == Printers::NameProblem::IllegalCharacter ?
                                     std::string("A preset name cannot contain any of ") + Printers::kIllegalNameCharacters :
                                     "That name is reserved.",
                                 {}, {copy_name(collection, preset)}});
    }
    if (!owner.selected) {
        // A printer that is not in use is saved in place, as a nozzle change
        // is. A copy of it would be a second printer made behind the project's
        // back.
        if (scope == SettingsScope::Printer && name != preset.name)
            result.issues.push_back({"", "not_selected", "A copy is saved only of the printer in use; save \"" + preset.name +
                                                             "\" under its own name."});
        // Another filament is edited where the sidebar's edit button edits
        // it, in its slot, which drops the unsaved edits of the one edited.
        if (scope == SettingsScope::Filament) {
            if (!filament_slot(preset.name))
                result.issues.push_back({"", "not_in_project", "\"" + preset.name + "\" is not one of the project's filaments."});
            else if (collection.current_is_dirty())
                result.warnings.push_back({"", "discards_unsaved_edits", "Saving drops the unsaved changes to \"" +
                                               collection.get_selected_preset_name() + "\"; the project's version history keeps them.",
                                           collection.current_dirty_options()});
        }
    }
    if (owner.selected && result.issues.empty()) {
        std::vector<std::string> others;
        for (const std::string& key : collection.current_dirty_options())
            if (!patch.changes.count(key))
                others.push_back(key);
        if (!others.empty())
            result.warnings.push_back({"", "saves_unsaved_edits",
                                       "Saving also saves " + std::to_string(others.size()) + " unsaved changes already made to \"" +
                                           preset.name + "\".", others});
    }
}

} // namespace

SettingsSearchResult OrcaWorkspaceAdapter::search_settings(const SettingsQuery& query) const
{
    wxASSERT(wxIsMainThread());
    const SettingsScope scope = query.target.scope;
    SettingsSearchResult result;
    if (preset_scope(scope) ? !fff_printer() : !process_settings_available()) {
        result.error = unavailable(scope);
        return result;
    }
    const PresetOwner owner = preset_owner(query.target);
    if (owner.preset == nullptr) {
        result.error = unknown_preset(query.target);
        return result;
    }
    // What differs: an object's own overrides, or the unsaved edits of the
    // preset Orca is editing. A saved preset has none.
    std::vector<std::string> changed;
    if (const ModelObject* object = settings_object(query.target))
        changed = object->config.keys();
    else if (owner.selected)
        changed = owner.collection->current_dirty_options();
    result        = search_setting_definitions(scope_definitions(scope), query, changed);
    result.preset = owner.preset->name;
    return result;
}

ModelObject* OrcaWorkspaceAdapter::settings_object(const SettingsTarget& target) const
{
    if (!target.object)
        return nullptr;
    const auto resolved = resolve(*target.object);
    if (!resolved)
        throw std::logic_error("A settings target names an object that is not in the project");
    return m_plater.model().objects[resolved->index];
}

SettingsReadResult OrcaWorkspaceAdapter::read_settings(const std::vector<std::string>& keys, const SettingsTarget& target) const
{
    wxASSERT(wxIsMainThread());
    SettingsReadResult result;
    if (keys.empty() || keys.size() > 32) {
        result.error = SettingIssue{"", "invalid_arguments", "Read 1 to 32 setting keys."};
        return result;
    }
    if (preset_scope(target.scope) ? !fff_printer() : !process_settings_available()) {
        result.error = unavailable(target.scope);
        return result;
    }
    const PresetOwner owner = preset_owner(target);
    if (owner.preset == nullptr) {
        result.error = unknown_preset(target);
        return result;
    }
    result.preset = owner.preset->name;
    const DynamicPrintConfig& config = owner_config(owner);
    const ModelObject*        object = settings_object(target);
    std::vector<std::string>  dirty, system;
    if (owner.selected) {
        dirty  = owner.collection->current_dirty_options();
        system = owner.collection->current_different_from_parent_options();
    } else if (const Preset* base = owner.collection->get_preset_base(*owner.preset); base != nullptr && base != owner.preset) {
        system = PresetCollection::dirty_options(owner.preset, base);
    }
    // An object reads every process value: the one it prints with.
    const SettingsScope scope = target.scope == SettingsScope::Object ? SettingsScope::Process : target.scope;
    for (const auto& key : keys) {
        if (!has_scope_setting(key, scope)) {
            result.unknown_keys.push_back(key);
            result.issues.push_back(missing_scope_setting(key, scope));
            continue;
        }
        if (withheld_setting(key)) {
            result.unknown_keys.push_back(key);
            result.issues.push_back(withheld_setting_issue(key));
            continue;
        }
        const bool overridden = object && object->config.has(key);
        const auto* value = overridden ? object->config.option(key) : config.option(key);
        if (!value) throw std::logic_error("A preset is missing its defined option: " + key);
        result.items.push_back({key, value->serialize(), std::find(dirty.begin(), dirty.end(), key) != dirty.end(),
            std::find(system.begin(), system.end(), key) != system.end(), setting_definition(key)});
        if (object)
            result.items.back().overridden = overridden;
    }
    return result;
}

SettingsCatalog OrcaWorkspaceAdapter::settings_catalog() const
{
    wxASSERT(wxIsMainThread());
    SettingsCatalog result;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || !fff_printer()) return result;
    result.available = true;
    Search::OptionsSearcher& searcher = m_plater.sidebar().get_searcher();

    result.process.name     = presets->prints.get_edited_preset().name;
    result.process.settings = catalog_settings(presets->prints, presets->prints.get_selected_preset(), true,
                                               Preset::TYPE_PRINT, searcher);
    result.printer.name     = presets->printers.get_edited_preset().name;
    result.printer.settings = catalog_settings(presets->printers, presets->printers.get_selected_preset(), true,
                                               Preset::TYPE_PRINTER, searcher);

    const DynamicPrintConfig& printer_config = presets->printers.get_edited_preset().config;
    const auto* colours = presets->project_config.option<ConfigOptionStrings>("filament_colour");
    for (std::size_t slot = 0; slot < presets->filament_presets.size(); ++slot) {
        const std::string& name   = presets->filament_presets[slot];
        const Preset*      preset = presets->filaments.find_preset(name, false);
        if (preset == nullptr) continue;
        const bool selected = presets->filaments.get_selected_preset_name() == name;
        CatalogFilament filament;
        filament.slot   = slot + 1;
        filament.preset = name;
        const DynamicPrintConfig& config = selected ? presets->filaments.get_edited_preset().config : preset->config;
        if (const auto* types = config.option<ConfigOptionStrings>("filament_type"); types != nullptr && !types->values.empty())
            filament.material = types->values.front();
        if (colours != nullptr && slot < colours->values.size())
            filament.colour = colours->values[slot];
        filament.settings = catalog_settings(presets->filaments, *preset, selected, Preset::TYPE_FILAMENT, searcher);
        mark_inherited(filament.settings, config, printer_config);
        result.filaments.push_back(std::move(filament));
    }

    PartPlateList& plate_list   = m_plater.get_partplate_list();
    const int      active_index = plate_list.get_curr_plate_index();
    for (int index = 0; index < plate_list.get_plate_count(); ++index) {
        PartPlate* plate = plate_list.get_plate(index);
        if (plate == nullptr) continue;
        CatalogPlate entry;
        entry.id     = PlateId(m_session, plate->id().id);
        entry.name   = plate->get_plate_name().empty() ? "Plate " + std::to_string(index + 1) : plate->get_plate_name();
        entry.active = index == active_index;
        // Its own plate type, else the project's: the one it prints with.
        const BedType in_force     = plate->get_bed_type(true);
        entry.own_bed_type         = plate->get_bed_type(false) != btDefault;
        entry.bed_type             = plate_label(in_force);
        entry.bed_temp_key         = get_bed_temp_key(in_force);
        entry.bed_temp_initial_key = get_bed_temp_1st_layer_key(in_force);
        entry.own                  = plate_own_settings(*plate, searcher);
        result.plates.push_back(std::move(entry));
    }

    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const ModelObject& object = *objects[index];
        CatalogObject entry;
        entry.id   = ObjectId(m_session, object.id().id);
        entry.name = object.name;
        for (int plate_index = 0; plate_index < plate_list.get_plate_count() && !entry.plate; ++plate_index)
            if (PartPlate* plate = plate_list.get_plate(plate_index); plate != nullptr && prints_on(object, index, *plate))
                entry.plate = PlateId(m_session, plate->id().id);
        add_object_customizations(object, 0, entry.overrides);
        result.objects.push_back(std::move(entry));
    }
    return result;
}

SettingsPreview OrcaWorkspaceAdapter::preview_settings(const SettingsPatch& patch) const
{
    wxASSERT(wxIsMainThread());
    const SettingsScope scope = patch.target.scope;
    SettingsPreview result;
    if (preset_scope(scope) ? !fff_printer() : !process_settings_available()) {
        result.issues.push_back(unavailable(scope));
        return result;
    }
    const PresetOwner owner = preset_owner(patch.target);
    if (owner.preset == nullptr) {
        result.issues.push_back(unknown_preset(patch.target));
        return result;
    }
    // An object's values are its overrides on top of the process preset.
    const ModelObject* object = settings_object(patch.target);
    DynamicPrintConfig current = owner_config(owner);
    if (object)
        current.apply(object->config.get());
    result.preset = owner.preset->name;
    if (patch.changes.empty() || patch.changes.size() > 32) {
        result.issues.push_back({"", "invalid_arguments", "A patch must contain 1 to 32 settings."});
        return result;
    }
    DynamicPrintConfig next = current;
    for (const auto& [key, text] : patch.changes) {
        if (!has_scope_setting(key, scope)) {
            result.issues.push_back(missing_scope_setting(key, scope));
            continue;
        }
        const auto def = setting_definition(key);
        if (!def.writable) {
            result.issues.push_back({key, "unsupported_setting_mutation", std::string("The tools do not change this ") + scope_name(scope) +
                                                                                 " setting; the person changes it in OrcaSlicer."});
            continue;
        }
        if (auto refused = set_setting_value(next, key, text))
            result.issues.push_back({key, "invalid_setting_value", std::move(*refused), def.enum_values, {}, def.min, def.max});
    }
    if (!result.issues.empty()) return result;
    if (!object)
        check_persist(patch, owner, result);
    if (scope == SettingsScope::Filament)
        check_filament_dialogs(next, result);
    else if (scope == SettingsScope::Printer)
        check_printer_dialogs(next, result);
    else {
        check_process_dialogs(next, result);
        check_support_style(next, patch.changes, result);
    }
    if (!result.issues.empty()) return result;

    const DynamicPrintConfig requested = next;
    // Object overrides are written to the ModelConfig as they are; Orca's
    // normalizer runs only on the process preset and on the object settings
    // panel's own edits. Filament and printer tabs have none.
    if (scope == SettingsScope::Process)
        predict_process_normalization(next);
    for (const auto& key : requested.diff(next)) {
        result.dependencies.push_back({key, current.option(key)->serialize(), next.option(key)->serialize()});
        result.warnings.push_back({key, "normalized_dependency", "Orca will normalize " + key + " to " + next.option(key)->serialize() + "."});
    }

    // The print is validated with the process or the printer in use. A
    // filament's values are not what Slic3r::validate checks, and a printer
    // that is not in use prints nothing here.
    if (scope == SettingsScope::Process || scope == SettingsScope::Object ||
        (scope == SettingsScope::Printer && owner.selected)) {
        auto full = wxGetApp().preset_bundle->full_config();
        full.apply(next);
        FullPrintConfig validation_config;
        validation_config.apply(full, true);
        for (const auto& [key, message] : Slic3r::validate(validation_config)) {
            auto& issues = patch.changes.count(key) ? result.issues : result.warnings;
            issues.push_back({key, "incompatible_settings", message});
        }
    }
    for (const auto& [key, text] : patch.changes) {
        const std::string before = current.option(key)->serialize(), after = next.option(key)->serialize();
        if (before != after) result.changes.push_back({key, before, after});
        else result.warnings.push_back({key, "unchanged", "The setting already has this value."});
    }
    // The confirmations OrcaSlicer asks of a person who makes these changes,
    // or saves them, by hand, which the patch skips.
    skipped_edit_confirmations(scope, result.changes, current, requested, result.warnings);
    if (patch.persist_as)
        skipped_save_confirmations(next, result.warnings);
    // With no explicit change Tab::load_config does not run its normalizer.
    if (result.changes.empty()) {
        result.dependencies.clear();
        result.warnings.erase(std::remove_if(result.warnings.begin(), result.warnings.end(), [](const auto& issue) {
            return issue.code == "normalized_dependency";
        }), result.warnings.end());
    }
    result.valid = result.issues.empty();
    return result;
}

CommandResult OrcaWorkspaceAdapter::apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& expected,
                                                   SettingsPreview& applied)
{
    wxASSERT(wxIsMainThread());
    const SettingsScope scope = patch.target.scope;
    if (preset_scope(scope) ? !fff_printer() : !process_settings_available())
        return CommandResult::failure(WorkspaceError::WorkspaceUnavailable, unavailable(scope).message);
    const PresetOwner owner = preset_owner(patch.target);
    if (owner.preset == nullptr)
        return CommandResult::failure(WorkspaceError::StaleSettings, "The preset is gone. Read and preview again.");
    auto transaction = m_plater.project_state_transaction();
    ModelObject* object = settings_object(patch.target);
    DynamicPrintConfig effective = owner_config(owner);
    if (object)
        effective.apply(object->config.get());
    for (const auto& change : expected)
        if (!effective.option(change.key) || effective.option(change.key)->serialize() != change.before)
            return CommandResult::failure(WorkspaceError::StaleSettings, "A previewed setting changed. Read and preview again.");
    applied = preview_settings(patch);
    if (!applied.valid)
        return CommandResult::failure(WorkspaceError::InvalidSettings, "The settings patch is invalid.");
    const auto actual = previewed_changes(applied);
    if (actual.size() != expected.size() || !std::equal(actual.begin(), actual.end(), expected.begin(),
        [](const auto& a, const auto& b) { return a.key == b.key && a.before == b.before && a.after == b.after; }))
        return CommandResult::failure(WorkspaceError::StaleSettings, "The patch no longer matches the preview.");
    if (applied.changes.empty())
        return CommandResult::failure(WorkspaceError::NoChange, "All requested values are unchanged.");
    if (object) {
        // ObjectSettings' write, as an undo step: the override lands in the
        // object's ModelConfig, the list shows its settings item, and the
        // object's slicing is invalidated.
        Plater::TakeSnapshot snapshot(&m_plater, "Change object settings");
        for (const auto& change : applied.changes) {
            DynamicPrintConfig value;
            value.set_deserialize_strict(change.key, change.after);
            object->config.set_key_value(change.key, value.option(change.key)->clone());
        }
        if (ObjectList* list = wxGetApp().obj_list())
            list->object_config_options_changed({object, nullptr});
        m_plater.changed_object(static_cast<int>(resolve(*patch.target.object)->index));
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Settings | ProjectStateChangeReason::Objects);
        for (auto& change : applied.changes)
            change.after = object->config.option(change.key)->serialize();
        applied.preset_dirty = owner.collection->current_is_dirty();
        return CommandResult::success();
    }
    DynamicPrintConfig diff;
    for (const auto& change : applied.changes)
        diff.set_deserialize_strict(change.key, change.after);

    PresetCollection& collection = *owner.collection;
    const std::string name       = owner.preset->name;
    if (!owner.selected && scope == SettingsScope::Printer) {
        // A saved printer the project does not use, saved in place: preview
        // allowed nothing else.
        Printers::write_unselected_printer(name, diff);
        applied.saved_as = name;
        return CommandResult::success();
    }
    Tab* tab = wxGetApp().get_tab(owner.preset->type);
    if (!owner.selected) {
        // PlaterPresetComboBox::switch_to_tab: the filament tab loads the
        // slot's preset and remembers the slot, so a save lands on it.
        // Tab::select_preset would ask about the unsaved edits it drops, which
        // preview listed; they are dropped first, so nothing asks.
        drop_unsaved_edits(tab);
        const std::size_t slot = *filament_slot(name);
        if (!tab->select_preset(name) || collection.get_selected_preset_name() != name)
            throw std::runtime_error("Orca did not open filament preset " + name + " for editing");
        if (auto* combo = tab->get_combo_box())
            combo->set_filament_idx(int(slot));
    }
    const DynamicPrintConfig& config = collection.get_edited_preset().config;
    const DynamicPrintConfig  before = config;
    // The existing owner updates the preset, its controls, dirty state and
    // slicing. Never imitate this path with direct writes or notifications.
    tab->load_config(diff);
    // Preserve an honest result if a future Orca normalizer introduces a
    // secondary rewrite not yet covered by the prediction audit.
    for (const auto& key : before.diff(config)) {
        const auto has_key = [&key](const auto& changes) {
            return std::any_of(changes.begin(), changes.end(), [&key](const auto& change) { return change.key == key; });
        };
        if (!has_key(applied.changes) && !has_key(applied.dependencies))
            applied.dependencies.push_back({key, before.option(key)->serialize(), config.option(key)->serialize()});
    }
    for (auto* changes : {&applied.changes, &applied.dependencies})
        for (auto& change : *changes) {
            const std::string actual_value = config.option(change.key)->serialize();
            if (actual_value != change.after || changes == &applied.dependencies)
                applied.warnings.push_back({change.key, "normalized", "Orca normalized this setting to " + actual_value + "."});
            change.after = actual_value;
        }
    if (patch.persist_as) {
        // Orca's Save with the name typed in: a copy is selected in place of
        // the preset it was made from, as the dialog's save does. Its first
        // dialog is the skipped confirmation preview reported; the save goes
        // ahead as sent.
        std::vector<SettingIssue> asked;
        skipped_save_confirmations(config, asked);
        ScopedDialogListener go_on([unanswered = !asked.empty()](wxWindow&, const DialogFacts& facts) mutable -> std::optional<int> {
            return std::exchange(unanswered, false) && facts.yes ? std::optional<int>(wxID_YES) : std::nullopt;
        });
        tab->save_preset(*patch.persist_as);
        if (collection.get_selected_preset_name() != *patch.persist_as || collection.current_is_dirty())
            throw std::runtime_error("Orca did not save " + name + " as " + *patch.persist_as);
        applied.saved_as = *patch.persist_as;
    }
    applied.preset_dirty = collection.current_is_dirty();
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::edit_own_setting(ObjectId id, const OwnSettingEdit& edit)
{
    wxASSERT(wxIsMainThread());
    const auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    // Only what the catalog offers: the panel draws each action from
    // own_setting_action, so anything else is not a person's press.
    if (edit.action == OwnSettingAction::None || own_setting_action(edit.kind, edit.key) != edit.action)
        return CommandResult::failure(WorkspaceError::Refused, "The All settings panel offers nothing like that");
    const int    index  = static_cast<int>(resolved->index);
    ModelObject& object = *m_plater.model().objects[resolved->index];
    ObjectList*  list   = wxGetApp().obj_list();
    const auto   sets   = [&edit](const ModelConfig& config) {
        const ConfigOption* option = config.option(edit.key);
        return option != nullptr && option->serialize() == edit.value;
    };

    if (edit.action == OwnSettingAction::Remove) {
        // Orca's object list deletes a volume, one at a time from the last.
        std::vector<ItemForDelete> volumes;
        for (std::size_t volume = 0; volume < object.volumes.size(); ++volume)
            if (volume_kind(*object.volumes[volume]) == edit.kind)
                volumes.emplace_back(itVolume, index, static_cast<int>(volume));
        if (volumes.empty())
            return CommandResult::failure(WorkspaceError::NoChange, object.name + " has no " + edit.kind + " any more");
        if (list == nullptr)
            return CommandResult::failure(WorkspaceError::Failed, "OrcaSlicer's object list is not open");
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        Plater::TakeSnapshot snapshot(&m_plater, volumes.size() == 1 ? "Remove " + edit.kind :
                                                     "Remove " + std::to_string(volumes.size()) + " " + edit.kind + "s");
        ScopedLeastChangeAnswers unexpected;
        list->delete_from_model_and_list(volumes);
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects);
        for (const OrcaMessage& message : unexpected.messages())
            BOOST_LOG_TRIVIAL(warning) << "edit_own_setting: OrcaSlicer asked \"" << message.title << "\" (" << message.text
                                       << "), answered " << message.answer;
        return CommandResult::success();
    }

    // Use project value: ObjectSettings' "Remove parameter", wherever the row
    // the person pressed says the object sets it.
    std::vector<ModelVolume*>            volumes;
    std::vector<t_layer_height_range>    ranges;
    const bool on_object = edit.kind == "object" && sets(object.config);
    if (edit.kind == "height range") {
        for (const auto& [range, config] : object.layer_config_ranges)
            if (sets(config))
                ranges.push_back(range);
    } else if (edit.kind != "object") {
        for (ModelVolume* volume : object.volumes)
            if (volume_kind(*volume) == edit.kind && object.name + " / " + volume->name == edit.target && sets(volume->config))
                volumes.push_back(volume);
    }
    if (!on_object && volumes.empty() && ranges.empty())
        return CommandResult::failure(WorkspaceError::NoChange, object.name + " no longer sets that itself");
    const std::string label = edit.key == "extruder" ? "Filament slot" : setting_definition(edit.key).label;
    const ProjectStateTransaction transaction = m_plater.project_state_transaction();
    Plater::TakeSnapshot snapshot(&m_plater, "Use project value for " + label);
    if (on_object)
        object.config.erase(edit.key);
    for (ModelVolume* volume : volumes)
        volume->config.erase(edit.key);
    for (const t_layer_height_range& range : ranges)
        object.layer_config_ranges[range].erase(edit.key);
    if (list) {
        if (on_object)
            list->object_config_options_changed({&object, nullptr});
        for (ModelVolume* volume : volumes)
            list->object_config_options_changed({&object, volume});
        for (const t_layer_height_range& range : ranges) {
            const wxDataViewItem item = list->GetModel()->GetItemByLayerRange(index, range);
            if (item.IsOk())
                list->add_settings_item(item, &object.layer_config_ranges[range].get());
        }
        // The Filament column reads the slot back from the configs; an
        // object without one prints with slot 1, a part with the object's.
        if (edit.key == "extruder")
            list->update_filament_values_for_items(static_cast<std::size_t>(wxGetApp().filaments_cnt()));
        // The settings tab shows the selected item's own settings.
        list->update_and_show_object_settings_item();
    }
    m_plater.changed_object(index);
    m_plater.notify_project_state_changed(ProjectStateChangeReason::Settings | ProjectStateChangeReason::Objects);
    return CommandResult::success();
}

} // namespace Slic3r::GUI::JusPrin::Workspace
