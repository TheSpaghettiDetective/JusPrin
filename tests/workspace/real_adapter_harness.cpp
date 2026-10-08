#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Geometry.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectOutline.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectVersionStore.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

#include <wx/app.h>
#include <wx/modalhook.h>

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = boost::filesystem;

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

using ::Slic3r::GUI::JusPrin::CanvasPresentationController;

struct HarnessState
{
    enum class Mode { Automated, ManualStock, ManualJusPrin, CrashWrite, CrashVerify };

    std::atomic<int>  result{-1};
    std::atomic<bool> stop{false};
    std::shared_ptr<void> runner;
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + std::chrono::seconds(180)};
    Mode mode{Mode::Automated};
    std::filesystem::path crash_root;
    std::string crash_stage;
};

std::size_t object_count(const WorkspaceSnapshot& snapshot)
{
    std::set<ObjectId> ids;
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            ids.emplace(object.id);
    return ids.size();
}

const WorkspaceObject* find_object(const WorkspaceSnapshot& snapshot, ObjectId id)
{
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            if (object.id == id)
                return &object;
    return nullptr;
}

bool same_ids(const WorkspaceSnapshot& lhs, const WorkspaceSnapshot& rhs)
{
    if (lhs.session != rhs.session || lhs.active_plate != rhs.active_plate || lhs.plates.size() != rhs.plates.size())
        return false;
    for (std::size_t plate_index = 0; plate_index < lhs.plates.size(); ++plate_index) {
        const WorkspacePlate& left_plate = lhs.plates[plate_index];
        const WorkspacePlate& right_plate = rhs.plates[plate_index];
        if (left_plate.id != right_plate.id || left_plate.objects.size() != right_plate.objects.size())
            return false;
        for (std::size_t object_index = 0; object_index < left_plate.objects.size(); ++object_index)
            if (left_plate.objects[object_index].id != right_plate.objects[object_index].id)
                return false;
    }
    return true;
}

class Scenario final : public std::enable_shared_from_this<Scenario>
{
public:
    Scenario(GUI_App& app, std::shared_ptr<HarnessState> state) : m_app(app), m_state(std::move(state)) {}

