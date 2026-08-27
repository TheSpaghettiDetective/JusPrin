#pragma once

#include "Workspace.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace Slic3r::GUI::JusPrin::Workspace {

class FakeWorkspace final : public IWorkspace
{
public:
    explicit FakeWorkspace(WorkspaceSnapshot initial = {}) : m_session(next_session())
    {
        install_snapshot(std::move(initial));
        remember_ids();
    }

    WorkspaceSnapshot snapshot() const override
    {
        WorkspaceSnapshot result = m_snapshot;
        result.session           = m_session;
        result.revision          = m_changes.revision();
        result.can_undo          = !m_undo.empty();
        result.can_redo          = !m_redo.empty();
        return result;
    }

    CommandResult select_object(ObjectId id) override
    {
        if (CommandResult validation = validate(id); !validation.succeeded())
            return validation;
        if (m_snapshot.selection_status == SelectionStatus::Objects && m_snapshot.selected_objects == std::vector<ObjectId>{id})
            return CommandResult::failure(WorkspaceError::NoChange, "Object is already selected");

        m_snapshot.selection_status = SelectionStatus::Objects;
        m_snapshot.selected_objects = {id};
        publish(WorkspaceChangeReasons::Selection);
        return CommandResult::success();
    }

    CommandResult rename_object(ObjectId id, const std::string& name) override
    {
        if (name.empty() || std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Object name cannot be empty");
        if (CommandResult validation = validate(id); !validation.succeeded())
            return validation;

        const WorkspaceObject* object = find_object(id);
        if (object != nullptr && object->name == name)
            return CommandResult::failure(WorkspaceError::NoChange, "Object already has that name");

        save_undo();
        for_each_object(id, [&name](WorkspaceObject& item) { item.name = name; });
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::History);
        return CommandResult::success();
    }

