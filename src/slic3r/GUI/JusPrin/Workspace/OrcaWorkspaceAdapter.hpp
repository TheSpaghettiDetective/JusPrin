#pragma once

#include "Workspace.hpp"
#include "ProjectState.hpp"

#include <map>
#include <set>

namespace Slic3r::GUI {
class Job;
class Plater;
class PartPlate;
}

namespace Slic3r::GUI::JusPrin::Workspace {

class OrcaWorkspaceAdapter final : public IWorkspace
{
public:
    explicit OrcaWorkspaceAdapter(Plater& plater);
    ~OrcaWorkspaceAdapter() override;

    OrcaWorkspaceAdapter(const OrcaWorkspaceAdapter&) = delete;
    OrcaWorkspaceAdapter& operator=(const OrcaWorkspaceAdapter&) = delete;

    WorkspaceSnapshot snapshot() const override;
    CommandResult select_object(ObjectId id) override;
    CommandResult rename_object(ObjectId id, const std::string& name) override;
    CommandResult duplicate_object(ObjectId id) override;
    CommandResult remove_object(ObjectId id) override;
    CommandResult undo() override;
    CommandResult redo() override;
    CommandResult lay_out(const LayoutRequest& request, const std::string& job_handle, LayoutResult& result) override;
    CommandResult place_object(ObjectId id, const PlacementRequest& request, const std::string& job_handle,
                               PlacementResult& result) override;
    CommandResult analyze_object(ObjectId id, const AnalysisRequest& request, ObjectAnalysis& result) const override;
    std::vector<ObjectDetails> object_details() const override;
    ConfiguredPrinter configured_printer() const override;
    std::string current_process_preset() const override;
    PrinterSetupPreview preview_printer_setup(const PrinterSetupRequest& request) const override;
    CommandResult apply_printer_setup(const PrinterSetupRequest& request, PrinterSetupPreview& applied) override;
    WorkspaceHistory history() const override;
    CommandResult restore_history(std::uint64_t step, HistoryPoint point) override;
    CommandResult start_slice(std::optional<PlateId> plate, bool preempt) override;
    SliceReport   slice_report(PlateId plate, const SliceReportRequest& request = {}) const override;
    PresetListResult list_presets(const PresetQuery& query) const override;
    std::vector<PrinterDevice> printers() const override;
    SettingsSearchResult search_settings(const SettingsQuery& query) const override;
    SettingsReadResult read_settings(const std::vector<std::string>& keys, const SettingsTarget& target = {}) const override;
    SettingsPreview preview_settings(const SettingsPatch& patch) const override;
    CommandResult apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed,
                                 SettingsPreview& applied) override;
    std::string auxiliary_data_dir() const override;
    CommandResult export_project_archive(const std::string& file_path) override;
    CommandResult save_project(const std::string& file_path) override;
    ProjectDetails project_details() const override;
    CommandResult open_project(const ProjectOpenRequest& request, std::vector<LoadDecision>& decisions) override;
    CommandResult import_objects(const ImportRequest& request, std::vector<LoadDecision>& decisions,
                                 std::vector<ObjectId>& added) override;
    CommandResult delete_items(const std::vector<DeleteItem>& items) override;
    CommandResult preview_divide(ObjectId id, const DivideRequest& request, DivideResult& result) const override;
    CommandResult divide_object(ObjectId id, const DivideRequest& request, DivideResult& result) override;
    CommandResult merge_objects(const std::vector<ObjectId>& ids, ObjectId& merged) override;
    CommandResult repair_object(ObjectId id, RepairResult& result) override;
    CommandResult plan_regions(const std::vector<RegionRequest>& requests, const std::vector<RegionRecord>& stored,
                               std::vector<RegionRecord>& planned) const override;
    CommandResult apply_regions(const std::vector<RegionRecord>& planned, const std::vector<RegionRecord>& replaced,
                                std::vector<RegionRecord>& applied) override;
    CommandResult remove_regions(const std::vector<RegionRecord>& records) override;
    std::vector<RegionStatus> region_status(const std::vector<RegionRecord>& records) const override;
    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) override;