    void start()
    {
        try {
            if (m_state->mode == HarnessState::Mode::CrashWrite || m_state->mode == HarnessState::Mode::CrashVerify) {
                verify_crash_publication();
                return;
            }
            if (m_state->mode != HarnessState::Mode::Automated) {
                setup_manual_canvas();
                return;
            }
            setup_and_run_commands();
            if (m_finished)
                return;
            m_app.CallAfter([self = shared_from_this()] {
                self->m_app.CallAfter([self] { self->verify_committed_transform(); });
            });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

    void verify_crash_publication()
    {
        m_plater = m_app.plater();
        check(m_plater != nullptr, "crash_plater_ready");
        if (!m_plater) return;
        check(m_plater->new_project(true, true) != wxID_CANCEL, "crash_new_project");
        if (m_state->mode == HarnessState::Mode::CrashWrite) {
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            check(m_plater->load_files(std::vector<std::string>{cube},
                  LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
                  "crash_cube_loaded");
            if (!m_plater->canvas3D()->is_initialized()) {
                fail("crash fixture canvas did not initialize OpenGL");
                return;
            }
            ProjectVersionStore store(m_state->crash_root, "p-crash");
            const auto capture = [this, &store](std::uint64_t revision) {
                const std::string id = ProjectVersionStore::new_id();
                const auto metadata = store.pending_metadata_path(id);
                check(m_plater->export_3mf(fs::path(metadata.string()), SaveStrategy::Backup | SaveStrategy::Silence) >= 0,
                      "crash_metadata_captured");
                return store.freeze(m_plater->model(), id, revision,
                    nlohmann::json{{"project", {{"projectId", "p-crash"}}}, {"attachments", nlohmann::json::array()},
                                   {"draft", revision == 2 ? "after" : "before"}}.dump(), "", "crash-test");
            };
            store.publish(capture(1));
            check(m_plater->duplicate_object(0) == 1, "crash_second_object_created");
            auto second = capture(2);
            const auto stage = m_state->crash_stage;
            store.set_publish_stage_hook([stage](ProjectVersionStore::PublishStage reached) {
                const bool selected = (stage == "resources" && reached == ProjectVersionStore::PublishStage::ResourcesDurable) ||
                    (stage == "version" && reached == ProjectVersionStore::PublishStage::VersionPublished) ||
                    (stage == "head" && reached == ProjectVersionStore::PublishStage::HeadPublished);
                if (selected) std::_Exit(93);
            });
            store.publish(std::move(second));
            fail("publication did not reach the selected crash stage");
            return;
        }
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        check(m_plater->load_files(std::vector<std::string>{cube},
              LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1 &&
              m_plater->canvas3D()->is_initialized(), "crash_verifier_canvas_ready");
        m_plater->reset();
        ProjectVersionStore reopened(m_state->crash_root, "p-crash");
        const auto history = reopened.history();
        const bool committed = m_state->crash_stage == "head";
        check(history.size() == (committed ? 2u : 1u), "crash_committed_history_visible");
        if (!history.empty()) {
            check(reopened.head_id() == history.back().id, "crash_head_matches_history");
            const auto materialized = reopened.materialize(reopened.head_id());
            check(nlohmann::json::parse(materialized.semantic_state).value("draft", "") == "after",
                  "crash_current_document_independent_of_model_head");
            m_plater->reset();
            const auto loaded = m_plater->load_files({fs::path(materialized.metadata.string()),
                                                      fs::path(materialized.absent_original.string())},
                                                     LoadStrategy::Restore, false);
            check(loaded.size() == (committed ? 2u : 1u) &&
                  m_plater->model().objects.size() == (committed ? 2u : 1u), "crash_recovered_model_complete");
            const int released = m_plater->new_project(true, true);
            if (released == wxID_CANCEL)
                if (auto* shell = Slic3r::GUI::JusPrin::installed_shell(); shell && shell->autosave())
                    std::cerr << "HARNESS RECOVERY CLOSE ERROR " << shell->autosave()->error() << '\n';
            check(released != wxID_CANCEL, "crash_recovered_project_released");
            reopened.remove_materialization(materialized);
        }
        const auto root = m_state->crash_root / "p-crash";
        check(std::filesystem::is_empty(root / "pending"), "crash_pending_work_removed");
        check(std::distance(std::filesystem::directory_iterator(root / "resources"),
                            std::filesystem::directory_iterator()) == (committed ? 2 : 1),
              "crash_orphan_resource_removed");
        finish();
    }

private:
    void verify_process_settings()
    {
        struct DialogProbe : wxModalDialogHook {
            int count{0};
            int Enter(wxDialog*) override { ++count; return wxID_CANCEL; }
        } dialogs;
        dialogs.Register();
        auto* tab = m_app.get_tab(Preset::TYPE_PRINT);
        auto& prints = m_app.preset_bundle->prints;
        const DynamicPrintConfig original = prints.get_edited_preset().config;
        const auto start = m_workspace->snapshot();
        const auto revision = start.revision;
        auto search = m_workspace->search_settings({"layer_height"});
        check(!search.error && search.items.front().key == "layer_height", "settings_real_metadata_search");
        check(search.items.front().description == print_config_def.get("layer_height")->tooltip, "settings_metadata_owned_by_orca");
        for (const auto& key : Preset::print_options()) {
            if (!print_config_def.get(key)) continue;
            const auto read = m_workspace->read_settings({key});
            check(read.items.size() == 1 && read.items[0].value == original.option(key)->serialize(), "settings_read_definition_" + key);
        }
        const auto unknown = m_workspace->read_settings({"layer_heigt", "nozzle_diameter"});
        check(unknown.issues.size() == 2 && unknown.issues[0].code == "unknown_setting" &&
              unknown.issues[1].code == "unsupported_scope", "settings_real_unknown_and_scope");
        SettingsPatch patch{{{"layer_height", "0.16"}, {"wall_loops", "4"}, {"sparse_infill_density", "25%"},
            {"sparse_infill_pattern", "gyroid"}, {"top_shell_layers", "7"}, {"bottom_shell_layers", "6"}, {"brim_width", "8"}}};
        const auto preview = m_workspace->preview_settings(patch);
        for (const auto& issue : preview.issues) std::cerr << "SETTINGS ISSUE " << issue.key << ' ' << issue.message << '\n';
        check(preview.valid, "settings_real_preview_valid");
        check(current_values_equal(original, prints.get_edited_preset().config), "settings_preview_does_not_mutate");
        check(m_workspace->snapshot().revision == revision, "settings_preview_publishes_no_event");
        SettingsPreview applied;
        const auto before_events = m_changes.size();
        const auto result = m_workspace->apply_settings(patch, previewed_changes(preview), applied);
        check(result.succeeded(), "settings_apply_before_process_page_shown");
        check(m_changes.size() == before_events + 1 && m_changes.back().reasons == WorkspaceChangeReasons::Settings,
            "settings_batch_one_event");
        check(m_workspace->snapshot().revision == revision + 1, "settings_batch_one_revision");
        check(m_workspace->snapshot().setup.process_preset_dirty && tab->current_preset_is_dirty(), "settings_preset_dirty_indicator");
        check(m_workspace->snapshot().can_undo == start.can_undo && m_workspace->snapshot().can_redo == start.can_redo, "settings_outside_project_undo");
        for (const auto& change : applied.changes)
            check(prints.get_edited_preset().config.option(change.key)->serialize() == change.after, "settings_real_value_" + change.key);
        SettingsPatch inverse;
        for (const auto& change : applied.changes) inverse.changes[change.key] = change.before;
        check(m_workspace->apply_settings(inverse, previewed_changes(m_workspace->preview_settings(inverse)), applied).succeeded(), "settings_inverse_applies");
        check(current_values_equal(original, prints.get_edited_preset().config), "settings_inverse_restores_every_key");

        for (const auto& value : {"0", "999", "0.2junk", "NaN"}) {
            const SettingsPatch invalid{{{"layer_height", value}, {"wall_loops", "4"}}};
            const auto preview_bad = m_workspace->preview_settings(invalid);
            check(!preview_bad.valid && preview_bad.issues.front().code == "invalid_setting_value", "settings_reject_invalid_height_" + std::string(value));
            check(m_workspace->apply_settings(invalid, {}, applied).error == WorkspaceError::InvalidSettings, "settings_invalid_batch_atomic");
            check(current_values_equal(original, prints.get_edited_preset().config), "settings_invalid_batch_changes_nothing");
        }
        check(m_workspace->preview_settings({{{"sparse_infill_density", "101%"}}}).issues.front().code == "invalid_setting_value",
              "settings_percent_bounds_use_percent_units");
        auto& config = prints.get_edited_preset().config;
        auto& printer = m_app.preset_bundle->printers.get_edited_preset().config;
        const auto old_max = printer.option("max_layer_height")->clone();
        printer.set_key_value("max_layer_height", new ConfigOptionFloats({0.3}));
        const SettingsPatch maximum{{{"layer_height", "0.3"}}};
        check(m_workspace->preview_settings(maximum).valid, "settings_accept_printer_maximum");
        check(m_workspace->apply_settings(maximum, previewed_changes(m_workspace->preview_settings(maximum)), applied).succeeded(), "settings_apply_printer_maximum");
        tab->load_config(original);
        printer.set_key_value("max_layer_height", old_max);

        config.set_deserialize_strict("seam_slope_type", "external");
        config.set_deserialize_strict("seam_slope_start_height", "0.18");
        const SettingsPatch scarf{{{"layer_height", "0.16"}}};
        check(!m_workspace->preview_settings(scarf).valid, "settings_scarf_conflict_is_blocking");
        check(m_workspace->apply_settings(scarf, {}, applied).error == WorkspaceError::InvalidSettings, "settings_scarf_apply_no_dialog");
        config.set_deserialize_strict("seam_slope_start_height", "150%");
        check(!m_workspace->preview_settings(scarf).valid, "settings_percent_scarf_at_or_above_layer_is_blocking");
        config = original;
        config.set_deserialize_strict("spiral_mode", "1");
        for (const auto& key : {"wall_loops", "top_shell_layers", "sparse_infill_density"}) {
            const SettingsPatch spiral{{{key, "3"}}};
            check(!m_workspace->preview_settings(spiral).valid, "settings_spiral_conflict_" + std::string(key));
            check(m_workspace->apply_settings(spiral, {}, applied).error == WorkspaceError::InvalidSettings, "settings_spiral_apply_no_dialog");
        }
        config = original;
        for (const auto& fixture : std::vector<SettingsPatch>{
                 {{{"ironing_spacing", "0.01"}}}, {{{"support_ironing_spacing", "0.01"}}},
                 {{{"initial_layer_print_height", "0"}}}, {{{"xy_hole_compensation", "3"}}},
                 {{{"xy_contour_compensation", "3"}}}, {{{"elefant_foot_compensation", "2"}}},
                 {{{"infill_lock_depth", "1"}, {"skin_infill_depth", "0.5"}}}}) {
            config = original;
            for (const auto& item : fixture.changes) config.set_deserialize_strict(item.first, item.second);
            const DynamicPrintConfig before_invalid = config;
            const SettingsPatch change{{{"wall_loops", "4"}}};
            const auto refusal = m_workspace->preview_settings(change);
            check(!refusal.valid && refusal.issues.front().code == "incompatible_settings", "settings_preexisting_dialog_guard_" + fixture.changes.begin()->first);
            check(m_workspace->apply_settings(change, {}, applied).error == WorkspaceError::InvalidSettings &&
                  current_values_equal(before_invalid, config), "settings_preexisting_dialog_guard_is_atomic");
        }
        config = original;
        const auto support_gap = config.option("support_top_z_distance")->serialize();
        check(m_workspace->apply_settings(scarf, previewed_changes(m_workspace->preview_settings(scarf)), applied).succeeded(), "settings_height_with_support_gap");
        check(config.option("support_top_z_distance")->serialize() == support_gap, "settings_compiled_out_support_gap_rule_does_not_write");
        tab->load_config(original);
        // Exercise the active settings page and its real field, then a silent
        // dependency through Orca's normalizer.
        tab->activate_option("sparse_infill_pattern", "Strength");
        config.set_deserialize_strict("fill_multiline", "3");
        const SettingsPatch multiline{{{"sparse_infill_pattern", "line"}, {"sparse_infill_density", "25%"}}};
        const auto prediction = m_workspace->preview_settings(multiline);
        check(prediction.valid && std::any_of(prediction.dependencies.begin(), prediction.dependencies.end(), [](const auto& c) {
            return c.key == "fill_multiline" && c.after == "1";
        }), "settings_multiline_reset_predicted");
        check(m_workspace->apply_settings(multiline, previewed_changes(prediction), applied).succeeded(), "settings_multiline_apply");
        check(config.opt_int("fill_multiline") == 1, "settings_multiline_actual_reset");
        check(std::any_of(applied.warnings.begin(), applied.warnings.end(), [](const auto& issue) {
            return issue.key == "fill_multiline" && issue.code == "normalized";
        }), "settings_multiline_actual_reported");
        tab->activate_option("wall_loops", "Strength");
        const SettingsPatch walls{{{"wall_loops", "4"}}};
        const auto expected = previewed_changes(m_workspace->preview_settings(walls));
        DynamicPrintConfig gui_edit;
        gui_edit.set_deserialize_strict("wall_loops", "5");
        const auto before_gui_edit = m_changes.size();
        tab->load_config(gui_edit);
        check(m_changes.size() == before_gui_edit + 1 && has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Settings), "settings_gui_edit_one_revision");
        check(m_workspace->apply_settings(walls, expected, applied).error == WorkspaceError::StaleSettings, "settings_gui_edit_invalidates_previewed_values");
        auto* field = tab->get_field("wall_loops");
        check(field && boost::any_cast<int>(field->get_value()) == 5, "settings_native_field_matches_value");
        tab->on_roll_back_value(false);
        check(!m_workspace->snapshot().setup.process_preset_dirty, "settings_native_revert_clears_dirty");
        tab->load_config(original);

        // Saving a preset folds the value in force into it: the delta
        // disappears while nothing in force changes, so the change log must
        // not report a setting change. The save is simulated by writing the
        // value into the selected preset, which is what a save stores.
        tab->load_config(gui_edit);
        const std::string saved_walls = prints.get_selected_preset().config.opt_serialize("wall_loops");
        const std::size_t edits_before_save = m_edits.size();
        prints.get_selected_preset().config.set_deserialize_strict("wall_loops", "5");
        m_plater->notify_project_state_changed(ProjectStateChangeReason::Settings);
        check(std::none_of(m_edits.begin() + static_cast<std::ptrdiff_t>(edits_before_save), m_edits.end(),
                           [](const WorkspaceEdit& edit) { return edit.kind == EditKind::Setting; }),
              "saved_preset_is_not_a_setting_change");
        prints.get_selected_preset().config.set_deserialize_strict("wall_loops", saved_walls);
        tab->load_config(original);
        check(dialogs.count == 0, "settings_no_modal_dialog_reached");
    }

    // The Plates pane's read model against the real Plater: membership comes
    // from the plate list, copy switches from the model, selection from the
    // canvas, and every command ends in the path Orca's own UI takes. The
    // fixture is two objects with one copy each, one per plate.
    void verify_outline(const WorkspaceSnapshot& initial)
    {
        const auto local_override = [](const WorkspaceSnapshot& snapshot, const std::string& key,
                                       const std::string& kind = {}) -> const SetupLocalOverride* {
            if (!snapshot.applied_setup) return nullptr;
            const auto& overrides = snapshot.applied_setup->local_overrides;
            const auto found = std::find_if(overrides.begin(), overrides.end(), [&](const SetupLocalOverride& item) {
                return item.key == key && (kind.empty() || item.kind == kind);
            });
            return found == overrides.end() ? nullptr : &*found;
        };
        check(initial.applied_setup && initial.applied_setup->printable_objects == 1,
              "setup_active_plate_excludes_other_plate_object");
        check(initial.applied_setup->local_overrides.empty(), "setup_clean_object_has_no_local_overrides");
        check(initial.applied_setup->objects.size() == 1 &&
                  initial.applied_setup->objects.front() == m_plater->model().objects.front()->name,
              "setup_names_the_objects_it_covers");

        const ProjectOutline outline = m_workspace->outline();
        check(outline.session == initial.session, "pane_outline_session_agrees");
        check(outline.plates.size() == initial.plates.size() && outline.objects.size() == 2, "pane_outline_lists_fixture");
        check(outline.filament_count >= 1, "pane_outline_counts_a_filament");
        check(outline.can_add_plate == m_plater->can_add_plate(), "pane_outline_reports_orcas_add_plate_condition");

        const auto active = std::count_if(outline.plates.begin(), outline.plates.end(), [](const OutlinePlate& p) { return p.active; });
        check(active == 1 && initial.active_plate &&
                  std::any_of(outline.plates.begin(), outline.plates.end(),
                              [&](const OutlinePlate& p) { return p.active && p.id == *initial.active_plate; }),
              "pane_outline_active_plate_agrees_with_snapshot");

        // Membership must agree with the snapshot's per-plate objects, which
        // are read from the same plate list by a different path.
        bool membership_agrees = true;
        for (const WorkspacePlate& plate : initial.plates) {
            std::set<ObjectId> from_snapshot, from_outline;
            for (const WorkspaceObject& object : plate.objects)
                from_snapshot.insert(object.id);
            for (const PlacedCopy& placed : copies_on_plate(outline, plate.id))
                from_outline.insert(placed.object->id);
            membership_agrees = membership_agrees && from_snapshot == from_outline;
        }
        check(membership_agrees, "pane_outline_membership_agrees_with_snapshot");
        check(copies_off_plate(outline).empty(), "pane_fixture_has_no_off_plate_copy");
        check(summarize_plate(outline, initial.plates.front().id).copies == 1 &&
                  summarize_plate(outline, initial.plates.front().id).objects == 1,
              "pane_summary_of_a_plate_with_one_object");

        const OutlineObject& first = outline.objects.front();
        check(first.copies.size() == 1 && first.copies.front().printable && first.printable, "pane_simple_object_prints");
        check(first.volumes.size() == 1 && first.volumes.front().role == VolumeRole::Part && !has_volume_children(first),
              "pane_simple_object_has_no_children");
        std::cerr << "PANE first object: customization=" << first.customization.any() << " overrides=" << first.customization.setting_overrides
                  << " mesh open=" << first.mesh.open_edges << " repaired=" << first.mesh.repaired_errors << " extruder=" << first.extruder.value_or(0) << '\n';
        check(!first.customization.any() && !first.mesh.any() && first.extruder.value_or(1) == 1,
              "pane_clean_object_has_no_marks");

        const WorkspaceSnapshot before_read = m_workspace->snapshot();
        const ProjectOutline    second_read = m_workspace->outline();
        const WorkspaceSnapshot again       = m_workspace->snapshot();
        check(second_read.objects.front().id == first.id && second_read.objects.front().copies.front().id == first.copies.front().id &&
                  second_read.objects.front().volumes.front().id == first.volumes.front().id && again.revision == before_read.revision,
              "pane_outline_ids_are_stable_and_reading_is_silent");

        // Facts the person chose: a per-object setting, and a filament override.
        ModelObject& model_first = *m_plater->model().objects.front();
        model_first.config.set_key_value("wall_loops", new ConfigOptionInt(5));
        const WorkspaceSnapshot locally_edited = m_workspace->snapshot();
        const SetupLocalOverride* local_walls = local_override(locally_edited, "wall_loops", "object");
        check(local_walls && local_walls->value == "5" && !local_walls->label.empty() && !local_walls->display.empty(),
              "setup_manual_object_setting_has_generic_display_facts");

        // The case the preset-plus-object read gets wrong: a modifier holds a
        // value that neither config has, so that read still answers 5 for the
        // whole plate. The setup must not.
        ModelVolume* modifier = model_first.add_volume(TriangleMesh(model_first.volumes.front()->mesh()),
                                                       ModelVolumeType::PARAMETER_MODIFIER);
        modifier->name = "tab";
        modifier->config.set_key_value("wall_loops", new ConfigOptionInt(7));
        const WorkspaceSnapshot with_modifier = m_workspace->snapshot();
        const SetupLocalOverride* modified_walls = local_override(with_modifier, "wall_loops", "modifier");
        const auto whole_plate_read = m_workspace->read_settings(
            {"wall_loops"}, SettingsTarget{SettingsScope::Object, with_modifier.plates.front().objects.front().id, {}});
        std::cerr << "SETUP modifier: object read=" << (whole_plate_read.items.empty() ? "" : whole_plate_read.items.front().value)
                  << " modifier=" << (modified_walls ? modified_walls->display : "") << '\n';
        check(modified_walls && modified_walls->value == "7" &&
                  modified_walls->target == model_first.name + " / tab" && modified_walls->object == 0,
              "setup_modifier_value_is_local_not_universal");
        model_first.delete_volume(model_first.volumes.size() - 1);

        // A height range that changes walls is a local wall setting, and is
        // not variable layer height.
        ModelConfig& range = model_first.layer_config_ranges[{0.0, 2.0}];
        range.set_key_value("wall_loops", new ConfigOptionInt(3));
        const WorkspaceSnapshot with_range = m_workspace->snapshot();
        const SetupLocalOverride* ranged_walls = local_override(with_range, "wall_loops", "height range");
        check(ranged_walls && ranged_walls->value == "3" && !with_range.applied_setup->variable_layer_height,
              "setup_height_range_is_local_and_not_variable_height");
        model_first.layer_config_ranges.clear();
        check(local_override(m_workspace->snapshot(), "wall_loops", "height range") == nullptr,
              "setup_height_range_clears_with_the_override");
        check(m_workspace->outline().objects.front().customization.setting_overrides == 1 &&
                  m_workspace->outline().objects.front().customization.any(),
              "pane_setting_override_is_customization");
        model_first.config.set_key_value("extruder", new ConfigOptionInt(2));
        const OutlineObject overridden = m_workspace->outline().objects.front();
        check(overridden.extruder == 2 && overridden.customization.setting_overrides == 1,
              "pane_filament_override_is_not_a_setting_override");
        const WorkspaceSnapshot with_filament_override = m_workspace->snapshot();
        const SetupLocalOverride* filament_override = local_override(with_filament_override, "extruder", "object");
        check(filament_override && filament_override->value == "2" && !filament_override->label.empty(),
              "setup_nondefault_filament_slot_is_local");
        model_first.config.erase("extruder");
        model_first.config.erase("wall_loops");

        // A disabled copy is explicit, and the object switch is separate.
        model_first.instances.front()->printable = false;
        check(m_workspace->snapshot().applied_setup && m_workspace->snapshot().applied_setup->printable_objects == 0,
              "setup_disabled_copy_is_not_printable");
        check(!m_workspace->outline().objects.front().copies.front().printable &&
                  summarize_plate(m_workspace->outline(), initial.plates.front().id).wont_print == 1,
              "pane_disabled_copy_wont_print");
        model_first.instances.front()->printable = true;
        model_first.printable = false;
        check(!m_workspace->outline().objects.front().printable &&
                  summarize_plate(m_workspace->outline(), initial.plates.front().id).wont_print == 1,
              "pane_disabled_object_wont_print");
        model_first.printable = true;

        // A copy no plate holds is off the plate; where it sits does not matter.
        model_first.add_instance();
        check(m_workspace->snapshot().applied_setup && m_workspace->snapshot().applied_setup->printable_objects == 1,
              "setup_off_plate_copy_does_not_change_active_scope");
        const ProjectOutline with_loose = m_workspace->outline();
        const auto loose = copies_off_plate(with_loose);
        check(loose.size() == 1 && loose.front().object->id == first.id && with_loose.objects.front().copies.size() == 2,
              "pane_unplaced_copy_is_off_plate");
        check(summarize_plate(with_loose, initial.plates.front().id).copies == 1, "pane_off_plate_copy_is_in_no_summary");
        model_first.delete_last_instance();
        check(copies_off_plate(m_workspace->outline()).empty(), "pane_off_plate_group_clears");

        // Selecting a plate is Orca's plate click: one Plates change, no undo step.
        const WorkspaceSnapshot before_select = m_workspace->snapshot();
        const PlateId inactive = [&] {
            for (const OutlinePlate& plate : m_workspace->outline().plates)
                if (!plate.active) return plate.id;
            return PlateId();
        }();
        check(static_cast<bool>(inactive), "pane_fixture_has_an_inactive_plate");
        const std::size_t changes_before = m_changes.size();
        const std::size_t edits_before   = m_edits.size();
        check(m_workspace->select_plate(inactive).succeeded(), "pane_select_plate");
        check(m_workspace->snapshot().active_plate && *m_workspace->snapshot().active_plate == inactive,
              "pane_select_plate_activates_the_orca_plate");
        check(m_plater->get_partplate_list().get_curr_plate()->id().id == inactive.value(), "pane_select_plate_reached_orca");
        // The setup is the selected plate's: its scope moves with the plate,
        // and it names that plate's object rather than the first in the model.
        const WorkspaceSnapshot on_other_plate = m_workspace->snapshot();
        check(on_other_plate.applied_setup && on_other_plate.applied_setup->plate == inactive &&
                  on_other_plate.applied_setup->objects ==
                      std::vector<std::string>{m_plater->model().objects.back()->name},
              "setup_follows_the_selected_plate");
        check(m_changes.size() > changes_before && has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Plates),
              "pane_select_plate_publishes_a_plates_change");
        check(m_edits.size() == edits_before, "pane_select_plate_is_not_an_edit");
        check(m_workspace->select_plate(inactive).error == WorkspaceError::NoChange, "pane_select_plate_twice_is_no_change");
        check(m_workspace->select_plate(PlateId(initial.session, 987654321)).error == WorkspaceError::MissingObject,
              "pane_select_unknown_plate");
        check(m_workspace->select_plate(PlateId(ProjectSessionId(initial.session.value() + 1), inactive.value())).error ==
                  WorkspaceError::StaleId,
              "pane_select_plate_of_another_session");
        check(m_workspace->select_plate(*before_select.active_plate).succeeded(), "pane_select_plate_back");

        // A copy is selected exactly, and the canvas reports the same thing.
        const InstanceId copy_id = m_workspace->outline().objects.front().copies.front().id;
        check(m_workspace->select_copy(copy_id).succeeded(), "pane_select_copy");
        Selection& selection = m_plater->canvas3D()->get_selection();
        check(selection.is_single_full_instance() && selection.get_object_idx() == 0 && selection.get_instance_idx() == 0,
              "pane_select_copy_reached_the_canvas_selection");
        check(m_workspace->outline().selected_copies == std::vector<InstanceId>{copy_id} &&
                  m_workspace->outline().selected_volumes.empty(),
              "pane_selected_copy_is_read_back");
        // Selecting an object is not a change of scope: the setup still
        // describes the whole plate.
        const WorkspaceSnapshot with_selection = m_workspace->snapshot();
        check(with_selection.applied_setup && with_selection.applied_setup->plate == *before_select.active_plate &&
                  with_selection.applied_setup->printable_objects == 1,
              "setup_scope_ignores_object_selection");
        check(m_workspace->select_copy(copy_id).error == WorkspaceError::NoChange, "pane_select_copy_twice_is_no_change");
        check(m_workspace->select_volume(m_workspace->outline().objects.front().volumes.front().id).error ==
                  WorkspaceError::UnavailableOperation,
              "pane_single_part_volume_is_not_selectable");
        m_plater->deselect_all();
        check(m_workspace->outline().selected_copies.empty(), "pane_canvas_deselect_is_read_back");

        // Add plate is the toolbar's command: one undo step, and undo removes it.
        const std::size_t plates_before = m_workspace->outline().plates.size();
        check(m_workspace->add_plate().succeeded(), "pane_add_plate");
        check(m_workspace->outline().plates.size() == plates_before + 1 && m_workspace->outline().plates.back().active,
              "pane_add_plate_adds_and_activates");
        check(m_workspace->snapshot().can_undo, "pane_add_plate_is_undoable");
        check(m_workspace->undo().succeeded() && m_workspace->outline().plates.size() == plates_before, "pane_add_plate_undo");
        check(m_workspace->redo().succeeded() && m_workspace->outline().plates.size() == plates_before + 1, "pane_add_plate_redo");
        check(m_workspace->undo().succeeded() && m_workspace->outline().plates.size() == plates_before, "pane_add_plate_undone_again");
        check(m_workspace->select_plate(*before_select.active_plate).error == WorkspaceError::NoChange ||
                  m_workspace->snapshot().active_plate == before_select.active_plate,
              "pane_fixture_restored");
    }

    // Filament and printer settings, by preset name: an unsaved change to the
    // preset being edited, a save in place, a copy of a preset Orca ships
    // that takes its place, and a saved printer the project does not use,
    // all through Orca's own tabs and files, with no dialog.
    void verify_preset_settings()
    {
        struct DialogProbe : wxModalDialogHook {
            int count{0};
            int Enter(wxDialog*) override { ++count; return wxID_CANCEL; }
        } dialogs;
        dialogs.Register();
        PresetBundle&     bundle   = *m_app.preset_bundle;
        PresetCollection& printers = bundle.printers;
        PresetCollection& filaments = bundle.filaments;
        SettingsPreview   applied;
        const auto expected = [this](const SettingsPatch& patch) { return previewed_changes(m_workspace->preview_settings(patch)); };
        const auto refused = [this](const SettingsPatch& patch, const std::string& code) {
            const auto issues = m_workspace->preview_settings(patch).issues;
            return std::any_of(issues.begin(), issues.end(), [&code](const SettingIssue& issue) { return issue.code == code; });
        };

        const std::string printer = printers.get_selected_preset_name();
        const SettingsTarget in_use{SettingsScope::Printer, {}, printer};
        const auto read = m_workspace->read_settings({"machine_start_gcode", "nozzle_diameter"}, in_use);
        check(!read.error && read.preset == printer && read.items.size() == 2 &&
                  read.items[0].value == printers.get_edited_preset().config.opt_serialize("machine_start_gcode") &&
                  read.items[0].definition.writable && !read.items[1].definition.writable,
              "preset_settings_read_printer_by_name");
        const auto label = m_workspace->read_settings({"machine_start_gcode"}, {SettingsScope::Printer, {}, printer + " (label)"});
        check(label.error && label.error->code == "unknown_preset", "preset_settings_label_is_not_a_name");

        // The gcode goes through Tab::load_config, unsaved, as a process edit does.
        const std::string gcode = "G28 ; jusprin harness";
        SettingsPatch unsaved{{{"machine_start_gcode", gcode}}, in_use};
        check(m_workspace->apply_settings(unsaved, expected(unsaved), applied).succeeded() && applied.preset_dirty &&
                  printers.current_is_dirty() && printers.get_edited_preset().config.opt_string("machine_start_gcode") == gcode,
              "preset_settings_printer_unsaved_change");
        const auto* retraction = printers.get_edited_preset().config.option<ConfigOptionFloats>("retraction_length");
        const std::string two_values = retraction->values.size() == 1 ? "0.8,0.8" : "0.8";
        check(refused({{{"retraction_length", two_values}}, in_use}, "invalid_setting_value"), "preset_settings_list_keeps_its_length");
        const SettingsPatch firmware{{{"use_firmware_retraction", "1"}, {"wipe", std::string(retraction->values.size() == 1 ? "1" : "1,1")},
                                      {"retract_before_wipe", std::string(retraction->values.size() == 1 ? "50%" : "50%,50%")}}, in_use};
        check(refused(firmware, "incompatible_settings"), "preset_settings_printer_dialog_refused");

        // Saved: in place for the person's own printer, as a copy of one Orca
        // ships, which is then the printer in use.
        const bool shipped = printers.get_selected_preset().is_system;
        SettingsPatch own{{{"machine_start_gcode", gcode + " 2"}}, in_use, printer};
        const auto own_preview = m_workspace->preview_settings(own);
        check(shipped ? !own_preview.issues.empty() && own_preview.issues.front().code == "read_only_preset" &&
                            own_preview.issues.front().suggestions == std::vector<std::string>{printer + " - Copy"} :
                        own_preview.valid,
              "preset_settings_save_in_place_only_for_a_user_preset");
        const std::string saved_name = shipped ? printer + " - Copy" : printer;
        SettingsPatch save{{{"machine_start_gcode", gcode + " 2"}}, in_use, saved_name};
        check(m_workspace->apply_settings(save, expected(save), applied).succeeded() && applied.saved_as == saved_name && !applied.preset_dirty,
              "preset_settings_printer_saved");
        const Preset* copy = printers.find_preset(saved_name, false);
        check(copy != nullptr && !copy->is_system && printers.get_selected_preset_name() == saved_name &&
                  copy->config.opt_string("machine_start_gcode") == gcode + " 2" && !printers.current_is_dirty(),
              "preset_settings_printer_saved_is_selected");
        if (shipped)
            check(printers.find_preset(printer, false)->config.opt_string("machine_start_gcode") != gcode + " 2",
                  "preset_settings_shipped_printer_unchanged");
        check(m_workspace->snapshot().setup.printer_preset == saved_name, "workspace_names_the_printer_by_preset_name");

        // A saved printer the project does not use is written in place, and
        // stays unselected.
        if (shipped) {
            check(m_app.get_tab(Preset::TYPE_PRINTER)->select_preset(printer) && printers.get_selected_preset_name() == printer,
                  "preset_settings_printer_reselected");
            const SettingsTarget other{SettingsScope::Printer, {}, saved_name};
            check(refused({{{"machine_start_gcode", "G28 ; other"}}, other}, "not_selected"), "preset_settings_unselected_needs_persist");
            SettingsPatch in_place{{{"machine_start_gcode", "G28 ; other"}}, other, saved_name};
            check(m_workspace->apply_settings(in_place, expected(in_place), applied).succeeded() && applied.saved_as == saved_name,
                  "preset_settings_unselected_printer_saved");
            check(printers.get_selected_preset_name() == printer &&
                      printers.find_preset(saved_name, false)->config.opt_string("machine_start_gcode") == "G28 ; other",
                  "preset_settings_unselected_printer_written_not_selected");
        }

        // A filament in the project: opened in its slot as the sidebar's edit
        // button opens it, then saved; a copy of one Orca ships takes its slot.
        const std::string filament = bundle.filament_presets.front();
        const SettingsTarget slot{SettingsScope::Filament, {}, filament};
        // One temperature per nozzle kind the filament knows: a list keeps
        // its length (Bambu's X1C filaments hold two).
        const auto* temperatures = filaments.find_preset(filament, false)->config.option<ConfigOptionInts>("nozzle_temperature");
        std::string hotter;
        for (const int temperature : temperatures->values)
            hotter += (hotter.empty() ? "" : ",") + std::to_string(temperature + 5);
        const bool        shipped_filament = filaments.find_preset(filament, false)->is_system;
        const std::string filament_name    = shipped_filament ? filament + " - Copy" : filament;
        check(refused({{{"filament_max_volumetric_speed", "0.2"}}, slot, filament_name}, "invalid_setting_value"),
              "preset_settings_filament_dialog_refused");
        SettingsPatch     warmer{{{"nozzle_temperature", hotter}, {"nozzle_temperature_initial_layer", hotter}}, slot, filament_name};
        const auto warmer_preview = m_workspace->preview_settings(warmer);
        check(warmer_preview.valid, "preset_settings_filament_preview_valid");
        check(m_workspace->apply_settings(warmer, previewed_changes(warmer_preview), applied).succeeded() &&
                  applied.saved_as == filament_name,
              "preset_settings_filament_saved");
        check(bundle.filament_presets.front() == filament_name &&
                  filaments.find_preset(filament_name, false)->config.opt_serialize("nozzle_temperature") == hotter &&
                  !filaments.current_is_dirty(),
              "preset_settings_filament_in_its_slot");
        check(dialogs.count == 0, "preset_settings_no_modal_dialog_reached");
    }

    static bool current_values_equal(const DynamicPrintConfig& a, const DynamicPrintConfig& b) { return a.diff(b).empty(); }

    void check(bool condition, const std::string& name)
    {
        std::cerr << "HARNESS CHECK " << name << ' ' << (condition ? "PASS" : "FAIL") << '\n';
        if (!condition)
            ++m_failures;
    }

    void setup_and_run_commands()
    {
        Plater* plater = m_app.plater();
        check(plater != nullptr, "plater_ready");
        if (plater == nullptr) {
            fail("Plater was not constructed");
            return;
        }
        m_plater = plater;

        verify_selection_tail_redo();
        // The canvas initializes GL while loading the first model. Selection
        // is built from its volumes, so without usable GL (on Windows: no Mesa
        // opengl32.dll beside the harness) the later checks fail without
        // naming the cause, and the gizmo check crashes. Stop here instead.
        if (!m_plater->canvas3D()->is_initialized()) {
            fail("the 3D canvas did not initialize OpenGL; on Windows without a GPU, run "
                 "src/slic3r/GUI/JusPrin/Testing/windows-gl/provision-mesa-windows.ps1 on this build tree");
            return;
        }
        verify_cross_plate_duplicate();
        verify_plate_membership_after_cut();
        verify_local_versions();
        verify_reopened_local_versions();
        verify_paint_version_resources();
        verify_attachment_retention();
        verify_legacy_delete_history();
        verify_disabled_gizmo_wheel();

        check(m_plater->new_project(true, true) != wxID_CANCEL, "new_project_fixture");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "repository_cube_loaded");
        const int second_index = m_plater->duplicate_object(0);
        check(second_index == 1, "second_fixture_object_created");
        PartPlateList& plates = m_plater->get_partplate_list();
        const int second_plate = plates.create_plate(true);
        check(second_plate == 1, "second_fixture_plate_created");
        check(plates.add_to_plate(1, 0, second_plate) == 0, "fixture_object_moved_to_second_plate");
        // The loader does not place the first object on a plate; center it so
        // the fixture is deterministically on-bed with one object per plate.
        check(plates.get_plate(0)->add_instance(0, 0, true) == 0, "fixture_object_centered_on_first_plate");
        m_plater->canvas3D()->reload_scene(true, true);
        check(m_plater->canvas3D()->get_volumes_count() >= 2, "fixture_render_volumes_loaded");

        m_workspace = std::make_unique<OrcaWorkspaceAdapter>(*m_plater);
        m_subscription = m_workspace->subscribe([this](const WorkspaceChanged& change) {
            m_changes.emplace_back(change);
            const WorkspaceSnapshot observed = m_workspace->snapshot();
            check(observed.revision == change.revision, "event_snapshot_revision_agrees");
            check(observed.session == change.session, "event_snapshot_session_agrees");
        });
        m_edit_subscription = m_workspace->subscribe_edits([this](const WorkspaceEdit& edit) { m_edits.push_back(edit); });

        const WorkspaceSnapshot initial = m_workspace->snapshot();
        verify_process_settings();
        // The change log reads settings from the presets' own differences.
        check(std::any_of(m_edits.begin(), m_edits.end(), [](const WorkspaceEdit& edit) {
                  return edit.kind == EditKind::Setting && !edit.label.empty() && edit.before != edit.after;
              }),
              "change_log_records_setting_edits");
        verify_preset_settings();
        check(initial.plates.size() >= 2, "initial_snapshot_has_multiple_plates");
        check(initial.active_plate.has_value(), "initial_snapshot_has_active_plate");
        check(object_count(initial) == 2, "initial_snapshot_has_two_objects");
        check(same_ids(initial, m_workspace->snapshot()), "stable_snapshot_ids_within_session");

        verify_outline(initial);

        m_first = initial.plates.front().objects.front().id;
        for (const WorkspacePlate& plate : initial.plates)
            for (const WorkspaceObject& object : plate.objects)
                if (object.id != m_first)
                    m_second = object.id;
        check(static_cast<bool>(m_first) && static_cast<bool>(m_second), "stable_object_ids_discovered");

        const std::size_t edits_before_selection = m_edits.size();
        const std::size_t before_adapter_selection = m_changes.size();
        check(m_workspace->select_object(m_first).succeeded(), "adapter_selection_command");
        check(m_changes.size() == before_adapter_selection + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Selection),
              "adapter_selection_observed_once");

        const std::size_t second_index_now = model_index(m_second);
        const std::size_t before_native_selection = m_changes.size();
        check(second_index_now != invalid_index && m_plater->select_object(second_index_now), "native_selection_command");
        check(m_changes.size() == before_native_selection + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Selection),
              "native_selection_observed_by_adapter");
        // Selection records "!"-named or Selection-type undo steps; neither is
        // a real edit.
        check(m_edits.size() == edits_before_selection, "selection_is_not_a_change_log_edit");

