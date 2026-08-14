#pragma once

#include "Workspace.hpp"

#include <algorithm>
#include <set>

namespace Slic3r::GUI::JusPrin::Workspace {

class FakeWorkspace final : public IWorkspace
{
public:
    explicit FakeWorkspace(WorkspaceSnapshot initial = {}) : m_snapshot(std::move(initial)) { remember_ids(); }

    WorkspaceSnapshot snapshot() const override
    {
        WorkspaceSnapshot result = m_snapshot;
        result.revision          = m_changes.revision();
        result.can_undo          = !m_undo.empty();
        result.can_redo          = !m_redo.empty();
        return result;
    }

    CommandResult select_object(ObjectId id) override
    {
        if (auto error = validate(id); !error.succeeded())
            return error;
        m_snapshot.selection_status = SelectionStatus::Objects;
        m_snapshot.selected_objects = {id};
        publish(WorkspaceChangeReasons::Selection);
        return CommandResult::success();
    }

    CommandResult rename_object(ObjectId id, const std::string& name) override
    {
        if (name.empty())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Object name cannot be empty");
        if (auto error = validate(id); !error.succeeded())
            return error;

        save_undo();
        for_each_object(id, [&name](WorkspaceObject& object) { object.name = name; });
        publish(WorkspaceChangeReasons::Contents);
        return CommandResult::success();
    }

    CommandResult duplicate_object(ObjectId id) override
    {
        if (auto error = validate(id); !error.succeeded())
            return error;

        const WorkspaceObject* source = find_object(id);
        if (source == nullptr)
            return CommandResult::failure(WorkspaceError::MissingObject, "Object is unavailable");

        save_undo();
        const ObjectId new_id(++m_last_id);
        WorkspaceObject copy = *source;
        copy.id              = new_id;
        for (WorkspacePlate& plate : m_snapshot.plates) {
            const bool contains_source = std::any_of(plate.objects.begin(), plate.objects.end(),
                                                     [id](const WorkspaceObject& object) { return object.id == id; });
            if (contains_source)
                plate.objects.emplace_back(copy);
        }
        m_known_ids.insert(new_id);
        publish(WorkspaceChangeReasons::Contents);
        return CommandResult::success(new_id);
    }

    CommandResult remove_object(ObjectId id) override
    {
        if (auto error = validate(id); !error.succeeded())
            return error;

        save_undo();
        for (WorkspacePlate& plate : m_snapshot.plates) {
            plate.objects.erase(std::remove_if(plate.objects.begin(), plate.objects.end(),
                                               [id](const WorkspaceObject& object) { return object.id == id; }),
                                plate.objects.end());
        }
        m_snapshot.selected_objects.erase(std::remove(m_snapshot.selected_objects.begin(), m_snapshot.selected_objects.end(), id),
                                          m_snapshot.selected_objects.end());
        if (m_snapshot.selected_objects.empty())
            m_snapshot.selection_status = SelectionStatus::None;
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Selection);
        return CommandResult::success();
    }

    CommandResult undo() override
    {
        if (m_undo.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to undo");
        m_redo.emplace_back(m_snapshot);
        m_snapshot = std::move(m_undo.back());
        m_undo.pop_back();
        remember_ids();
        publish(WorkspaceChangeReasons::History | WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Selection |
                WorkspaceChangeReasons::Transform);
        return CommandResult::success();
    }

    CommandResult redo() override
    {
        if (m_redo.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to redo");
        m_undo.emplace_back(m_snapshot);
        m_snapshot = std::move(m_redo.back());
        m_redo.pop_back();
        remember_ids();
        publish(WorkspaceChangeReasons::History | WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Selection |
                WorkspaceChangeReasons::Transform);
        return CommandResult::success();
    }

    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) override { return m_changes.subscribe(std::move(callback)); }

    void emit(WorkspaceChangeReasons reasons) { publish(reasons); }

private:
    const WorkspaceObject* find_object(ObjectId id) const
    {
        for (const WorkspacePlate& plate : m_snapshot.plates)
            for (const WorkspaceObject& object : plate.objects)
                if (object.id == id)
                    return &object;
        return nullptr;
    }

    template<class Fn> void for_each_object(ObjectId id, Fn&& fn)
    {
        for (WorkspacePlate& plate : m_snapshot.plates)
            for (WorkspaceObject& object : plate.objects)
                if (object.id == id)
                    fn(object);
    }

    CommandResult validate(ObjectId id) const
    {
        if (!id)
            return CommandResult::failure(WorkspaceError::InvalidId, "Object ID is invalid");
        if (find_object(id) != nullptr)
            return CommandResult::success();
        return CommandResult::failure(m_known_ids.count(id) > 0 ? WorkspaceError::StaleId : WorkspaceError::MissingObject,
                                      "Object does not exist in the current workspace");
    }

    void save_undo()
    {
        m_undo.emplace_back(m_snapshot);
        m_redo.clear();
    }

    void remember_ids()
    {
        for (const WorkspacePlate& plate : m_snapshot.plates)
            for (const WorkspaceObject& object : plate.objects) {
                m_known_ids.insert(object.id);
                m_last_id = std::max(m_last_id, object.id.value());
            }
    }

    void publish(WorkspaceChangeReasons reasons)
    {
        m_changes.merge(reasons);
        m_changes.flush();
    }

    WorkspaceSnapshot m_snapshot;
    std::vector<WorkspaceSnapshot> m_undo;
    std::vector<WorkspaceSnapshot> m_redo;
    std::set<ObjectId> m_known_ids;
    std::uint64_t m_last_id{0};
    WorkspaceChangeHub m_changes;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
