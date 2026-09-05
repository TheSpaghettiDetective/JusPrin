#pragma once

#include "Workspace.hpp"
#include "FakeSettings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

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
        result.setup.process_preset = m_settings_available ? "Fixture process" : "";
        result.setup.process_preset_dirty = m_settings.values != m_settings.preset_values;
        for (auto& plate : result.plates)
            if (!plate.sliced) plate.slice_result_id = 0;
        return result;
    }

    SettingsSearchResult search_settings(const SettingsQuery& query) const override
    {
        if (!m_settings_available) {
            SettingsSearchResult result;
            result.error = SettingIssue{"", "workspace_unavailable", "No active FFF process preset."};
            return result;
        }
        return search_setting_definitions(m_settings.definitions, query);
    }

    SettingsReadResult read_settings(const std::vector<std::string>& keys) const override
    {
        if (!m_settings_available) {
            SettingsReadResult result;
            result.error = SettingIssue{"", "workspace_unavailable", "No active FFF process preset."};
            return result;
        }
        return m_settings.read(keys);
    }

    SettingsPreview preview_settings(const SettingsPatch& patch) const override
    {
        if (!m_settings_available) {
            SettingsPreview result;
            result.issues.push_back({"", "workspace_unavailable", "No active FFF process preset."});
            return result;
        }
        return m_settings.preview(patch);
    }

    CommandResult apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed,
                                 SettingsPreview& applied) override
    {
        if (!m_settings_available)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "No active FFF process preset.");
        const auto result = m_settings.apply(patch, confirmed, applied);
        if (result.succeeded()) {
            for (auto& plate : m_snapshot.plates)
                plate.sliced = false;
            publish(WorkspaceChangeReasons::Settings);
        }
        return result;
    }

    // Fixture-only seams for pre-existing dependencies and an unannounced edit.
    void set_setting_for_testing(const std::string& key, std::string value, bool notify = true)
    {
        m_settings.values.at(key) = std::move(value);
        if (notify)
            publish(WorkspaceChangeReasons::Settings);
    }
    void set_settings_available_for_testing(bool available) { m_settings_available = available; }

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

    std::string auxiliary_data_dir() const override
    {
        if (m_auxiliary_dir.empty())
            m_auxiliary_dir = fresh_auxiliary_dir();
        std::filesystem::create_directories(m_auxiliary_dir);
        return m_auxiliary_dir;
    }

    CommandResult export_project_archive(const std::string& file_path) override
    {
        nlohmann::json archive{{"fakeWorkspaceArchive", 1}, {"snapshot", snapshot_to_json(m_snapshot)}};
        std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "The archive path is not writable");
        out << archive.dump(2);
        return out.good() ? CommandResult::success() :
                            CommandResult::failure(WorkspaceError::UnavailableOperation, "Writing the archive failed");
    }

    CommandResult restore_project_archive(const std::string& file_path) override
    {
        std::ifstream in(file_path, std::ios::binary);
        if (!in.is_open())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "The archive does not exist");
        std::stringstream buffer;
        buffer << in.rdbuf();
        const nlohmann::json archive = nlohmann::json::parse(buffer.str(), nullptr, false);
        if (archive.is_discarded() || !archive.is_object() || !archive.contains("snapshot"))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "The archive is not a fake workspace archive");
        // A restore is a project replacement: new session, fresh auxiliary
        // dir (mirroring the real adapter, whose model adopts a new backup
        // path), cleared history.
        replace_project(snapshot_from_json(archive["snapshot"]));
        return CommandResult::success();
    }

    CommandResult import_model(const std::string& file_path) override
    {
        std::ifstream in(file_path, std::ios::binary);
        if (!in.is_open())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "The model file does not exist");

        // Add one object named after the file to the active plate (or the first
        // plate). This is an additive manufacturing change, not a replacement.
        std::string name = std::filesystem::path(file_path).stem().string();
        if (name.empty())
            name = "Imported model";

        save_undo();
        const ObjectId new_id(m_session, ++m_last_object_id);
        WorkspaceObject object;
        object.id   = new_id;
        object.name = name;
        object.instances.push_back({});
        WorkspacePlate* target = nullptr;
        for (WorkspacePlate& plate : m_snapshot.plates)
            if (plate.active) {
                target = &plate;
                break;
            }
        if (target == nullptr && !m_snapshot.plates.empty())
            target = &m_snapshot.plates.front();
        if (target == nullptr) {
            WorkspacePlate plate;
            plate.id     = PlateId(m_session, ++m_last_object_id);
            plate.name   = "Plate 1";
            plate.active = true;
            m_snapshot.plates.push_back(plate);
            m_snapshot.active_plate = plate.id;
            target                  = &m_snapshot.plates.back();
        }
        target->objects.push_back(object);
        m_known_object_ids.insert(new_id);
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::History);
        return CommandResult::success(new_id);
    }

    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) override
    {
        return m_changes.subscribe(std::move(callback));
    }

    // Test/support seam for an authoritative project replacement. All IDs from
    // the previous session become stale, history starts empty, and — like the
    // real adapter — the auxiliary data dir changes with the project.
    void replace_project(WorkspaceSnapshot replacement) { replace_project_impl(std::move(replacement), /*keep_aux_dir=*/false); }

    // Test seam mirroring an in-place full reset (Delete All): the project is
    // replaced but the auxiliary data dir stays where it was.
    void reset_project_in_place() { replace_project_impl(WorkspaceSnapshot{}, /*keep_aux_dir=*/true); }

    // Test seam for the real adapter's event-before-directory-move ordering:
    // moves the auxiliary dir without publishing anything.
    void move_auxiliary_dir_for_testing() { m_auxiliary_dir = fresh_auxiliary_dir(); }

    // Test seam for the project/printer facts that Orca owns; the fake treats
    // a setup change like any other committed workspace change.
    void set_setup(WorkspaceSetup setup)
    {
        if (m_snapshot.setup == setup)
            return;
        m_snapshot.setup = std::move(setup);
        publish(WorkspaceChangeReasons::Project);
    }

    void set_plate_sliced(PlateId id, bool sliced)
    {
        for (WorkspacePlate& plate : m_snapshot.plates)
            if (plate.id == id && plate.sliced != sliced) {
                plate.sliced = sliced;
                if (sliced) ++plate.slice_result_id;
                publish(WorkspaceChangeReasons::Plates);
                return;
            }
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

    void replace_project_impl(WorkspaceSnapshot replacement, bool keep_aux_dir)
    {
        m_session = next_session();
        m_undo.clear();
        m_redo.clear();
        m_known_object_ids.clear();
        m_last_object_id = 0;
        if (!keep_aux_dir)
            m_auxiliary_dir = fresh_auxiliary_dir();
        install_snapshot(std::move(replacement));
        remember_ids();
        publish(WorkspaceChangeReasons::Project | WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Plates |
                WorkspaceChangeReasons::Selection | WorkspaceChangeReasons::History);
    }

    static std::string fresh_auxiliary_dir()
    {
        static std::uint64_t next_dir = 0;
        for (int attempt = 0; attempt < 10; ++attempt) {
            const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            const std::filesystem::path dir =
                std::filesystem::temp_directory_path() /
                ("jusprin-fake-workspace-" + std::to_string(nonce) + "-" + std::to_string(++next_dir));
            std::error_code error;
            if (std::filesystem::create_directory(dir, error))
                return dir.string();
            if (error && error != std::errc::file_exists)
                throw std::runtime_error("Unable to create a fake-workspace directory: " + error.message());
        }
        throw std::runtime_error("Unable to allocate a unique fake-workspace directory");
    }

    static nlohmann::json snapshot_to_json(const WorkspaceSnapshot& snapshot)
    {
        nlohmann::json plates = nlohmann::json::array();
        for (const WorkspacePlate& plate : snapshot.plates) {
            nlohmann::json objects = nlohmann::json::array();
            for (const WorkspaceObject& object : plate.objects) {
                nlohmann::json instances = nlohmann::json::array();
                for (const ObjectTransform& transform : object.instances)
                    instances.push_back(nlohmann::json{{"position", transform.position},
                                                       {"rotation", transform.rotation},
                                                       {"scale", transform.scale}});
                objects.push_back(nlohmann::json{{"id", object.id.value()}, {"name", object.name},
                                                 {"instances", std::move(instances)}});
            }
            plates.push_back(nlohmann::json{{"id", plate.id.value()}, {"name", plate.name}, {"active", plate.active},
                                            {"sliced", plate.sliced}, {"objects", std::move(objects)}});
        }
        return nlohmann::json{{"setup", nlohmann::json{{"projectName", snapshot.setup.project_name},
                                                       {"printerPreset", snapshot.setup.printer_preset},
                                                       {"filamentPreset", snapshot.setup.filament_preset}}},
                              {"plates", std::move(plates)}};
    }

    static WorkspaceSnapshot snapshot_from_json(const nlohmann::json& value)
    {
        WorkspaceSnapshot snapshot;
        snapshot.setup.project_name   = value["setup"].value("projectName", "");
        snapshot.setup.printer_preset = value["setup"].value("printerPreset", "");
        snapshot.setup.filament_preset = value["setup"].value("filamentPreset", "");
        for (const nlohmann::json& plate_json : value.value("plates", nlohmann::json::array())) {
            WorkspacePlate plate;
            plate.id     = PlateId(ProjectSessionId(1), plate_json.value("id", std::uint64_t(0)));
            plate.name   = plate_json.value("name", "");
            plate.active = plate_json.value("active", false);
            plate.sliced = plate_json.value("sliced", false);
            for (const nlohmann::json& object_json : plate_json.value("objects", nlohmann::json::array())) {
                WorkspaceObject object;
                object.id   = ObjectId(ProjectSessionId(1), object_json.value("id", std::uint64_t(0)));
                object.name = object_json.value("name", "");
                for (const nlohmann::json& instance_json : object_json.value("instances", nlohmann::json::array())) {
                    ObjectTransform transform;
                    if (instance_json.contains("position"))
                        transform.position = instance_json["position"].get<std::array<double, 3>>();
                    if (instance_json.contains("rotation"))
                        transform.rotation = instance_json["rotation"].get<std::array<double, 3>>();
                    if (instance_json.contains("scale"))
                        transform.scale = instance_json["scale"].get<std::array<double, 3>>();
                    object.instances.push_back(transform);
                }
                plate.objects.push_back(std::move(object));
            }
            if (plate.active)
                snapshot.active_plate = plate.id;
            snapshot.plates.push_back(std::move(plate));
        }
        return snapshot;
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
    mutable std::string           m_auxiliary_dir;
    WorkspaceSnapshot             m_snapshot;
    std::vector<WorkspaceSnapshot> m_undo;
    std::vector<WorkspaceSnapshot> m_redo;
    std::set<ObjectId>             m_known_object_ids;
    std::uint64_t                  m_last_object_id{0};
    WorkspaceChangeHub             m_changes;
    FakeSettings                   m_settings;
    bool                           m_settings_available{true};
};

} // namespace Slic3r::GUI::JusPrin::Workspace