        const std::size_t edits_before_rename = m_edits.size();
        check(m_workspace->rename_object(m_first, "Adapter Renamed").succeeded(), "adapter_rename");
        check(find_object(m_workspace->snapshot(), m_first)->name == "Adapter Renamed", "adapter_rename_projected");
        const auto steps_since = [this](std::size_t from) {
            return std::count_if(m_edits.begin() + static_cast<std::ptrdiff_t>(from), m_edits.end(),
                                 [](const WorkspaceEdit& edit) { return edit.kind == EditKind::Step; });
        };
        check(steps_since(edits_before_rename) == 1, "rename_is_one_change_log_step");
        const std::string rename_step = m_edits.empty() ? std::string() : m_edits.back().label;
        const std::size_t before_adapter_undo = m_changes.size();
        check(m_workspace->undo().succeeded(), "rename_undo");
        check(!m_edits.empty() && m_edits.back().kind == EditKind::Undo && m_edits.back().label == rename_step,
              "undo_is_logged_with_the_step_undone");
        check(m_changes.size() == before_adapter_undo + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::History),
              "adapter_undo_observed_once");
        check(find_object(m_workspace->snapshot(), m_first)->name != "Adapter Renamed", "rename_undo_projected");
        const std::size_t before_adapter_redo = m_changes.size();
        check(m_workspace->redo().succeeded(), "rename_redo");
        check(!m_edits.empty() && m_edits.back().kind == EditKind::Redo && m_edits.back().label == rename_step,
              "redo_is_logged_with_the_step_redone");
        check(m_changes.size() == before_adapter_redo + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::History),
              "adapter_redo_observed_once");
        check(find_object(m_workspace->snapshot(), m_first)->name == "Adapter Renamed", "rename_redo_projected");

        const CommandResult duplicate = m_workspace->duplicate_object(m_first);
        check(duplicate.succeeded() && duplicate.object_id.has_value(), "adapter_duplicate_returns_id");
        m_duplicate = duplicate.object_id.value_or(ObjectId());
        check(find_object(m_workspace->snapshot(), m_duplicate) != nullptr, "duplicate_id_resolves");
        check(m_workspace->undo().succeeded(), "duplicate_undo");
        check(find_object(m_workspace->snapshot(), m_duplicate) == nullptr, "duplicate_undo_removes_id");
        check(m_workspace->redo().succeeded(), "duplicate_redo");
        check(find_object(m_workspace->snapshot(), m_duplicate) != nullptr, "duplicate_redo_restores_same_id");

        check(m_workspace->remove_object(m_duplicate).succeeded(), "adapter_remove");
        check(m_workspace->select_object(m_duplicate).error == WorkspaceError::StaleId, "removed_id_is_stale");
        check(m_workspace->undo().succeeded(), "remove_undo");
        check(find_object(m_workspace->snapshot(), m_duplicate) != nullptr, "remove_undo_restores_id");
        check(m_workspace->redo().succeeded(), "remove_redo");
        check(find_object(m_workspace->snapshot(), m_duplicate) == nullptr, "remove_redo_projects_removal");

        const std::size_t native_index = model_index(m_second);
        const std::size_t before_native_rename = m_changes.size();
        check(native_index != invalid_index && m_plater->rename_object(native_index, "Native Renamed"), "native_rename_command");
        check(m_changes.size() == before_native_rename + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Contents),
              "native_content_change_observed");
        const std::size_t before_native_undo = m_changes.size();
        m_plater->undo();
        check(m_changes.size() == before_native_undo + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::History),
              "native_undo_observed_once");
        check(find_object(m_workspace->snapshot(), m_second)->name != "Native Renamed", "native_undo_projected");
        const std::size_t before_native_redo = m_changes.size();
        m_plater->redo();
        check(m_changes.size() == before_native_redo + 1 &&
                  has_reason(m_changes.back().reasons, WorkspaceChangeReasons::History),
              "native_redo_observed_once");
        check(find_object(m_workspace->snapshot(), m_second)->name == "Native Renamed", "native_redo_projected");

        // Switching the active plate records a "select partplate!" step,
        // which the "!" rule excludes: not an edit.
        const std::size_t edits_before_plate_switch = m_edits.size();
        m_plater->select_plate(1);
        m_plater->select_plate(0);
        check(m_edits.size() == edits_before_plate_switch, "plate_switch_is_not_a_change_log_edit");

        // Mirror reaches no project-state notification of its own; the undo
        // step it records is what the change log reads.
        check(m_workspace->select_object(m_first).succeeded(), "select_for_mirror");
        const std::size_t edits_before_mirror = m_edits.size();
        m_plater->mirror(X);
        check(steps_since(edits_before_mirror) == 1, "mirror_is_one_change_log_step");

        // The transform check below starts from no selection, as it did
        // before these checks existed.
        m_plater->deselect_all();

        const bool was_shown = m_plater->IsShown();
        m_plater->Show(false);
        check(!m_plater->can_undo(), "legacy_history_gate_hidden");
        check(m_workspace->snapshot().can_undo, "model_history_available_when_legacy_panel_hidden");
        m_plater->Show(was_shown);

        check(m_workspace->select_object(m_first).succeeded(), "select_for_transform");
        m_transform_events_before = transform_event_count();
        Selection& selection = m_plater->canvas3D()->get_selection();
        selection.setup_cache();
        selection.translate(Vec3d(3.0, 0.0, 0.0), TransformationType::World);
        m_plater->canvas3D()->do_move("JusPrin integration move");
    }

    void verify_selection_tail_redo()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "redo_tail_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "redo_tail_cube_loaded");
        check(m_plater->rename_object(0, "Redo Tail Renamed"), "redo_tail_rename_succeeds");
        m_plater->deselect_all();
        check(m_plater->undo_project(), "redo_tail_undo_succeeds");
        check(m_plater->model().objects.front()->name != "Redo Tail Renamed", "redo_tail_undo_restores_name");
        check(m_plater->redo_project(), "redo_tail_redo_succeeds");
        check(m_plater->model().objects.front()->name == "Redo Tail Renamed", "redo_tail_redo_restores_name");
    }

    void verify_cross_plate_duplicate()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "duplicate_plate_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "duplicate_plate_cube_loaded");
        PartPlateList& plates = m_plater->get_partplate_list();
        check(plates.create_plate(true) == 1, "duplicate_plate_second_plate_created");
        check(plates.select_plate(1) == 0, "duplicate_plate_second_plate_selected");
        const int duplicate = m_plater->duplicate_object(0);
        check(duplicate == 1, "duplicate_plate_object_created");
        check(duplicate >= 0 && plates.get_plate(1)->contain_instance(duplicate, 0),
              "duplicate_plate_uses_current_plate");
    }

    void verify_plate_membership_after_cut()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "cut_plate_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const auto loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "cut_plate_cube_loaded");
        check(m_plater->duplicate_object(0) == 1, "cut_plate_second_object_created");
        PartPlateList& plates = m_plater->get_partplate_list();
        check(plates.create_plate(true) == 1, "cut_plate_second_plate_created");
        check(plates.add_to_plate(1, 0, 1) == 0, "cut_plate_second_object_moved");
        check(plates.get_plate(0)->add_instance(0, 0, true) == 0, "cut_plate_first_object_centered");
        m_plater->canvas3D()->reload_scene(true, true);
        OrcaWorkspaceAdapter adapter(*m_plater);
        const auto snapshot = adapter.snapshot();
        const ObjectId first = snapshot.plates[0].objects.front().id;
        const auto survivor_id = m_plater->model().objects[1]->id().id;
        const Vec3d center = m_plater->model().objects[0]->instance_bounding_box(0).center();
        DivideRequest request;
        request.point = Vec3{center.x(), center.y(), center.z()};
        DivideResult divided;
        check(adapter.divide_object(first, request, divided).succeeded(), "cut_plate_divide_succeeds");
        const auto check_membership = [this, &plates](const std::string& label) {
            std::vector<int> live;
            for (std::size_t index = 0; index < m_plater->model().objects.size(); ++index)
                live.push_back(plates.find_instance(static_cast<int>(index), 0));
            plates.reload_all_objects();
            bool equal = true;
            for (std::size_t index = 0; index < live.size(); ++index)
                equal &= live[index] == plates.find_instance(static_cast<int>(index), 0);
            check(equal, label + "_membership_matches_rebuild");
        };
        const auto& objects = m_plater->model().objects;
        const auto survivor = std::find_if(objects.begin(), objects.end(), [survivor_id](const ModelObject* object) {
            return object->id().id == survivor_id;
        });
        check(survivor != objects.end(), "cut_plate_survivor_exists");
        if (survivor != objects.end()) {
            const int index = static_cast<int>(std::distance(objects.begin(), survivor));
            check(plates.find_instance(index, 0) == 1, "cut_plate_survivor_stays_on_second_plate");
        }
        check_membership("cut_plate");
        check(adapter.undo().succeeded(), "cut_plate_undo_succeeds");
        check_membership("cut_plate_undo");
        check(adapter.redo().succeeded(), "cut_plate_redo_succeeds");
        check_membership("cut_plate_redo");
    }

    void verify_local_versions()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "versions_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const auto loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "versions_cube_loaded");
        ProjectVersionStore store(std::filesystem::path(data_dir()) / "version-test", "p-harness");
        std::string document_state = "{\"project\":{\"projectId\":\"p-harness\"},\"attachments\":[]}";
        const auto capture = [this, &store, &document_state](const std::string& label) {
            const std::string id = ProjectVersionStore::new_id();
            const std::filesystem::path metadata = store.pending_metadata_path(id);
            check(m_plater->export_3mf(fs::path(metadata.string()), SaveStrategy::Backup | SaveStrategy::Silence) >= 0,
                  "versions_metadata_" + label);
            auto frozen = store.freeze(m_plater->model(), id, 1, document_state, "", label);
            const std::size_t changed = frozen.changed_objects.size();
            store.publish(std::move(frozen));
            return std::make_pair(id, changed);
        };
        const auto initial = capture("initial");
        check(initial.second == 1, "versions_initial_writes_one_resource");
        const auto version_root = std::filesystem::path(data_dir()) / "version-test" / "p-harness" / "versions";
        check(!std::filesystem::exists(version_root / initial.first / "state.json"),
              "versions_do_not_copy_current_document");
        const std::string live_state = "{\"project\":{\"projectId\":\"p-harness\"},\"attachments\":[],\"draft\":\"partial reply\"}";
        store.publish_document(live_state);
        document_state = live_state;
        const auto current_document = store.materialize(initial.first);
        check(store.history().size() == 1 && current_document.semantic_state == live_state,
              "versions_current_document_advances_without_model_version");
        store.remove_materialization(current_document);
        const std::string unchanged_id = ProjectVersionStore::new_id();
        const auto unchanged_metadata = store.pending_metadata_path(unchanged_id);
        check(m_plater->export_3mf(fs::path(unchanged_metadata.string()), SaveStrategy::Backup | SaveStrategy::Silence) >= 0,
              "versions_unchanged_model_metadata_captured");
        const auto unchanged = store.freeze(m_plater->model(), unchanged_id, 2, document_state, "", "semantic-only");
        check(store.unchanged(unchanged), "versions_document_update_does_not_change_checkpoint_equality");
        store.discard(unchanged);
        check(m_plater->rename_object(0, "Renamed cube"), "versions_rename");
        const auto renamed = capture("renamed");
        check(renamed.second == 0, "versions_rename_reuses_resource");
        check(!std::filesystem::exists(version_root / renamed.first / "state.json"),
              "versions_renamed_checkpoint_has_no_document_copy");
        const auto historical_document = store.materialize(initial.first);
        check(historical_document.semantic_state == live_state,
              "versions_history_uses_current_document_after_model_publish");
        store.remove_materialization(historical_document);
        const auto versions = store.history();
        check(versions.size() == 2 && versions[0].objects[0].resource.id == versions[1].objects[0].resource.id,
              "versions_share_immutable_resource");
        bool second_writer_refused = false;
        try {
            ProjectVersionStore second(std::filesystem::path(data_dir()) / "version-test", "p-harness");
        } catch (const std::exception&) {
            second_writer_refused = true;
        }
        check(second_writer_refused, "versions_concurrent_writer_refused");
        const auto resource = std::filesystem::path(data_dir()) / "version-test" / "p-harness" / "resources" /
                              (versions[0].objects[0].resource.id + ".zip");
        const auto original_resource = resource.string() + ".original";
        std::filesystem::copy_file(resource, original_resource);
        const auto rejects_resource = [&store, &initial] {
            try {
                store.materialize(initial.first);
                return false;
            } catch (const std::exception&) {
                return true;
            }
        };
        {
            std::ofstream broken(resource, std::ios::binary | std::ios::trunc);
            broken << "invalid zip";
        }
        check(rejects_resource(), "versions_corrupt_resource_refused");
        std::filesystem::remove(resource);
        check(rejects_resource(), "versions_missing_resource_refused");
        mz_zip_archive incomplete;
        mz_zip_zero_struct(&incomplete);
        check(open_zip_writer(&incomplete, resource.u8string()), "versions_meshless_zip_created");
        const std::string meshless = "<model xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\"/>";
        const bool meshless_written = mz_zip_writer_add_mem(&incomplete,
            versions[0].objects[0].resource.entry.c_str(), meshless.data(), meshless.size(), MZ_DEFAULT_COMPRESSION) &&
            mz_zip_writer_finalize_archive(&incomplete);
        close_zip_writer(&incomplete);
        check(meshless_written && rejects_resource(), "versions_valid_zip_without_mesh_refused");
        std::filesystem::copy_file(original_resource, resource, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(original_resource);
        store.pin(initial.first);
        store.prune(1, 1024 * 1024);
        check(store.history().size() == 2, "versions_pin_survives_prune");
        store.begin_chat_restore(renamed.first, initial.first, "c-1");
        check(std::filesystem::exists(std::filesystem::path(data_dir()) / "version-test" / "p-harness" /
                                      "chat-restore.pending.json"), "versions_chat_restore_marker_durable");
        store.finish_chat_restore();
        check(!std::filesystem::exists(std::filesystem::path(data_dir()) / "version-test" / "p-harness" /
                                       "chat-restore.pending.json"), "versions_chat_restore_marker_cleared");
        const auto restored = store.materialize(initial.first);
        check(restored.semantic_state == live_state,
              "versions_restore_keeps_current_document");
        m_plater->reset();
        const auto reloaded = m_plater->load_files({fs::path(restored.metadata.string()), fs::path(restored.absent_original.string())},
            LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::Restore, false);
        check(!reloaded.empty() && m_plater->model().objects.size() == 1 &&
              m_plater->model().objects[0]->name != "Renamed cube", "versions_restore_original_model");
        check(m_plater->new_project(true, true) != wxID_CANCEL, "versions_release_restored_project");
        store.remove_materialization(restored);
        const auto empty = capture("empty");
        check(empty.second == 0, "versions_empty_writes_no_resources");
        store.publish_document(live_state);
        store.record_operation("a-harness", "object_edit", "succeeded", renamed.first, empty.first);
        store.prune(1, 1024 * 1024);
        check(store.history().size() == 3, "versions_operation_references_survive_prune");
        const auto empty_restore = store.materialize(empty.first);
        struct Dialogs : wxModalDialogHook {
            int count{0};
            int Enter(wxDialog*) override { ++count; return wxID_CANCEL; }
        } dialogs;
        dialogs.Register();
        m_plater->reset();
        m_plater->load_files({fs::path(empty_restore.metadata.string()), fs::path(empty_restore.absent_original.string())},
            LoadStrategy::Restore | LoadStrategy::AllowEmpty, false);
        dialogs.Unregister();
        check(m_plater->model().objects.empty() && dialogs.count == 0, "versions_empty_restores_without_warning");
        check(m_plater->new_project(true, true) != wxID_CANCEL, "versions_release_empty_project");
        store.remove_materialization(empty_restore);
    }

    void verify_reopened_local_versions()
    {
        const auto root = std::filesystem::path(data_dir()) / "version-test" / "p-harness";
        nlohmann::json head, current;
        std::ifstream head_file(root / "HEAD");
        head_file >> head;
        std::ifstream current_file(root / "current-state.json");
        current_file >> current;
        // Windows will not replace a file that is still open for reading.
        head_file.close();
        current_file.close();
        const std::string legacy_head = head.at("current").get<std::string>();
        std::ofstream(root / "versions" / legacy_head / "state.json") << current.at("state").dump(2);
        std::ofstream(root / "current-state.json") <<
            nlohmann::json{{"schema", 1}, {"baseVersion", legacy_head}, {"state", current.at("state")}}.dump();
        std::filesystem::create_directories(root / "pending" / "interrupted-write");
        std::filesystem::create_directories(root / "versions" / "uncommitted-version");
        std::filesystem::create_directories(root / "materialized" / "stale-restore");
        std::ofstream(root / "current-state.pending") << "interrupted state write";
        std::ofstream(root / "chat-restore.pending.json") << "{}";
        bool interrupted_restore_refused = false;
        try {
            ProjectVersionStore blocked(root.parent_path(), "p-harness");
        } catch (const std::exception& error) {
            interrupted_restore_refused = std::string(error.what()).find("interrupted chat restoration") != std::string::npos;
        }
        check(interrupted_restore_refused && std::filesystem::exists(root / "chat-restore.pending.json"),
              "versions_restart_blocks_interrupted_chat_restore");
        std::filesystem::remove(root / "chat-restore.pending.json");
        ProjectVersionStore reopened(root.parent_path(), "p-harness");
        const auto versions = reopened.history();
        check(!std::filesystem::exists(root / "pending" / "interrupted-write") &&
              !std::filesystem::exists(root / "versions" / "uncommitted-version") &&
              !std::filesystem::exists(root / "materialized" / "stale-restore") &&
              !std::filesystem::exists(root / "current-state.pending"),
              "versions_restart_removes_uncommitted_work");
        check(versions.size() == 3 && reopened.head_id() == versions.back().id,
              "versions_restart_reads_committed_history");
        reopened.publish_document(current.at("state").dump(2));
        nlohmann::json upgraded;
        std::ifstream upgraded_file(root / "current-state.json");
        upgraded_file >> upgraded;
        upgraded_file.close();
        check(upgraded.value("schema", 0) == 2 && !upgraded.contains("baseVersion"),
              "versions_legacy_document_upgrades_without_model_checkpoint");
        const auto current_document = reopened.materialize(reopened.head_id());
        check(current_document.semantic_state.find("partial reply") != std::string::npos,
              "versions_restart_reads_current_document_overlay");
        reopened.remove_materialization(current_document);
        if (!versions.empty()) {
            const auto earlier = reopened.materialize(versions.front().id);
            check(!earlier.empty && std::filesystem::is_regular_file(earlier.metadata),
                  "versions_restart_keeps_earlier_resource");
            reopened.remove_materialization(earlier);
        }
    }

    void verify_paint_version_resources()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "paint_versions_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        check(m_plater->load_files(std::vector<std::string>{cube},
              LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
              "paint_versions_cube_loaded");
        ProjectVersionStore store(std::filesystem::path(data_dir()) / "version-test", "p-paints");
        const auto capture = [this, &store](const std::string& label) {
            const std::string id = ProjectVersionStore::new_id();
            const auto metadata = store.pending_metadata_path(id);
            check(m_plater->export_3mf(fs::path(metadata.string()), SaveStrategy::Backup | SaveStrategy::Silence) >= 0,
                  "paint_versions_metadata_" + label);
            auto frozen = store.freeze(m_plater->model(), id, store.history().size() + 1,
                                       "{\"project\":{\"projectId\":\"p-paints\"},\"attachments\":[]}", "", label);
            const auto changed = frozen.changed_objects.size();
            store.publish(std::move(frozen));
            return changed;
        };
        check(capture("initial") == 1, "paint_versions_initial_resource");
        ModelVolume& volume = *m_plater->model().objects[0]->volumes[0];
        std::array<FacetsAnnotation*, 4> layers{
            &volume.supported_facets, &volume.seam_facets,
            &volume.mmu_segmentation_facets, &volume.fuzzy_skin_facets};
        const std::array<std::string, 4> names{"support", "seam", "material", "fuzzy"};
        for (std::size_t index = 0; index < layers.size(); ++index) {
            TriangleSelector selector(volume.mesh());
            selector.set_facet(0, EnforcerBlockerType::ENFORCER);
            check(layers[index]->set(selector), "paint_versions_" + names[index] + "_applied");
            check(capture(names[index]) == 1, "paint_versions_" + names[index] + "_rewrites_resource");
        }
        std::array<std::string, 4> expected;
        for (std::size_t index = 0; index < layers.size(); ++index)
            expected[index] = layers[index]->get_triangle_as_string(0);
        const auto export_dir = std::filesystem::path(data_dir()) / "version-test";
        const auto staged_export = export_dir / "verified-export.pending.3mf";
        const auto final_export = export_dir / "verified-export.3mf";
        {
            std::ofstream old(final_export, std::ios::binary);
            old << "old output";
        }
        check(m_plater->export_3mf(fs::path(staged_export.string()),
                                  SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::SkipAuxiliary) >= 0,
              "paint_versions_export_staged");
        ProjectVersionStore::publish_export_archive(staged_export, final_export);
        check(std::filesystem::is_regular_file(final_export) && !std::filesystem::exists(staged_export),
              "paint_versions_export_verified_and_replaced");
        const auto restored = store.materialize(store.head_id());
        m_plater->reset();
        const auto loaded = m_plater->load_files({fs::path(restored.metadata.string()), fs::path(restored.absent_original.string())},
            LoadStrategy::Restore, false);
        check(!loaded.empty() && m_plater->model().objects.size() == 1, "paint_versions_reloaded");
        if (!m_plater->model().objects.empty()) {
            const ModelVolume& actual = *m_plater->model().objects[0]->volumes[0];
            const std::array<const FacetsAnnotation*, 4> reloaded{
                &actual.supported_facets, &actual.seam_facets,
                &actual.mmu_segmentation_facets, &actual.fuzzy_skin_facets};
            for (std::size_t index = 0; index < reloaded.size(); ++index)
                check(reloaded[index]->get_triangle_as_string(0) == expected[index],
                      "paint_versions_" + names[index] + "_restored");
        }
        check(m_plater->new_project(true, true) != wxID_CANCEL, "paint_versions_release_project");
        store.remove_materialization(restored);
    }

    void verify_attachment_retention()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "attachment_versions_new_project");
        const auto root = std::filesystem::path(data_dir()) / "version-test";
        ProjectVersionStore store(root, "p-attachments");
        const auto blob = root / "p-attachments" / "attachments" / "a-1" / "note.txt";
        std::filesystem::create_directories(blob.parent_path());
        {
            std::ofstream out(blob, std::ios::binary);
            out << "note";
        }
        const auto capture = [this, &store](const std::string& state, std::uint64_t revision) {
            const std::string id = ProjectVersionStore::new_id();
            const auto metadata = store.pending_metadata_path(id);
            check(m_plater->export_3mf(fs::path(metadata.string()), SaveStrategy::Backup | SaveStrategy::Silence) >= 0,
                  "attachment_versions_metadata_captured");
            store.publish(store.freeze(m_plater->model(), id, revision, state, "", "attachment-test"));
        };
        capture("{\"project\":{\"projectId\":\"p-attachments\"},\"attachments\":[{\"id\":\"a-1\",\"storedName\":\"note.txt\",\"sizeBytes\":4}]}", 1);
        check(std::filesystem::is_regular_file(blob), "attachment_versions_blob_committed");
        capture("{\"project\":{\"projectId\":\"p-attachments\"},\"attachments\":[]}", 2);
        store.prune(1, 1024 * 1024);
        check(store.history().size() == 1 && !std::filesystem::exists(blob),
              "attachment_versions_unreferenced_blob_pruned");
    }

    void verify_legacy_delete_history()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "legacy_delete_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "legacy_delete_cube_loaded");
        check(m_plater->select_object(0), "legacy_delete_object_selected");
        m_app.obj_list()->remove();
        check(m_plater->model().objects.empty(), "legacy_delete_removes_object");

        const char* newest = nullptr;
        const char* previous = nullptr;
        check(m_plater->undo_redo_string_getter(true, 0, &newest) &&
                  std::string(newest).find("Delete Object") == 0,
              "legacy_delete_keeps_object_snapshot");
        check(m_plater->undo_redo_string_getter(true, 1, &previous) && std::string(previous) == "Delete selected",
              "legacy_delete_keeps_selection_snapshot");
    }

    void verify_disabled_gizmo_wheel()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "gizmo_wheel_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "gizmo_wheel_cube_loaded");
        check(m_plater->select_object(0), "gizmo_wheel_object_selected");

        GLGizmosManager& gizmos = m_plater->canvas3D()->get_gizmos_manager();
        check(gizmos.open_gizmo(GLGizmosManager::FdmSupports), "gizmo_wheel_painter_activated");
        wxMouseEvent wheel(wxEVT_MOUSEWHEEL);
        wheel.m_wheelRotation = wheel.m_wheelDelta = 120;
        wheel.SetControlDown(true);
        wheel.SetRawControlDown(true);
        gizmos.set_picker_input_enabled(false);
        check(gizmos.on_mouse_wheel(wheel), "gizmo_wheel_dispatches_when_picker_input_disabled");
        gizmos.set_active_gizmo_input_enabled(false);
        check(!gizmos.on_mouse_wheel(wheel), "gizmo_wheel_respects_active_input_gate");
        gizmos.set_active_gizmo_input_enabled(true);
        gizmos.set_picker_input_enabled(true);
        m_plater->canvas3D()->reset_all_gizmos();
    }

    void setup_manual_canvas()
    {
        m_plater = m_app.plater();
        if (m_plater == nullptr) {
            fail("Plater was not constructed");
            return;
        }

        m_plater->new_project(true, true);
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        m_plater->load_files(std::vector<std::string>{cube},
                             LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        ModelObject* object = m_plater->model().objects.front();
        object->center_around_origin();
        object->instances.front()->set_offset(Vec3d(128.0, 128.0, -object->origin_translation(2)));
        m_plater->update();

        if (m_state->mode == HarnessState::Mode::ManualJusPrin) {
            m_manual_controller = std::make_unique<CanvasPresentationController>(*m_plater->canvas3D());
            m_manual_controller->toggle_tool(GLGizmosManager::Move);
        }
        if (m_app.mainframe != nullptr) {
            m_app.mainframe->Bind(wxEVT_CLOSE_WINDOW, [weak = weak_from_this()](wxCloseEvent& event) {
                if (const auto self = weak.lock(); self && self->m_manual_controller)
                    self->m_manual_controller->detach();
                event.Skip();
            });
            m_app.mainframe->Maximize();
        }
        auto* ready_timer = new wxTimer();
        ready_timer->Bind(wxEVT_TIMER, [self = shared_from_this(), ready_timer](wxTimerEvent&) {
            ready_timer->Stop();
            if (self->m_app.mainframe != nullptr)
                self->m_app.mainframe->select_tab(MainFrame::tp3DEditor);
            self->m_plater->canvas3D()->reload_scene(true, true);
            self->m_plater->select_object(0);
            std::cerr << "HARNESS MANUAL READY "
                      << (self->m_state->mode == HarnessState::Mode::ManualJusPrin ? "jusprin" : "stock") << '\n';
            delete ready_timer;
        });
        ready_timer->StartOnce(500);
        m_state->result = 0;
    }

    void verify_committed_transform()
    {
        try {
            check(transform_event_count() == m_transform_events_before + 1, "committed_move_observed_once");
            // The move reaches the adapter through the queued
            // EVT_GLCANVAS_INSTANCE_MOVED, interleaved with queued
            // EVT_GLCANVAS_OBJECT_SELECT events that each publish a
            // Selection-only change, so the move need not be the last change
            // (on Windows two follow it). Skip those.
            const auto last = std::find_if(m_changes.rbegin(), m_changes.rend(), [](const WorkspaceChanged& change) {
                return change.reasons != WorkspaceChangeReasons::Selection;
            });
            check(last != m_changes.rend() && has_reason(last->reasons, WorkspaceChangeReasons::Transform),
                  "committed_move_has_transform_reason");

            GLCanvas3D& canvas = *m_plater->canvas3D();
            const bool stock_hidden = canvas.legacy_overlays_hidden();
            CanvasPresentationController controller(canvas);
            check(canvas.legacy_overlays_hidden(), "policy_hides_legacy_overlays");
            check(!canvas.get_gizmos_manager().is_picker_input_enabled(),
                  "policy_hides_gizmo_picker_input");
            check(canvas.get_gizmos_manager().is_active_gizmo_input_enabled(),
                  "policy_keeps_active_gizmo_input");
            // The tool strip's buttons toggle: a click opens the tool, a
            // click on another switches to it, and a second click closes it.
            const auto open_tool = [&canvas]() { return canvas.get_gizmos_manager().get_current_type(); };
            check(controller.toggle_tool(GLGizmosManager::Move), "controller_opens_move");
            check(open_tool() == GLGizmosManager::Move, "move_is_open");
            check(controller.toggle_tool(GLGizmosManager::Rotate), "controller_switches_to_rotate");
            check(open_tool() == GLGizmosManager::Rotate, "rotate_replaces_move");
            check(controller.toggle_tool(GLGizmosManager::Rotate), "controller_closes_rotate");
            check(open_tool() == GLGizmosManager::Undefined, "second_click_closes_rotate");
            check(controller.toggle_tool(GLGizmosManager::Scale), "controller_opens_scale");
            check(open_tool() == GLGizmosManager::Scale, "scale_is_open");
            check(controller.toggle_tool(GLGizmosManager::Scale), "controller_closes_scale");
            check(open_tool() == GLGizmosManager::Undefined, "second_click_closes_scale");
            controller.detach();
            check(canvas.legacy_overlays_hidden() == stock_hidden, "controller_restores_stock_overlays");

            const ProjectSessionId old_session = m_workspace->snapshot().session;
            const ObjectId old_id = m_first;
            check(m_plater->new_project(true, true) != wxID_CANCEL, "project_replacement");
            check(m_workspace->snapshot().session != old_session, "project_replacement_changes_session");
            check(m_workspace->select_object(old_id).error == WorkspaceError::StaleId, "prior_session_id_is_stale");
            const std::size_t before_unavailable = m_changes.size();
            check(m_workspace->undo().error == WorkspaceError::UnavailableOperation, "unavailable_undo_reports_failure");
            check(m_changes.size() == before_unavailable, "unavailable_undo_emits_no_event");

            auto teardown_workspace = std::make_shared<std::unique_ptr<OrcaWorkspaceAdapter>>(
                std::make_unique<OrcaWorkspaceAdapter>(*m_plater));
            check((*teardown_workspace)->select_object(old_id).error == WorkspaceError::StaleId,
                  "recreated_adapter_preserves_project_session");
            auto teardown_subscription = std::make_shared<WorkspaceSubscription>();
            bool teardown_called = false;
            *teardown_subscription = (*teardown_workspace)->subscribe(
                [teardown_workspace, teardown_subscription, &teardown_called](const WorkspaceChanged&) {
                    teardown_called = true;
                    teardown_subscription->reset();
                    teardown_workspace->reset();
                });
            m_plater->get_partplate_list().create_plate(true);
            check(teardown_called && !*teardown_workspace, "adapter_teardown_during_native_dispatch_is_safe");

            verify_managed_mesh_versions();
        } catch (const std::exception& error) {
            fail(std::string("transform verification exception: ") + error.what());
        } catch (...) {
            fail("unknown transform verification exception");
        }
    }

    // Managed projects retain changed meshes in the version store. Region
    // paint and repair must advance that resource without Orca's backup cache.
    void verify_managed_mesh_versions()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "managed_mesh_new_project");
        const fs::path open_cube = fs::path(data_dir()) / "open-cube.stl";
        {
            std::ifstream in(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl", std::ios::binary);
            std::stringstream stl;
            stl << in.rdbuf();
            std::string text = stl.str();
            const std::size_t first = text.find("  facet");
            const std::size_t last = text.find('\n', text.find("endfacet", first));
            text.erase(first, last + 1 - first);
            std::ofstream(open_cube.string(), std::ios::binary) << text;
        }
        const auto loaded = m_plater->load_files(
            std::vector<std::string>{open_cube.string()}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "managed_mesh_open_cube_loaded");
        auto* shell = Slic3r::GUI::JusPrin::installed_shell();
        auto* autosave = shell ? shell->autosave() : nullptr;
        check(autosave != nullptr, "managed_mesh_autosave_available");
        if (loaded.size() != 1 || autosave == nullptr) {
            finish();
            return;
        }
        Model& model = m_plater->model();
        ModelObject& object = *model.objects.front();
        const fs::path backup_file = model.get_backup_path() + "/3D/Objects/" + object.name + "_" +
                                     std::to_string(model.get_object_backup_id(object)) + ".model";
        auto saved_resource = [this, autosave](const std::string& label) {
            check(autosave->save_now(), label + "_saved");
            const auto versions = autosave->history();
            check(!versions.empty() && !versions.back().objects.empty(), label + "_version_has_mesh");
            return versions.empty() || versions.back().objects.empty() ? std::string() :
                versions.back().objects.front().resource.id;
        };
        std::string resource = saved_resource("managed_mesh_loaded");
        const std::string initial_version = autosave->current_version();
        const std::string initial_layer_height = m_app.preset_bundle->prints.get_edited_preset().config.opt_serialize("layer_height");
        bool found_metadata = false;
        for (const auto& project : autosave->projects()) {
            const auto metadata = std::filesystem::u8path(project.store_path) / "versions" /
                                  autosave->current_version() / "metadata.3mf";
            if (!std::filesystem::is_regular_file(metadata))
                continue;
            found_metadata = true;
            mz_zip_archive archive;
            mz_zip_zero_struct(&archive);
            check(open_zip_reader(&archive, metadata.u8string()), "managed_mesh_metadata_opens");
            size_t size = 0;
            void* data = mz_zip_reader_extract_file_to_heap(&archive, "Metadata/project_settings.config", &size, 0);
            check(data != nullptr, "managed_mesh_project_settings_present");
            if (data != nullptr) {
                const auto config = nlohmann::json::parse(std::string(static_cast<const char*>(data), size));
                mz_free(data);
                std::set<std::string> saved_keys;
                for (auto it = config.begin(); it != config.end(); ++it)
                    if (it.key() != "version" && it.key() != "name" && it.key() != "from" &&
                        it.key() != "different_settings_to_system")
                        saved_keys.insert(it.key());
                const auto& groups = config.at("different_settings_to_system");
                check(groups.size() == config.at("filament_colour").size() + 2,
                      "managed_mesh_different_settings_group_count");
                for (std::size_t index = 0; index < groups.size(); ++index) {
                    std::vector<std::string> keys;
                    check(unescape_strings_cstyle(groups[index].get<std::string>(), keys),
                          "managed_mesh_different_settings_decode_" + std::to_string(index));
                    const std::set<std::string> protected_keys(keys.begin(), keys.end());
                    for (const std::string& key : saved_keys)
                        if (protected_keys.find(key) == protected_keys.end()) {
                            std::cerr << "HARNESS MISSING PROTECTED KEY " << index << ' ' << key << '\n';
                            break;
                        }
                    check(std::includes(protected_keys.begin(), protected_keys.end(),
                                        saved_keys.begin(), saved_keys.end()),
                          "managed_mesh_all_settings_protected_" + std::to_string(index));
                }
            }
            close_zip_reader(&archive);
            break;
        }
        check(found_metadata, "managed_mesh_metadata_found");
        RegionRecord region;
        region.id = "r1";
        region.kind = "support";
        region.session = m_workspace->snapshot().session.value();
        region.object = object.id().id;
        region.object_name = object.name;
        region.part_facets = {object.volumes.front()->mesh().facets_count()};
        region.artifacts.push_back({"paint", "support", "enforcer", {}, 0, {1, 2, 3}});
        std::vector<RegionRecord> applied;
        check(m_workspace->apply_regions({region}, {}, applied).succeeded(), "managed_mesh_region_paint_succeeds");
        std::string next = saved_resource("managed_mesh_region_paint");
        check(!next.empty() && next != resource, "managed_mesh_region_paint_changes_resource");
        resource = next;
        check(m_workspace->remove_regions({region}).succeeded(), "managed_mesh_region_removal_succeeds");
        next = saved_resource("managed_mesh_region_removal");
        check(!next.empty() && next != resource, "managed_mesh_region_removal_changes_resource");
        resource = next;
        RepairResult repaired;
        const ObjectId id = m_workspace->snapshot().plates.front().objects.front().id;
        check(m_workspace->repair_object(id, repaired).succeeded() && repaired.changed,
              "managed_mesh_repair_succeeds");
        next = saved_resource("managed_mesh_repair");
        check(!next.empty() && next != resource, "managed_mesh_repair_changes_resource");
        check(!fs::exists(backup_file), "managed_mesh_skips_orca_backup_cache");
        check(autosave->restore(initial_version), "managed_mesh_restores_protected_checkpoint");
        check(m_plater->model().objects.size() == 1, "managed_mesh_restores_original_object");
        check(m_app.preset_bundle->prints.get_edited_preset().config.opt_serialize("layer_height") == initial_layer_height,
              "managed_mesh_restores_layer_height");
        m_plater->sidebar().add_filament();
        check(m_app.preset_bundle->filament_presets.size() == 2, "managed_mesh_second_filament_added");
        check(autosave->save_now(), "managed_mesh_two_filament_checkpoint_saved");
        const std::string two_filament_version = autosave->current_version();
        m_plater->sidebar().delete_filament(1, 0);
        check(m_app.preset_bundle->filament_presets.size() == 1, "managed_mesh_second_filament_removed");
        check(autosave->save_now(), "managed_mesh_one_filament_checkpoint_saved");
        // Managed restore answers the false-positive modified-G-code warning
        // before it is shown. This outer hook keeps the test non-blocking and
        // fails on that warning or any other dialog escaping the restore scope.
        struct RestoreDialogs : wxModalDialogHook {
            std::vector<std::string> titles;
            int Enter(wxDialog* dialog) override {
                titles.emplace_back(dialog->GetTitle().ToStdString());
                return wxID_OK;
            }
        } restore_dialogs;
        restore_dialogs.Register();
        const bool restored_two_filaments = autosave->restore(two_filament_version);
        restore_dialogs.Unregister();
        check(restored_two_filaments, "managed_mesh_two_filament_checkpoint_restored");
        check(m_app.preset_bundle->filament_presets.size() == 2, "managed_mesh_two_filament_slots_restored");
        check(restore_dialogs.titles.empty(), "managed_mesh_checkpoint_restore_shows_no_dialog");
        std::cerr << "HARNESS CHECKPOINT_RESTORE_DIALOGS count=" << restore_dialogs.titles.size();
        for (const auto& title : restore_dialogs.titles) std::cerr << " title=" << title;
        std::cerr << '\n';
        finish();
    }

    // Polls on a timer so the event loop, and the backup's hand-offs to it,
    // keep running.
    void wait_until(std::function<bool()> ready, int timeout_ms, std::function<void(bool)> then)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        m_poll              = std::make_unique<wxTimer>();
        m_poll->Bind(wxEVT_TIMER, [this, ready, then, deadline](wxTimerEvent&) {
            const bool ok = ready();
            if (!ok && std::chrono::steady_clock::now() < deadline)
                return;
            m_poll->Stop();
            m_app.CallAfter([then, ok] { then(ok); });
        });
        m_poll->Start(50);
    }

    std::size_t model_index(ObjectId id) const
    {
        const ModelObjectPtrs& objects = m_plater->model().objects;
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index]->id().id == id.value())
                return index;
        return invalid_index;
    }

    std::size_t transform_event_count() const
    {
        return static_cast<std::size_t>(std::count_if(m_changes.begin(), m_changes.end(), [](const WorkspaceChanged& change) {
            return has_reason(change.reasons, WorkspaceChangeReasons::Transform);
        }));
    }

    void fail(const std::string& message)
    {
        std::cerr << "HARNESS ERROR " << message << '\n';
        ++m_failures;
        finish();
    }

    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        m_subscription.reset();
        m_workspace.reset();
        const int result = m_failures == 0 ? 0 : 1;
        std::cerr << "HARNESS RESULT " << (result == 0 ? "PASS" : "FAIL") << " failures=" << m_failures << '\n';
        m_state->result = result;
        m_state->stop = true;
        // On macOS Orca adds each web view's script handler from a CallAfter
        // that waits on the page in a nested event loop, and in this harness,
        // which does its work in one synchronous run, the first one has not
        // returned by now, so Orca has not run post_init. Closing the frame
        // ends that wait, and the web view setup and post_init it held back
        // then run against destroyed windows. This harness cannot use Orca's
        // normal shutdown until post_init completes; the shell harness covers
        // normal shutdown separately. End this process with the check result.
        if (m_app.mainframe == nullptr || !m_app.post_initialized()) {
            std::cerr.flush();
            std::_Exit(result);
        }
        // Leave the way the application leaves, as the shell harness does: a
        // forced close runs MainFrame::shutdown(), which clears the backup
        // callback and stops background threads, and the loop ends once the
        // frame is gone. ExitMainLoop() skipped that, and a backup posted
        // shortly after a model load then reached a deleted frame. Deferred
        // so nothing on the current stack uses the frame after shutdown.
        m_app.CallAfter([frame = m_app.mainframe] { frame->Close(true); });
    }

    static constexpr std::size_t invalid_index = static_cast<std::size_t>(-1);

    GUI_App&                              m_app;
    std::shared_ptr<HarnessState>         m_state;
    Plater*                               m_plater{nullptr};
    std::unique_ptr<OrcaWorkspaceAdapter> m_workspace;
    WorkspaceSubscription                 m_subscription;
    std::vector<WorkspaceChanged>         m_changes;
    WorkspaceSubscription                 m_edit_subscription;
    std::vector<WorkspaceEdit>            m_edits;
    ObjectId                              m_first;
    ObjectId                              m_second;
    ObjectId                              m_duplicate;
    std::size_t                           m_transform_events_before{0};
    int                                   m_failures{0};
    bool                                  m_finished{false};
    std::unique_ptr<CanvasPresentationController> m_manual_controller;
    std::unique_ptr<wxTimer>              m_poll;
};

