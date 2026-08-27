#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Geometry.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/JusPrin/CanvasPresentationController.hpp"
#include "slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

#include <wx/app.h>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
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
        m_plater->canvas3D()->reload_scene(true, true);
        check(m_plater->canvas3D()->get_volumes_count() >= 2, "fixture_render_volumes_loaded");

        m_workspace = std::make_unique<OrcaWorkspaceAdapter>(*m_plater);
        m_subscription = m_workspace->subscribe([this](const WorkspaceChanged& change) {
            m_changes.emplace_back(change);
            const WorkspaceSnapshot observed = m_workspace->snapshot();
            check(observed.revision == change.revision, "event_snapshot_revision_agrees");
            check(observed.session == change.session, "event_snapshot_session_agrees");
        });

        const WorkspaceSnapshot initial = m_workspace->snapshot();
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

        check(m_workspace->rename_object(m_first, "Adapter Renamed").succeeded(), "adapter_rename");
        check(find_object(m_workspace->snapshot(), m_first)->name == "Adapter Renamed", "adapter_rename_projected");
        check(m_workspace->undo().succeeded(), "rename_undo");
        check(find_object(m_workspace->snapshot(), m_first)->name != "Adapter Renamed", "rename_undo_projected");
        check(m_workspace->redo().succeeded(), "rename_redo");
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
            m_manual_controller->activate_move();
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
            check(!m_changes.empty() && has_reason(m_changes.back().reasons, WorkspaceChangeReasons::Transform),
                  "committed_move_has_transform_reason");

            GLCanvas3D& canvas = *m_plater->canvas3D();
            const GLCanvasPresentationOptions stock = canvas.presentation_options();
            CanvasPresentationController controller(canvas);
            const GLCanvasPresentationOptions policy = canvas.presentation_options();
            check(!policy.main_toolbar_visible && !policy.main_toolbar_input_enabled, "policy_hides_main_toolbar_and_input");
            check(!policy.gizmo_picker_visible && !canvas.get_gizmos_manager().is_picker_input_enabled(),
                  "policy_hides_gizmo_picker_and_input");
            check(policy.active_gizmo_visible && canvas.get_gizmos_manager().is_active_gizmo_input_enabled(),
                  "policy_keeps_active_gizmo_input");
            check(!policy.plate_controls_visible && !policy.plate_controls_input_enabled, "policy_hides_plate_controls_and_input");
            check(!policy.canvas_toolbar_visible && !policy.canvas_toolbar_input_enabled,
                  "policy_hides_canvas_toolbar_and_input");
            check(controller.activate_move(), "controller_activates_move");
            check(controller.activate_rotate(), "controller_activates_rotate");
            controller.detach();
            check(canvas.presentation_options() == stock, "controller_restores_stock_options");

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

            finish();
        } catch (const std::exception& error) {
            fail(std::string("transform verification exception: ") + error.what());
        } catch (...) {
            fail("unknown transform verification exception");
        }
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
        m_app.ExitMainLoop();
    }

    static constexpr std::size_t invalid_index = static_cast<std::size_t>(-1);

    GUI_App&                              m_app;
    std::shared_ptr<HarnessState>         m_state;
    Plater*                               m_plater{nullptr};
    std::unique_ptr<OrcaWorkspaceAdapter> m_workspace;
    WorkspaceSubscription                 m_subscription;
    std::vector<WorkspaceChanged>         m_changes;
    ObjectId                              m_first;
    ObjectId                              m_second;
    ObjectId                              m_duplicate;
    std::size_t                           m_transform_events_before{0};
    int                                   m_failures{0};
    bool                                  m_finished{false};
    std::unique_ptr<CanvasPresentationController> m_manual_controller;
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
    fs::copy_file(fs::path(JUSPRIN_SOURCE_DIR) / "tests/data/jusprin/OrcaSlicer.conf",
                  data_directory / "OrcaSlicer.conf", fs::copy_option::overwrite_if_exists);

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
    fs::remove_all(data_directory);
    if (state->result < 0)
        return gui_result == 0 ? 1 : gui_result;
    return state->result;
}