    CommandResult duplicate_object(ObjectId id) override
    {
        if (CommandResult validation = validate(id); !validation.succeeded())
            return validation;

        const WorkspaceObject* source = find_object(id);
        if (source == nullptr)
            return CommandResult::failure(WorkspaceError::MissingObject, "Object is unavailable");

        save_undo();
        const ObjectId new_id(m_session, ++m_last_object_id);
        WorkspaceObject copy = *source;
        copy.id              = new_id;
        for (WorkspacePlate& plate : m_snapshot.plates) {
            const bool contains_source = std::any_of(plate.objects.begin(), plate.objects.end(),
                                                     [id](const WorkspaceObject& item) { return item.id == id; });
            if (contains_source)
                plate.objects.emplace_back(copy);
        }
        m_known_object_ids.insert(new_id);
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::History);
        return CommandResult::success(new_id);
    }

    CommandResult remove_object(ObjectId id) override
    {
        if (CommandResult validation = validate(id); !validation.succeeded())
            return validation;

        save_undo();
        for (WorkspacePlate& plate : m_snapshot.plates) {
            plate.objects.erase(std::remove_if(plate.objects.begin(), plate.objects.end(),
                                               [id](const WorkspaceObject& item) { return item.id == id; }),
                                plate.objects.end());
        }
        m_snapshot.selected_objects.erase(std::remove(m_snapshot.selected_objects.begin(), m_snapshot.selected_objects.end(), id),
                                          m_snapshot.selected_objects.end());
        WorkspaceChangeReasons reasons = WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::History;
        if (m_snapshot.selected_objects.empty() && m_snapshot.selection_status == SelectionStatus::Objects) {
            m_snapshot.selection_status = SelectionStatus::None;
            reasons |= WorkspaceChangeReasons::Selection;
        }
        publish(reasons);
        return CommandResult::success();
    }

    CommandResult undo() override
    {
        if (m_undo.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to undo");

        const WorkspaceSnapshot before = m_snapshot;
        m_redo.emplace_back(m_snapshot);
        m_snapshot = std::move(m_undo.back());
        m_undo.pop_back();
        remember_ids();
        publish(changes_between(before, m_snapshot) | WorkspaceChangeReasons::History);
        return CommandResult::success();
    }

    CommandResult redo() override
    {
        if (m_redo.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to redo");

        const WorkspaceSnapshot before = m_snapshot;
        m_undo.emplace_back(m_snapshot);
        m_snapshot = std::move(m_redo.back());
        m_redo.pop_back();
        remember_ids();
        publish(changes_between(before, m_snapshot) | WorkspaceChangeReasons::History);
        return CommandResult::success();
    }

    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) override
    {
        return m_changes.subscribe(std::move(callback));
    }

    // Test/support seam for an authoritative project replacement. All IDs from
    // the previous session become stale and history starts empty.
    void replace_project(WorkspaceSnapshot replacement)
    {
        m_session = next_session();
        m_undo.clear();
        m_redo.clear();
        m_known_object_ids.clear();
        m_last_object_id = 0;
        install_snapshot(std::move(replacement));
        remember_ids();
        publish(WorkspaceChangeReasons::Project | WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Plates |
                WorkspaceChangeReasons::Selection | WorkspaceChangeReasons::History);
    }

    void set_unsupported_selection()
    {
        if (m_snapshot.selection_status == SelectionStatus::Unsupported)
            return;
        m_snapshot.selection_status = SelectionStatus::Unsupported;
        m_snapshot.selected_objects.clear();
        publish(WorkspaceChangeReasons::Selection);
    }

    // Produces a safe-to-queue delivery after advancing the committed revision.
    // It is used to prove that delayed delivery cannot outlive the workspace.
    WorkspaceChangeDelivery queue_change(WorkspaceChangeReasons reasons)
    {
        m_changes.merge(reasons);
        return m_changes.commit(m_session);
    }

private:
    static ProjectSessionId next_session()
    {
        static std::uint64_t next_value = 0;
        return ProjectSessionId(++next_value);
    }

    void install_snapshot(WorkspaceSnapshot snapshot)
    {
        m_snapshot = std::move(snapshot);
        m_snapshot.session = m_session;
        for (WorkspacePlate& plate : m_snapshot.plates) {
            plate.id = PlateId(m_session, plate.id.value());
            for (WorkspaceObject& object : plate.objects)
                object.id = ObjectId(m_session, object.id.value());
        }
        if (m_snapshot.active_plate)
            m_snapshot.active_plate = PlateId(m_session, m_snapshot.active_plate->value());
        for (ObjectId& id : m_snapshot.selected_objects)
            id = ObjectId(m_session, id.value());
    }

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
        if (id.session() != m_session)
            return CommandResult::failure(WorkspaceError::StaleId, "Object ID belongs to an earlier project session");
        if (find_object(id) != nullptr)
            return CommandResult::success();
        return CommandResult::failure(m_known_object_ids.count(id) > 0 ? WorkspaceError::StaleId : WorkspaceError::MissingObject,
                                      "Object does not exist in the current project session");
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
                m_known_object_ids.insert(object.id);
                m_last_object_id = std::max(m_last_object_id, object.id.value());
            }
    }

    static WorkspaceChangeReasons changes_between(const WorkspaceSnapshot& before, const WorkspaceSnapshot& after)
    {
        WorkspaceChangeReasons reasons = WorkspaceChangeReasons::None;

        using Content = std::map<ObjectId, std::string>;
        using Transforms = std::map<ObjectId, std::vector<ObjectTransform>>;
        auto content_of = [](const WorkspaceSnapshot& value) {
            Content result;
            for (const WorkspacePlate& plate : value.plates)
                for (const WorkspaceObject& object : plate.objects)
                    result[object.id] = object.name;
            return result;
        };
        auto transforms_of = [](const WorkspaceSnapshot& value) {
            Transforms result;
            for (const WorkspacePlate& plate : value.plates)
                for (const WorkspaceObject& object : plate.objects)
                    result[object.id] = object.instances;
            return result;
        };
        auto plates_of = [](const WorkspaceSnapshot& value) {
            std::vector<std::pair<PlateId, std::string>> result;
            for (const WorkspacePlate& plate : value.plates)
                result.emplace_back(plate.id, plate.name);
            return result;
        };

        if (content_of(before) != content_of(after))
            reasons |= WorkspaceChangeReasons::Contents;
        if (transforms_of(before) != transforms_of(after))
            reasons |= WorkspaceChangeReasons::Transform;
        if (before.selection_status != after.selection_status || before.selected_objects != after.selected_objects)
            reasons |= WorkspaceChangeReasons::Selection;
        if (before.active_plate != after.active_plate || plates_of(before) != plates_of(after))
            reasons |= WorkspaceChangeReasons::Plates;
        return reasons;
    }

    void publish(WorkspaceChangeReasons reasons)
    {
        m_changes.merge(reasons);
        WorkspaceChangeDelivery delivery = m_changes.commit(m_session);
        delivery.deliver();
    }

    ProjectSessionId              m_session;
    WorkspaceSnapshot             m_snapshot;
    std::vector<WorkspaceSnapshot> m_undo;
    std::vector<WorkspaceSnapshot> m_redo;
    std::set<ObjectId>             m_known_object_ids;
    std::uint64_t                  m_last_object_id{0};
    WorkspaceChangeHub             m_changes;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
