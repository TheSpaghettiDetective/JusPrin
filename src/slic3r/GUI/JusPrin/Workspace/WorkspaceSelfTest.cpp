#include "WorkspaceSelfTest.hpp"

#include "Workspace.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/app.h>
#include <wx/timer.h>

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

// The scenario renames to this fixed string so the transcript diffs cleanly.
constexpr const char* kRenamedTo = "JusPrin SelfTest Renamed";

// Ticks are event-loop turns given back to the application between steps.
constexpr int kTickIntervalMs = 200;
constexpr int kLoadTimeoutTicks = 100; // 20s
constexpr int kUndoAvailableTimeoutTicks = 100; // 20s

bool env_is_one(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

std::string reasons_text(WorkspaceChangeReasons reasons)
{
    std::string result;
    const auto add = [&result](const char* value) {
        if (!result.empty())
            result += '|';
        result += value;
    };
    if (has_reason(reasons, WorkspaceChangeReasons::Selection))
        add("Selection");
    if (has_reason(reasons, WorkspaceChangeReasons::Contents))
        add("Contents");
    if (has_reason(reasons, WorkspaceChangeReasons::History))
        add("History");
    if (has_reason(reasons, WorkspaceChangeReasons::Transform))
        add("Transform");
    if (has_reason(reasons, WorkspaceChangeReasons::Plates))
        add("Plates");
    return result.empty() ? "None" : result;
}

// Same shape as the WorkspaceProbe transcript so the two are comparable.
std::string snapshot_text(const WorkspaceSnapshot& snapshot)
{
    std::ostringstream out;
    out << "revision=" << snapshot.revision << " plates=" << snapshot.plates.size()
        << " active=" << (snapshot.active_plate ? std::to_string(snapshot.active_plate->value()) : "none") << " selection=";
    switch (snapshot.selection_status) {
    case SelectionStatus::None: out << "none"; break;
    case SelectionStatus::Objects:
        for (std::size_t i = 0; i < snapshot.selected_objects.size(); ++i) {
            if (i > 0)
                out << ',';
            out << snapshot.selected_objects[i].value();
        }
        break;
    case SelectionStatus::Unsupported: out << "unsupported"; break;
    }
    out << " can_undo=" << snapshot.can_undo << " can_redo=" << snapshot.can_redo << " objects=[";
    bool first = true;
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects) {
            if (!first)
                out << "; ";
            first = false;
            out << object.id.value() << ':' << object.name;
        }
    out << ']';
    return out.str();
}

// The material content of a snapshot: what an undo is expected to change.
// Deliberately excludes revision, which moves on its own.
std::string objects_text(const WorkspaceSnapshot& snapshot)
{
    std::ostringstream out;
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            out << object.id.value() << ':' << object.name << ';';
    return out.str();
}

const WorkspaceObject* find_object(const WorkspaceSnapshot& snapshot, ObjectId id)
{
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            if (object.id == id)
                return &object;
    return nullptr;
}

std::size_t count_objects(const WorkspaceSnapshot& snapshot)
{
    std::size_t total = 0;
    for (const WorkspacePlate& plate : snapshot.plates)
        total += plate.objects.size();
    return total;
}

class SelfTestRunner final : public wxEvtHandler
{
public:
    SelfTestRunner(Plater& plater, IWorkspace& workspace) : m_plater(plater), m_workspace(workspace)
    {
        if (const char* path = std::getenv("JUSPRIN_WORKSPACE_SELFTEST_LOG"))
            m_log_file.open(path, std::ios::out | std::ios::trunc);

        m_subscription = m_workspace.subscribe([this](const WorkspaceChanged& change) {
            log("EVENT revision=" + std::to_string(change.revision) + " reasons=" + reasons_text(change.reasons));
        });

        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &SelfTestRunner::on_tick, this);
    }

    void start()
    {
        log("BEGIN scenario=rename_object_and_undo");
        m_timer.Start(kTickIntervalMs);
    }

