#pragma once

#include "Workspace.hpp"

#include <memory>
#include <set>

namespace Slic3r::GUI {
class Plater;
class SimpleEvent;
} // namespace Slic3r::GUI

namespace Slic3r::GUI::JusPrin::Workspace {

class OrcaWorkspaceAdapter final : public IWorkspace
{
public:
    explicit OrcaWorkspaceAdapter(Plater& plater);
    ~OrcaWorkspaceAdapter() override;

    OrcaWorkspaceAdapter(const OrcaWorkspaceAdapter&)            = delete;
    OrcaWorkspaceAdapter& operator=(const OrcaWorkspaceAdapter&) = delete;

    WorkspaceSnapshot snapshot() const override;
    CommandResult select_object(ObjectId id) override;
    CommandResult rename_object(ObjectId id, const std::string& name) override;
    CommandResult duplicate_object(ObjectId id) override;
    CommandResult remove_object(ObjectId id) override;
    CommandResult undo() override;
    CommandResult redo() override;
    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) override;

private:
    struct ResolvedObject
    {
        std::size_t index;
    };

    std::optional<ResolvedObject> resolve(ObjectId id) const;
    CommandResult missing_result(ObjectId id) const;
    void schedule_change(WorkspaceChangeReasons reasons);
    WorkspaceChangeReasons changes_after_history(const WorkspaceSnapshot& before, const WorkspaceSnapshot& after) const;
    void on_selection(SimpleEvent& event);
    void on_transform(SimpleEvent& event);
    void on_plate(SimpleEvent& event);

    Plater& m_plater;
    WorkspaceChangeHub m_changes;
    mutable std::set<ObjectId> m_known_object_ids;
    bool m_dispatch_scheduled{false};
    std::shared_ptr<int> m_lifetime{std::make_shared<int>(0)};
};

} // namespace Slic3r::GUI::JusPrin::Workspace