void start_when_ready(GUI_App& app, const std::shared_ptr<HarnessState>& state)
{
    if (state->stop)
        return;
    if (std::chrono::steady_clock::now() >= state->deadline) {
        std::cerr << "HARNESS ERROR application did not become ready before timeout\n";
        state->result = 1;
        state->stop = true;
        app.ExitMainLoop();
        return;
    }
    if (app.mainframe != nullptr && app.plater() != nullptr) {
        auto scenario = std::make_shared<Scenario>(app, state);
        state->runner = scenario;
        scenario->start();
        return;
    }
    app.CallAfter([&app, state] { start_when_ready(app, state); });
}

} // namespace
} // namespace Slic3r::GUI::JusPrin::Workspace

int main(int argc, char** argv)
{
    using namespace Slic3r;
    using namespace Slic3r::GUI;
    using namespace Slic3r::GUI::JusPrin::Workspace;

    const fs::path original_directory = fs::current_path();
    const fs::path data_directory = fs::temp_directory_path() / fs::unique_path("jusprin-workspace-%%%%-%%%%-%%%%");
    fs::create_directories(data_directory / "log");
    fs::copy_file(fs::path(JUSPRIN_SOURCE_DIR) / "tests/data/jusprin/harness.conf",
                  data_directory / (std::string(SLIC3R_APP_KEY) + ".conf"), fs::copy_option::overwrite_if_exists);

    const fs::path resources = fs::path(JUSPRIN_SOURCE_DIR) / "resources";
    set_resources_dir(resources.string());
    set_var_dir((resources / "images").string());
    set_local_dir((resources / "i18n").string());
    set_sys_shapes_dir((resources / "shapes").string());
    set_custom_gcodes_dir((resources / "custom_gcodes").string());
    set_data_dir(data_directory.string());
    set_temporary_dir(data_directory.string());
    save_main_thread_id();

    auto state = std::make_shared<HarnessState>();
    std::vector<char*> gui_arguments;
    gui_arguments.reserve(static_cast<std::size_t>(argc));
    gui_arguments.emplace_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--manual-stock")
            state->mode = HarnessState::Mode::ManualStock;
        else if (argument == "--manual-jusprin")
            state->mode = HarnessState::Mode::ManualJusPrin;
        else if (argument == "--crash-write" || argument == "--crash-verify") {
            if (index + 2 >= argc) {
                std::cerr << argument << " requires a store root and publication stage\n";
                return 2;
            }
            state->mode = argument == "--crash-write" ? HarnessState::Mode::CrashWrite : HarnessState::Mode::CrashVerify;
            state->crash_root = std::filesystem::absolute(argv[++index]);
            state->crash_stage = argv[++index];
            if (state->crash_stage != "resources" && state->crash_stage != "version" && state->crash_stage != "head")
                return 2;
        }
        else
            gui_arguments.emplace_back(argv[index]);
    }
    std::thread installer([state] {
        while (!state->stop && std::chrono::steady_clock::now() < state->deadline) {
            if (auto* app = dynamic_cast<GUI_App*>(wxApp::GetInstance())) {
                app->CallAfter([app, state] { start_when_ready(*app, state); });
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        state->result = 1;
        state->stop = true;
    });

    GUI_InitParams params;
    params.argc = static_cast<int>(gui_arguments.size());
    params.argv = gui_arguments.data();
    const int gui_result = GUI_Run(params);
    state->stop = true;
    installer.join();

    fs::current_path(original_directory);
    // The boost::log sink keeps log/debug_*.log.0 open until static
    // destruction, so on Windows this cannot delete everything; a throwing
    // remove_all would end a PASS run in std::terminate. Report instead.
    boost::system::error_code error;
    fs::remove_all(data_directory, error);
    if (error)
        std::cerr << "HARNESS WARNING data dir not fully removed: " << error.message() << " ("
                  << data_directory.string() << ")\n";
    if (state->result < 0)
        return gui_result == 0 ? 1 : gui_result;
    return state->result;
}