private:
    struct Check
    {
        std::string name;
        bool passed{false};
        std::string detail;
    };

    void log(const std::string& line)
    {
        std::ostringstream ordered;
        ordered << "SELFTEST seq=" << std::setw(4) << std::setfill('0') << ++m_sequence << ' ' << line;
        const std::string text = ordered.str();
        std::cerr << text << std::endl;
        if (m_log_file) {
            m_log_file << text << '\n';
            m_log_file.flush();
        }
    }

    void check(const std::string& name, bool passed, const std::string& detail)
    {
        m_checks.push_back({name, passed, detail});
        log("CHECK " + name + (passed ? " PASS" : " FAIL") + (detail.empty() ? "" : " " + detail));
    }

    // Reads the name the legacy sidebar tree is currently displaying.
    std::string legacy_list_name(int object_index) const
    {
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list == nullptr || object_list->GetModel() == nullptr)
            return "<no object list>";
        const wxDataViewItem item = object_list->GetModel()->GetItemById(object_index);
        if (!item.IsOk())
            return "<no item>";
        return object_list->GetModel()->GetName(item).ToUTF8().data();
    }

    int model_index_of(ObjectId id) const
    {
        const ModelObjectPtrs& objects = m_plater.model().objects;
        for (std::size_t i = 0; i < objects.size(); ++i)
            if (objects[i]->id().id == id.value())
                return static_cast<int>(i);
        return -1;
    }

    void fail_fast(const std::string& reason)
    {
        check("scenario_completed", false, "aborted: " + reason);
        finish();
    }

    void finish()
    {
        m_timer.Stop();
        m_subscription.reset();

        int failed = 0;
        for (const Check& item : m_checks)
            if (!item.passed)
                ++failed;
        log("SUMMARY checks=" + std::to_string(m_checks.size()) + " passed=" + std::to_string(m_checks.size() - failed) +
            " failed=" + std::to_string(failed));
        log(failed == 0 ? "RESULT pass" : "RESULT fail");

        if (m_log_file) {
            m_log_file.flush();
            m_log_file.close();
        }
        std::cerr.flush();
        std::cout.flush();

        // Leave immediately with a deterministic status. A normal wxWidgets
        // shutdown from inside a timer handler would run the whole application
        // teardown and could mask the result.
        std::_Exit(failed == 0 ? 0 : 1);
    }

    void on_tick(wxTimerEvent&)
    {
        switch (m_step) {
        case 0: step_load(); break;
        case 1: step_wait_for_load(); break;
        case 2: step_rename(); break;
        case 3: step_assert_renamed(); break;
        case 4: step_wait_and_undo(); break;
        case 5: step_assert_reverted(); break;
        default: finish(); break;
        }
    }

    void step_load()
    {
        std::string fixture;
        if (const char* override_path = std::getenv("JUSPRIN_WORKSPACE_SELFTEST_FIXTURE"))
            fixture = override_path;
        else
            fixture = resources_dir() + "/jusprin/selftest/selftest_cube.stl";

        log("STEP load fixture=" + fixture);
        try {
            m_plater.load_files(std::vector<std::string>{fixture},
                                LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
        } catch (const std::exception& error) {
            fail_fast(std::string("load_files threw: ") + error.what());
            return;
        }
        ++m_step;
    }

    void step_wait_for_load()
    {
        const WorkspaceSnapshot current = m_workspace.snapshot();
        if (count_objects(current) > 0) {
            log("SNAPSHOT source=after_load " + snapshot_text(current));
            // Plater::can_undo() is false unless the 3D view is the shown panel,
            // so without this the undo step is skipped on most runs.
            m_plater.select_view_3D("3D");
            ++m_step;
            return;
        }
        if (++m_load_ticks > kLoadTimeoutTicks) {
            log("SNAPSHOT source=load_timeout " + snapshot_text(current));
            fail_fast("fixture produced no objects in the snapshot before the timeout");
        }
    }

    void step_rename()
    {
        const WorkspaceSnapshot before = m_workspace.snapshot();
        const WorkspaceObject* target = find_first(before);
        if (target == nullptr) {
            fail_fast("no object available to rename");
            return;
        }
        m_target_id = target->id;
        m_original_name = target->name;
        m_target_index = model_index_of(m_target_id);
        log("STEP rename id=" + std::to_string(m_target_id.value()) + " index=" + std::to_string(m_target_index) + " from=" +
            m_original_name + " to=" + kRenamedTo);

        const CommandResult result = m_workspace.rename_object(m_target_id, kRenamedTo);
        check("rename_command_succeeded", result.succeeded(), result.succeeded() ? "" : "message=" + result.message);
        ++m_step;
    }

    void step_assert_renamed()
    {
        const WorkspaceSnapshot current = m_workspace.snapshot();
        log("SNAPSHOT source=after_rename " + snapshot_text(current));

        const WorkspaceObject* object = find_object(current, m_target_id);
        if (object == nullptr)
            check("snapshot_shows_new_name", false, "object id=" + std::to_string(m_target_id.value()) + " absent from snapshot");
        else
            check("snapshot_shows_new_name", object->name == kRenamedTo, "expected=" + std::string(kRenamedTo) + " actual=" + object->name);

        const std::string listed = legacy_list_name(m_target_index);
        check("legacy_object_list_shows_new_name", listed == kRenamedTo, "expected=" + std::string(kRenamedTo) + " actual=" + listed);
        ++m_step;
    }

    // Plater::can_undo() also requires the plater panel to be the shown tab,
    // which is not the case when the application is started this way. Bring the
    // 3D editor up, then undo in the same tick the availability is observed:
    // waiting a tick lets visibility flip again before undo() re-checks it.
    void step_wait_and_undo()
    {
        if (m_undo_ticks == 0 && !m_plater.IsShown()) {
            if (MainFrame* frame = wxGetApp().mainframe) {
                frame->Show();
                frame->select_tab(static_cast<size_t>(MainFrame::tp3DEditor));
            }
        }

        const WorkspaceSnapshot current = m_workspace.snapshot();
        if (!current.can_undo && ++m_undo_ticks <= kUndoAvailableTimeoutTicks)
            return;

        log("STEP undo can_undo=" + std::to_string(current.can_undo) + " waited_ticks=" + std::to_string(m_undo_ticks) +
            " plater_shown=" + std::to_string(m_plater.IsShown()) + " view3d_shown=" + std::to_string(m_plater.is_view3D_shown()));

        m_before_undo = current;
        m_undo_result = m_workspace.undo();
        check("undo_command_succeeded", m_undo_result.succeeded(), m_undo_result.succeeded() ? "" : "message=" + m_undo_result.message);
        ++m_step;
    }

    void step_assert_reverted()
    {
        const WorkspaceSnapshot current = m_workspace.snapshot();
        log("SNAPSHOT source=after_undo " + snapshot_text(current));

        const WorkspaceObject* object = find_object(current, m_target_id);
        if (object == nullptr) {
            check("snapshot_name_reverted", false,
                  "object id=" + std::to_string(m_target_id.value()) + " absent from snapshot after undo");
        } else {
            check("snapshot_name_reverted", object->name == m_original_name,
                  "expected=" + m_original_name + " actual=" + object->name);
        }
        // A command result must describe what actually happened: reporting
        // success means the workspace changed, reporting failure means it did
        // not. Either mismatch is a defect in the adapter regardless of whether
        // the underlying undo was able to do anything.
        const bool state_changed = objects_text(m_before_undo) != objects_text(current);
        const bool consistent    = m_undo_result.succeeded() == state_changed;
        check("undo_result_matches_state", consistent,
              std::string("reported=") + (m_undo_result.succeeded() ? "success" : "failure") +
                  " state_changed=" + (state_changed ? "yes" : "no"));

        check("scenario_completed", true, "");
        ++m_step;
        finish();
    }

    // Only reached when the first plate is empty but some later plate is not.
    const WorkspaceObject* find_first(const WorkspaceSnapshot& snapshot) const
    {
        for (const WorkspacePlate& plate : snapshot.plates)
            if (!plate.objects.empty())
                return &plate.objects.front();
        return nullptr;
    }

    Plater& m_plater;
    IWorkspace& m_workspace;
    wxTimer m_timer;
    WorkspaceSubscription m_subscription;
    std::ofstream m_log_file;
    std::uint64_t m_sequence{0};
    std::vector<Check> m_checks;
    int m_step{0};
    int m_load_ticks{0};
    int m_undo_ticks{0};
    WorkspaceSnapshot m_before_undo;
    CommandResult m_undo_result;
    ObjectId m_target_id;
    int m_target_index{-1};
    std::string m_original_name;
};

} // namespace

bool workspace_selftest_requested() { return env_is_one("JUSPRIN_WORKSPACE_SELFTEST"); }

void run_workspace_selftest(Plater& plater, IWorkspace& workspace)
{
    // Owned by the event loop for the rest of the (short) process lifetime.
    auto* runner = new SelfTestRunner(plater, workspace);
    runner->start();
}

} // namespace Slic3r::GUI::JusPrin::Workspace