private:
    struct ResolvedObject
    {
        std::size_t index;
    };

    std::optional<ResolvedObject> resolve(ObjectId id) const;
    // The object a settings target names; the caller has checked it exists.
    ModelObject* settings_object(const SettingsTarget& target) const;
    CommandResult id_error(ObjectId id) const;
    // Regions (OrcaRegions.cpp): the object a record belongs to now, and the
    // edits behind apply and remove, which share one undo step.
    std::optional<std::size_t> region_object(const RegionRecord& record) const;
    void remove_region_artifacts(ModelObject& object, const RegionRecord& record);
    void generate_region_artifacts(ModelObject& object, RegionRecord& record);
    // The slice checks of a report (OrcaSliceChecks.cpp).
    void check_slice(PartPlate& plate, const SliceReportRequest& request, SliceReport& report) const;
    CommandResult change_regions(const std::vector<RegionRecord>& removed, const std::vector<RegionRecord>& added,
                                 const char* snapshot_name);
    void on_project_state_changed(const ProjectStateChanged& change);
    void on_slice_status_changed(wxCommandEvent& event);
    // Per plate: whether it holds a valid slice, and which result. Compared on
    // every slice-status event so only a real change advances the revision.
    // What one plate holds right now. Compared whole on every slice-status
    // event, so only a real change advances the revision.
    struct PlateSlice
    {
        bool          sliced{false};
        std::uint64_t result{0};
        bool          slicing{false};
        bool operator==(const PlateSlice& other) const
        { return sliced == other.sliced && result == other.result && slicing == other.slicing; }
    };
    using SliceState = std::map<std::uint64_t, PlateSlice>;
    SliceState current_slice_state() const;
    void publish_change(WorkspaceChangeReasons reasons);
    void remember_current_ids() const;

    // The change log reads what happened from state OrcaSlicer already keeps:
    // its undo history for model edits, and the presets' differences for
    // settings. So a feature that records an undo step is covered without a
    // line of fork code.
    void record_history_edits();
    void remember_history();
    void record_settings_edits(bool report);
    struct PresetReading
    {
        std::string              name;
        std::vector<PresetDelta> deltas;
    };
    // The timestamp the next undo step will take, and the active position in
    // the history at the last look. Timestamps restart when a project is
    // created or opened, so both are reset at every project replacement.
    std::size_t                m_first_unreported_step{0};
    std::size_t                m_seen_active_step{0};
    std::vector<PresetReading> m_presets; // process, filament, printer

    Plater&                         m_plater;
    ProjectSessionId                m_session;
    WorkspaceChangeHub              m_changes;
    SliceState m_known_slice_state;
    // The last figure each plate could defend, and what took it away. Both are
    // keyed by plate id, which survives a plate being inserted or removed.
    mutable std::map<std::uint64_t, SliceEstimate> m_last_estimate;
    std::map<std::uint64_t, std::string>           m_invalidated_by;
    std::string                                    m_last_change_reason;
    mutable std::set<std::uint64_t> m_known_object_ids;
    ProjectStateSubscription        m_project_subscription;

    // UI jobs the tool system started, by handle, oldest first. A job that
    // outlives the adapter reports into nothing: `m_alive` expires first.
    bool start_job(const std::string& handle, const std::string& kind, std::unique_ptr<Job> job);
    void finish_job(const std::string& handle, const char* state);
    std::vector<WorkspaceJob> m_jobs;
    // The arrange spacing and rotation in force before a tool's arrange,
    // restored when that arrange ends.
    std::optional<std::pair<float, bool>> m_arrange_restore;
    std::shared_ptr<bool>     m_alive = std::make_shared<bool>(true);
};

} // namespace Slic3r::GUI::JusPrin::Workspace
