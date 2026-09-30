#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Geometry.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"
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

#include <algorithm>
#include <atomic>
#include <chrono>
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
    enum class Mode { Automated, ManualStock, ManualJusPrin };

    std::atomic<int>  result{-1};
    std::atomic<bool> stop{false};
    std::shared_ptr<void> runner;
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + std::chrono::seconds(180)};
    Mode mode{Mode::Automated};
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
        const auto result = m_workspace->apply_settings(patch, settings_confirmation(preview), applied);
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
        check(m_workspace->apply_settings(inverse, settings_confirmation(m_workspace->preview_settings(inverse)), applied).succeeded(), "settings_inverse_applies");
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
        check(m_workspace->apply_settings(maximum, settings_confirmation(m_workspace->preview_settings(maximum)), applied).succeeded(), "settings_apply_printer_maximum");
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
        check(m_workspace->apply_settings(scarf, settings_confirmation(m_workspace->preview_settings(scarf)), applied).succeeded(), "settings_height_with_support_gap");
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
        check(m_workspace->apply_settings(multiline, settings_confirmation(prediction), applied).succeeded(), "settings_multiline_apply");
        check(config.opt_int("fill_multiline") == 1, "settings_multiline_actual_reset");
        check(std::any_of(applied.warnings.begin(), applied.warnings.end(), [](const auto& issue) {
            return issue.key == "fill_multiline" && issue.code == "normalized";
        }), "settings_multiline_actual_reported");
        tab->activate_option("wall_loops", "Strength");
        const SettingsPatch walls{{{"wall_loops", "4"}}};
        const auto confirmed = settings_confirmation(m_workspace->preview_settings(walls));
        DynamicPrintConfig gui_edit;
        gui_edit.set_deserialize_strict("wall_loops", "5");
        const auto before_gui_edit = m_changes.size();
        tab->load_config(gui_edit);
        check(m_changes.size() == before_gui_edit + 1 && has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Settings), "settings_gui_edit_one_revision");
        check(m_workspace->apply_settings(walls, confirmed, applied).error == WorkspaceError::StaleSettings, "settings_gui_edit_invalidates_confirmed_values");
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
        const auto confirm = [this](const SettingsPatch& patch) { return settings_confirmation(m_workspace->preview_settings(patch)); };
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
        check(m_workspace->apply_settings(unsaved, confirm(unsaved), applied).succeeded() && applied.preset_dirty &&
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
        check(m_workspace->apply_settings(save, confirm(save), applied).succeeded() && applied.saved_as == saved_name && !applied.preset_dirty,
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
            check(m_workspace->apply_settings(in_place, confirm(in_place), applied).succeeded() && applied.saved_as == saved_name,
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
        check(m_workspace->apply_settings(warmer, settings_confirmation(warmer_preview), applied).succeeded() &&
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

            verify_backup_mesh_rewrites();
        } catch (const std::exception& error) {
            fail(std::string("transform verification exception: ") + error.what());
        } catch (...) {
            fail("unknown transform verification exception");
        }
    }

    // Orca's crash backup keeps each object's meshes and paint in a file of its
    // own, rewritten a few seconds after something queues it. Region paint and
    // repair change that file's contents, so each must queue it, as Orca's
    // paint tools do, or a crash restores the object without the change. The
    // file is deleted before each edit and must come back.
    void verify_backup_mesh_rewrites()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "backup_mesh_new_project");
        // A cube with its first triangle gone, so repair has a hole to close.
        const fs::path open_cube = fs::path(data_dir()) / "open-cube.stl";
        {
            std::ifstream     in(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl", std::ios::binary);
            std::stringstream stl;
            stl << in.rdbuf();
            std::string       text  = stl.str();
            const std::size_t first = text.find("  facet");
            const std::size_t last  = text.find('\n', text.find("endfacet", first));
            text.erase(first, last + 1 - first);
            std::ofstream(open_cube.string(), std::ios::binary) << text;
        }
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{open_cube.string()}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        check(loaded.size() == 1, "backup_mesh_open_cube_loaded");
        if (loaded.size() != 1) {
            finish();
            return;
        }
        Model&             model  = m_plater->model();
        ModelObject&       object = *model.objects.front();
        const std::string  file   = model.get_backup_path() + "/3D/Objects/" + object.name + "_" +
                                 std::to_string(model.get_object_backup_id(object)) + ".model";
        const auto written = [file] { return fs::exists(file) && !fs::exists(file + ".tmp"); };

        RegionRecord region;
        region.id          = "r1";
        region.kind        = "support";
        region.session     = m_workspace->snapshot().session.value();
        region.object      = object.id().id;
        region.object_name = object.name;
        region.part_facets = {object.volumes.front()->mesh().facets_count()};
        region.artifacts.push_back({"paint", "support", "enforcer", {}, 0, {1, 2, 3}});

        auto self = shared_from_this();
        // Each edit starts from a deleted file and passes when the backup
        // writes it again.
        const auto expect_rewrite = [self, file, written](const std::string& name, std::function<bool()> edit, std::function<void()> next) {
            fs::remove(file);
            const auto start = std::chrono::steady_clock::now();
            self->check(edit(), name + "_succeeds");
            self->wait_until(written, 20000, [self, name, start, next](bool ok) {
                std::cerr << "HARNESS BACKUP " << name << " rewritten=" << ok << " ms="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() << '\n';
                self->check(ok, name + "_rewrites_backup_mesh");
                next();
            });
        };
        wait_until(written, 60000, [self, file, written, region, expect_rewrite](bool ok) {
            self->check(ok, "backup_mesh_written_after_load");
            // Nothing else is queued for the object: without an edit the
            // file stays gone.
            fs::remove(file);
            self->wait_until([file] { return fs::exists(file); }, 5000, [self, region, expect_rewrite](bool rewritten) {
                self->check(!rewritten, "backup_mesh_quiet_without_an_edit");
                expect_rewrite("region_paint", [self, region] {
                    std::vector<RegionRecord> applied;
                    return self->m_workspace->apply_regions({region}, {}, applied).succeeded();
                }, [self, region, expect_rewrite] {
                    expect_rewrite("region_removal", [self, region] { return self->m_workspace->remove_regions({region}).succeeded(); },
                                   [self, expect_rewrite] {
                        expect_rewrite("repair", [self] {
                            RepairResult repaired;
                            const ObjectId id = self->m_workspace->snapshot().plates.front().objects.front().id;
                            return self->m_workspace->repair_object(id, repaired).succeeded() && repaired.changed;
                        }, [self] { self->finish(); });
                    });
                });
            });
        });
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
        // then run against destroyed windows. Leave by ending the loop, as
        // this harness always did there.
        if (m_app.mainframe == nullptr || !m_app.post_initialized()) {
            m_app.ExitMainLoop();
            return;
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
