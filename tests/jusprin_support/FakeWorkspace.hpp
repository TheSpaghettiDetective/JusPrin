#pragma once

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include "FakeSettings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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
        result.setup.process_preset = m_settings_available ? m_process_preset : "";
        result.setup.process_preset_dirty = m_settings.values != m_settings.preset_values;
        result.currency = m_currency;
        if (m_settings_available)
            for (const auto& [key, value] : m_settings.values) {
                const auto preset = m_settings.preset_values.find(key);
                if (preset == m_settings.preset_values.end() || preset->second == value) continue;
                const auto definition = std::find_if(m_settings.definitions.begin(), m_settings.definitions.end(),
                                                     [&](const auto& candidate) { return candidate.key == key; });
                result.preset_deltas.push_back(
                    {key, definition == m_settings.definitions.end() ? key : definition->label, preset->second, value});
            }
        for (auto& plate : result.plates)
            if (!plate.sliced) {
                // A fixture may describe an estimate the plate can no longer
                // defend -- recomputing or stale -- but never a current one.
                if (plate.estimate_status == EstimateStatus::Current) plate.estimate.reset();
                continue;
            } else {
                plate.estimate_status = EstimateStatus::Current;
                plate.invalidated_by.clear();
            }
        return result;
    }

    SettingsSearchResult search_settings(const SettingsQuery& query) const override
    {
        if (!m_settings_available) {
            SettingsSearchResult result;
            result.error = SettingIssue{"", "workspace_unavailable", "No active FFF process preset."};
            return result;
        }
        std::vector<std::string> changed;
        for (const auto& [key, value] : m_settings.values)
            if (m_settings.preset_values.at(key) != value) changed.push_back(key);
        return search_setting_definitions(m_settings.definitions, query, changed);
    }

    SettingsReadResult read_settings(const std::vector<std::string>& keys, const SettingsTarget& target = {}) const override
    {
        if (!m_settings_available) {
            SettingsReadResult result;
            result.error = SettingIssue{"", "workspace_unavailable", "No active FFF process preset."};
            return result;
        }
        return m_settings.read(keys, target);
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
        if (patch.target.object && m_settings.preview(patch).valid)
            save_undo("Change object settings");
        const auto result = m_settings.apply(patch, confirmed, applied);
        if (result.succeeded()) {
            for (auto& plate : m_snapshot.plates)
                plate.sliced = false;
            for (const SettingChange& change : applied.changes)
                publish_setting_edit(change.key, change.before, change.after);
            publish(WorkspaceChangeReasons::Settings);
        }
        return result;
    }

    // Fixture-only seams for pre-existing dependencies and an unannounced edit.
    void set_setting_for_testing(const std::string& key, std::string value, bool notify = true)
    {
        const std::string before = m_settings.values.at(key);
        m_settings.values.at(key) = std::move(value);
        if (notify) {
            publish_setting_edit(key, before, m_settings.values.at(key));
            publish(WorkspaceChangeReasons::Settings);
        }
    }
    void set_settings_available_for_testing(bool available) { m_settings_available = available; }
    // Switching presets moves the baseline the deltas are measured from, which
    // a fixture can only say outright.
    void set_process_preset_for_testing(std::string name)
    {
        m_process_preset = std::move(name);
        publish_edit({EditKind::Preset, EditActor::Person, m_process_preset});
        publish(WorkspaceChangeReasons::Settings);
    }

    // A real undo step that changes nothing the fake projects, named as
    // OrcaSlicer names its steps (possibly empty), e.g. one paint stroke.
    void record_step_for_testing(std::string name)
    {
        save_undo(std::move(name));
        publish(WorkspaceChangeReasons::History);
    }
    // The real adapter reads this from the OS; a fixture states it outright so
    // a test can describe a machine with no regional currency at all.
    void set_currency_for_testing(std::string code) { m_currency = std::move(code); }

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

        save_undo("Rename Object");
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

        save_undo("Duplicate");
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

        save_undo("Delete Object");
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
        m_redo_names.push_back(std::move(m_undo_names.back()));
        m_undo_names.pop_back();
        m_redo_ids.push_back(m_undo_ids.back());
        m_undo_ids.pop_back();
        remember_ids();
        publish_edit({EditKind::Undo, EditActor::Person, m_redo_names.back()});
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
        m_undo_names.push_back(std::move(m_redo_names.back()));
        m_redo_names.pop_back();
        m_undo_ids.push_back(m_redo_ids.back());
        m_redo_ids.pop_back();
        remember_ids();
        publish_edit({EditKind::Redo, EditActor::Person, m_undo_names.back()});
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

    CommandResult import_objects(const ImportRequest& request, std::vector<LoadDecision>& decisions,
                                 std::vector<ObjectId>& added) override
    {
        const std::string& file_path = request.path;
        std::ifstream in(std::filesystem::u8path(file_path), std::ios::binary);
        if (!in.is_open())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "The model file does not exist");

        // Add one object named after the file to the active plate (or the first
        // plate). This is an additive manufacturing change, not a replacement.
        std::string name = std::filesystem::path(file_path).stem().string();
        if (name.empty())
            name = "Imported model";

        save_undo("Import model");
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
        last_import = request;
        decisions   = m_open_decisions;
        added.push_back(new_id);
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::History);
        return CommandResult::success(new_id);
    }
    ImportRequest last_import;

    // Objects and copies go; parts and plates are recorded only, since the
    // fixture keeps neither.
    CommandResult delete_items(const std::vector<DeleteItem>& items) override
    {
        for (const DeleteItem& item : items)
            if (item.kind != DeleteItem::Kind::Plate)
                if (CommandResult validation = validate(item.object); !validation.succeeded())
                    return validation;
        save_undo("Delete items");
        for (const DeleteItem& item : items) {
            if (item.kind == DeleteItem::Kind::Object)
                for (WorkspacePlate& plate : m_snapshot.plates)
                    plate.objects.erase(std::remove_if(plate.objects.begin(), plate.objects.end(),
                                                       [&](const WorkspaceObject& o) { return o.id == item.object; }),
                                        plate.objects.end());
            if (item.kind == DeleteItem::Kind::Instance)
                for_each_object(item.object, [&](WorkspaceObject& object) {
                    if (item.instance < object.instances.size())
                        object.instances.erase(object.instances.begin() + item.instance);
                });
        }
        last_delete = items;
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::History);
        return CommandResult::success();
    }
    std::vector<DeleteItem> last_delete;

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

    // Publishing here is part of the contract, not a fixture convenience: a
    // Starting a run is a Plates change: whether a slice is in flight is part
    // of what a plate can currently say about itself.
    CommandResult start_slice(std::optional<PlateId> plate, bool preempt) override
    {
        if (m_snapshot.slicing.running && !preempt)
            return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                          "A slice is already running. Wait for it, or ask again with preempt.");
        if (plate) {
            if (plate->session() != m_snapshot.session)
                return CommandResult::failure(WorkspaceError::StaleId, "That plate belongs to a project that is no longer open");
            const auto found = std::find_if(m_snapshot.plates.begin(), m_snapshot.plates.end(),
                                            [&plate](const WorkspacePlate& candidate) { return candidate.id == *plate; });
            if (found == m_snapshot.plates.end())
                return CommandResult::failure(WorkspaceError::InvalidId, "No such plate");
        }
        m_snapshot.slicing.running = true;
        m_snapshot.slicing.plate   = plate ? plate : (m_snapshot.plates.empty() ? std::optional<PlateId>() :
                                                                                 m_snapshot.plates.front().id);
        m_snapshot.slicing.percent = 0;
        ++slice_starts;
        publish(WorkspaceChangeReasons::Plates);
        return CommandResult::success();
    }

    PresetListResult list_presets(const PresetQuery& query) const override
    {
        PresetListResult result;
        const auto found = m_presets.find(query.kind);
        if (found == m_presets.end())
            return result;
        std::size_t offset = query.cursor.empty() ? 0 : std::stoull(query.cursor), skipped = 0;
        for (const PresetEntry& preset : found->second) {
            if (query.compatible_only && !preset.compatible)
                continue;
            if (!query.text.empty() && preset.name.find(query.text) == std::string::npos &&
                preset.label.find(query.text) == std::string::npos)
                continue;
            ++result.total;
            if (skipped++ < offset)
                continue;
            if (result.items.size() >= query.limit) {
                result.truncated = true;
                continue;
            }
            result.items.push_back(preset);
        }
        if (result.truncated)
            result.next_cursor = std::to_string(offset + result.items.size());
        return result;
    }

    void set_presets_for_testing(PresetKind kind, std::vector<PresetEntry> presets)
    {
        m_presets[kind] = std::move(presets);
    }

    std::vector<PrinterDevice> printers() const override { return m_printers; }

    ConfiguredPrinter configured_printer() const override { return m_configured_printer; }

    // Placement moves the fixture's transform the way the request says and
    // records the request; auto-orient leaves a running job to finish.
    CommandResult place_object(ObjectId id, const PlacementRequest& request, const std::string& job_handle,
                               PlacementResult& result) override
    {
        if (CommandResult validation = validate(id); !validation.succeeded())
            return validation;
        if (!request.face_down.empty() && m_changes.revision() != m_analysis_revision)
            return CommandResult::failure(WorkspaceError::FeatureExpired, "The project changed since the features were read");
        last_placement = request;
        save_undo("Place object");
        ObjectTransform placed;
        for_each_object(id, [&](WorkspaceObject& object) {
            if (request.instance >= object.instances.size()) return;
            auto& transform = object.instances[request.instance];
            if (request.rotate)
                for (int axis = 0; axis < 3; ++axis) transform.rotation[axis] += (*request.rotate)[axis];
            if (request.position) {
                transform.position[0] = (*request.position)[0];
                transform.position[1] = (*request.position)[1];
            }
            if (request.scale)
                for (int axis = 0; axis < 3; ++axis) transform.scale[axis] *= (*request.scale)[axis];
            placed = transform;
        });
        if (request.auto_orient) {
            m_snapshot.jobs.push_back({job_handle, "orient", "running", {}});
            result.orienting = true;
        }
        result.object    = id;
        result.transform = placed;
        result.size      = {20, 20, 20};
        publish(WorkspaceChangeReasons::Transform | WorkspaceChangeReasons::History);
        return CommandResult::success();
    }
    void finish_job_for_testing(const std::string& handle, std::string state, std::vector<ObjectId> not_placed = {})
    {
        for (WorkspaceJob& job : m_snapshot.jobs)
            if (job.handle == handle) {
                job.state      = std::move(state);
                job.not_placed = std::move(not_placed);
            }
        publish(WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::Transform);
    }
    PlacementRequest last_placement;

    // Quantity is the fixture's instance count; a plate row without an id
    // adds a plate; arrange leaves a running job.
    CommandResult lay_out(const LayoutRequest& request, const std::string& job_handle, LayoutResult& result) override
    {
        for (const LayoutObject& row : request.objects)
            if (CommandResult validation = validate(row.id); !validation.succeeded())
                return validation;
        last_layout = request;
        save_undo("Lay out plates");
        for (const LayoutObject& row : request.objects)
            for_each_object(row.id, [&](WorkspaceObject& object) {
                if (row.quantity) object.instances.resize(*row.quantity);
                if (row.name) object.name = *row.name;
            });
        for (const LayoutPlate& row : request.plates)
            if (!row.id) {
                WorkspacePlate plate;
                plate.id   = PlateId(m_session, 1000 + m_snapshot.plates.size());
                plate.name = row.name.value_or("Plate " + std::to_string(m_snapshot.plates.size() + 1));
                m_snapshot.plates.push_back(plate);
                result.added_plates.push_back(plate.id);
            }
        if (request.arrange) {
            m_snapshot.jobs.push_back({job_handle, "arrange", "running", {}});
            result.arranging = true;
        }
        publish(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::History);
        return CommandResult::success();
    }
    LayoutRequest last_layout;

    // Analysis answers are scripted per object; a handle is good for the
    // revision the fixture was given and expires after it.
    CommandResult analyze_object(ObjectId id, const AnalysisRequest& request, ObjectAnalysis& result) const override
    {
        if (CommandResult validation = validate(id); !validation.succeeded())
            return validation;
        const auto found = m_analyses.find(id.value());
        if (found == m_analyses.end())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "No analysis scripted for this object");
        if (request.mesh) result.mesh = found->second.mesh;
        if (request.features) result.features = found->second.features;
        if (request.fit) result.fit = found->second.fit;
        if (request.orientations) {
            last_candidates     = request.candidates;
            result.orientations = found->second.orientations;
        }
        if (request.measure) {
            if (m_changes.revision() != m_analysis_revision)
                return CommandResult::failure(WorkspaceError::FeatureExpired, "The project changed since the features were read");
            result.measurement = found->second.measurement;
        }
        return CommandResult::success();
    }
    void set_analysis_for_testing(ObjectId id, ObjectAnalysis analysis)
    {
        m_analyses[id.value()] = std::move(analysis);
        m_analysis_revision    = m_changes.revision();
    }
    std::map<std::uint64_t, ObjectAnalysis> m_analyses;
    std::uint64_t m_analysis_revision{0};
    mutable std::vector<OrientationCandidate> last_candidates;

    std::vector<ObjectDetails> object_details() const override
    {
        std::vector<ObjectDetails> result;
        for (const WorkspacePlate& plate : m_snapshot.plates)
            for (const WorkspaceObject& object : plate.objects) {
                ObjectDetails row;
                row.id        = object.id;
                row.name      = object.name;
                row.plates    = {plate.id};
                row.instances = object.instances.size();
                row.parts     = 1;
                result.push_back(row);
            }
        return result;
    }
    std::string current_process_preset() const override { return m_process_preset; }

    // A preset is known when set_presets_for_testing listed it, and
    // compatible when its entry says so. A printer switch replaces an
    // incompatible process, as Orca does.
    PrinterSetupPreview preview_printer_setup(const PrinterSetupRequest& request) const override
    {
        PrinterSetupPreview result;
        const auto find = [this](PresetKind kind, const std::string& name) -> const PresetEntry* {
            const auto found = m_presets.find(kind);
            if (found == m_presets.end()) return nullptr;
            for (const PresetEntry& entry : found->second)
                if (entry.name == name) return &entry;
            return nullptr;
        };
        result.resulting      = m_configured_printer;
        result.process_preset = m_process_preset;
        const bool printer_changes = request.printer_preset && *request.printer_preset != m_configured_printer.preset;
        if (request.printer_preset) {
            if (find(PresetKind::Printer, *request.printer_preset) == nullptr)
                result.issues.push_back({"unknown_preset", "No printer preset " + *request.printer_preset});
            result.resulting.preset = *request.printer_preset;
        }
        if (request.process_preset) {
            const PresetEntry* entry = find(PresetKind::Process, *request.process_preset);
            if (entry == nullptr) result.issues.push_back({"unknown_preset", "No process preset " + *request.process_preset});
            else if (!entry->compatible) result.issues.push_back({"incompatible_preset", "Not made for this printer"});
            else result.process_preset = entry->name;
        } else if (printer_changes && m_process_incompatible_after_printer) {
            result.substitutions.push_back({"process", m_process_preset, "not made for the new printer"});
        }
        if (request.filament_presets)
            for (std::size_t slot = 0; slot < request.filament_presets->size(); ++slot) {
                const std::string& name = (*request.filament_presets)[slot];
                if (find(PresetKind::Filament, name) == nullptr) {
                    result.issues.push_back({"unknown_preset", "No filament preset " + name});
                    continue;
                }
                if (slot >= result.resulting.filaments.size()) {
                    result.issues.push_back({"invalid_argument", "Too many filament slots"});
                    break;
                }
                result.resulting.filaments[slot] = {name, m_filament_materials.count(name) ? m_filament_materials.at(name) : ""};
            }
        if (request.plate_type) {
            if (std::find(m_plates.begin(), m_plates.end(), *request.plate_type) == m_plates.end())
                result.issues.push_back({"unsupported_plate", "Not a plate this printer offers"});
            else
                result.resulting.plate_type = *request.plate_type;
        }
        const bool process_changes = result.process_preset != m_process_preset || !result.substitutions.empty();
        if (m_process_dirty && (printer_changes || process_changes))
            result.unsaved_edits.push_back({"process", m_process_preset, 2});
        result.valid = result.issues.empty();
        return result;
    }

    CommandResult apply_printer_setup(const PrinterSetupRequest& request, PrinterSetupPreview& applied) override
    {
        applied = preview_printer_setup(request);
        if (!applied.valid)
            return CommandResult::failure(WorkspaceError::InvalidArgument, applied.issues.front().message);
        if (!applied.unsaved_edits.empty() && !request.discard_unsaved_edits)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Unsaved preset edits would be lost");
        ++setup_applies;
        m_process_dirty = false;
        m_configured_printer = applied.resulting;
        m_process_preset     = applied.substitutions.empty() ? applied.process_preset : "substitute process";
        m_snapshot.setup.printer_preset = m_configured_printer.preset;
        publish(WorkspaceChangeReasons::Settings);
        return CommandResult::success();
    }

    void set_setup_for_testing(std::string process, std::vector<std::string> plates, std::map<std::string, std::string> materials)
    {
        m_process_preset     = std::move(process);
        m_plates             = std::move(plates);
        m_filament_materials = std::move(materials);
    }
    std::vector<std::string> m_plates;
    std::map<std::string, std::string> m_filament_materials;
    bool m_process_dirty{false};
    bool m_process_incompatible_after_printer{false};
    std::uint32_t setup_applies{0};
    void set_configured_printer_for_testing(ConfiguredPrinter printer) { m_configured_printer = std::move(printer); }
    ConfiguredPrinter m_configured_printer;

    WorkspaceHistory history() const override
    {
        WorkspaceHistory result;
        result.restorable = m_history_restorable;
        for (std::size_t index = 0; index < m_undo_ids.size(); ++index)
            result.steps.push_back({m_undo_ids[index], m_undo_names[index], true});
        for (std::size_t index = m_redo_ids.size(); index-- > 0;)
            result.steps.push_back({m_redo_ids[index], m_redo_names[index], false});
        if (result.steps.size() > kHistoryLimit) {
            result.steps.erase(result.steps.begin(), result.steps.end() - kHistoryLimit);
            result.truncated = true;
        }
        return result;
    }

    CommandResult restore_history(std::uint64_t step, HistoryPoint point) override
    {
        if (!m_history_restorable)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Another tool owns the history");
        const WorkspaceHistory all = history();
        const auto found = std::find_if(all.steps.begin(), all.steps.end(),
                                        [step](const HistoryStep& candidate) { return candidate.id == step; });
        if (found == all.steps.end())
            return CommandResult::failure(WorkspaceError::StaleId, "That step is no longer in the history");
        const std::size_t applied = static_cast<std::size_t>(found - all.steps.begin()) + (point == HistoryPoint::After ? 1 : 0);
        if (applied == m_undo.size())
            return CommandResult::failure(WorkspaceError::NoChange, "The project is already there");
        while (m_undo.size() > applied) undo();
        while (m_undo.size() < applied) redo();
        return CommandResult::success();
    }

    void set_history_restorable_for_testing(bool restorable) { m_history_restorable = restorable; }
    bool m_history_restorable{true};
    std::vector<std::uint64_t> m_undo_ids, m_redo_ids;
    std::uint64_t m_last_step_id{0};

    // A save writes a real file, so a test can prove nothing was written
    // before approval, and marks the project clean at that path.
    CommandResult save_project(const std::string& file_path) override
    {
        const std::filesystem::path target = std::filesystem::u8path(file_path);
        if (!target.is_absolute() || target.extension() != ".3mf")
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Save to an absolute path ending in .3mf");
        if (!std::filesystem::is_directory(target.parent_path()))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "The folder to save into does not exist");
        std::ofstream(target, std::ios::binary | std::ios::trunc) << "fake project";
        m_snapshot.setup.project_path  = file_path;
        m_snapshot.setup.project_dirty = false;
        return CommandResult::success();
    }

    // A project file becomes a fresh session holding one object named after
    // it; what Orca would have asked is whatever the test scripted.
    CommandResult open_project(const ProjectOpenRequest& request, std::vector<LoadDecision>& decisions) override
    {
        const std::filesystem::path target = std::filesystem::u8path(request.path);
        if (!request.new_project && (!target.is_absolute() || !std::filesystem::is_regular_file(target)))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Open an absolute path to a file that exists");
        if ((m_snapshot.setup.project_dirty || m_snapshot.setup.presets_dirty) && !request.discard_unsaved)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "The open project has unsaved changes");
        ++opens;
        last_open = request;
        WorkspaceSnapshot next;
        next.setup.project_name = request.new_project ? "Untitled" : target.stem().u8string();
        if (target.extension() == ".3mf") next.setup.project_path = request.path;
        WorkspacePlate plate;
        plate.id     = PlateId(ProjectSessionId(m_session.value() + 1), 1);
        plate.name   = "Plate 1";
        plate.active = true;
        if (!request.new_project) {
            WorkspaceObject object;
            object.id   = ObjectId(ProjectSessionId(m_session.value() + 1), 1);
            object.name = next.setup.project_name;
            object.instances.push_back({});
            plate.objects.push_back(object);
        }
        next.plates       = {plate};
        next.active_plate = plate.id;
        decisions         = m_open_decisions;
        replace_project(std::move(next));
        if (on_open_for_testing) on_open_for_testing();
        return CommandResult::success();
    }
    ProjectDetails project_details() const override { return m_details; }
    ProjectDetails m_details;
    std::vector<LoadDecision> m_open_decisions;
    // What the host does when a project is replaced under it.
    std::function<void()>     on_open_for_testing;
    ProjectOpenRequest        last_open;
    std::uint32_t             opens{0};

    void set_project_path_for_testing(std::string path, bool dirty)
    {
        m_snapshot.setup.project_path  = std::move(path);
        m_snapshot.setup.project_dirty = dirty;
    }
    void set_printers_for_testing(std::vector<PrinterDevice> printers) { m_printers = std::move(printers); }

    SliceReport slice_report(PlateId plate) const override
    {
        const auto found = m_reports.find(plate.value());
        if (found == m_reports.end())
            return {};
        const auto& sliced = std::find_if(m_snapshot.plates.begin(), m_snapshot.plates.end(),
                                          [&plate](const WorkspacePlate& candidate) { return candidate.id == plate; });
        // A fixture cannot describe a report for a plate that is not sliced, or
        // one whose slice is being replaced: the real adapter refuses both.
        if (sliced == m_snapshot.plates.end() || !sliced->sliced || m_snapshot.slicing.running)
            return {};
        return found->second;
    }

    void set_slice_report_for_testing(PlateId plate, SliceReport report)
    {
        report.valid      = true;
        m_reports[plate.value()] = std::move(report);
    }

    // What the owner reports while a run is in flight, and when it ends.
    void finish_slice_for_testing(bool sliced)
    {
        const auto plate            = m_snapshot.slicing.plate;
        m_snapshot.slicing          = {};
        if (plate)
            for (WorkspacePlate& candidate : m_snapshot.plates)
                if (candidate.id == *plate)
                    candidate.sliced = sliced;
        publish(WorkspaceChangeReasons::Plates);
    }

    std::uint32_t slice_starts{0};
    std::map<std::uint64_t, SliceReport> m_reports;
    std::map<PresetKind, std::vector<PresetEntry>> m_presets;
    std::vector<PrinterDevice> m_printers;

    // slice changes what consumers may say about the plate, so it has to
    // advance the revision. The Orca adapter matches this by listening to
    // EVT_SLICE_STATUS_CHANGED -- it did not, once, and the setup card kept
    // reporting "not sliced yet" after a successful slice.
    void set_plate_sliced(PlateId id, bool sliced)
    {
        for (WorkspacePlate& plate : m_snapshot.plates)
            if (plate.id == id && plate.sliced != sliced) {
                plate.sliced = sliced;
                publish(WorkspaceChangeReasons::Plates);
                return;
            }
    }

    // Fixtures describe a slice by its estimate; snapshot() still drops it if
    // the plate is not sliced, so a test cannot invent an impossible state.
    void set_plate_estimate_for_testing(PlateId id, std::optional<SliceEstimate> estimate,
                                        EstimateStatus status = EstimateStatus::Current,
                                        std::string invalidated_by = {})
    {
        for (WorkspacePlate& plate : m_snapshot.plates)
            if (plate.id == id) {
                plate.estimate        = std::move(estimate);
                plate.estimate_status = status;
                plate.invalidated_by  = std::move(invalidated_by);
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
        m_undo_names.clear();
        m_redo_names.clear();
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

    // Like OrcaSlicer's take_snapshot: the step is recorded, and reported to
    // the change log, before the action changes anything.
    void save_undo(std::string name)
    {
        m_undo.emplace_back(m_snapshot);
        m_undo_names.push_back(name);
        m_undo_ids.push_back(++m_last_step_id);
        m_redo.clear();
        m_redo_names.clear();
        m_redo_ids.clear();
        publish_edit({EditKind::Step, EditActor::Person, std::move(name)});
    }

    void publish_setting_edit(const std::string& key, const std::string& before, const std::string& after)
    {
        const auto definition = std::find_if(m_settings.definitions.begin(), m_settings.definitions.end(),
                                             [&](const auto& candidate) { return candidate.key == key; });
        publish_edit({EditKind::Setting, EditActor::Person,
                      definition == m_settings.definitions.end() ? key : definition->label, before, after,
                      m_process_preset});
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
    std::vector<std::string>       m_undo_names;
    std::vector<std::string>       m_redo_names;
    std::set<ObjectId>             m_known_object_ids;
    std::uint64_t                  m_last_object_id{0};
    WorkspaceChangeHub             m_changes;
    FakeSettings                   m_settings;
    bool                           m_settings_available{true};
    std::string                    m_process_preset{"Fixture process"};
    std::string                    m_currency{"USD"};
};

} // namespace Slic3r::GUI::JusPrin::Workspace
