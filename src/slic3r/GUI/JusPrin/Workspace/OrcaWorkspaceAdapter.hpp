#pragma once

#include "Workspace.hpp"
#include "FileLoads.hpp"
#include "ProjectState.hpp"

#include <map>
#include <set>

namespace Slic3r {
class SlicingProcessCompletedEvent;
}

namespace Slic3r::GUI {
class Job;
class Plater;
class PartPlate;
}

namespace Slic3r::GUI::JusPrin::Workspace {

class ScopedLeastChangeAnswers;

class OrcaWorkspaceAdapter final : public IWorkspace, private FileLoadObserver
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
    // OrcaOutline.cpp
    ProjectOutline outline() const override;
    CommandResult  select_plate(PlateId id) override;
    CommandResult  add_plate() override;
    CommandResult  select_copy(InstanceId id) override;
    CommandResult  select_volume(VolumeId id) override;
    std::vector<PlateAction> plate_actions(PlateId id) const override;
    CommandResult            run_plate_action(PlateId id, PlateAction action) override;
    std::vector<ObjectAction> object_actions(ObjectId id) const override;
    CommandResult             run_object_action(ObjectId id, ObjectAction action, int slot = 0) override;
    CommandResult             toggle_copy_printable(InstanceId id) override;
    CommandResult             open_customization(ObjectId id, CustomizationTool tool) override;
    ConfiguredPrinter configured_printer() const override;
    std::string current_process_preset() const override;
    PrinterSetupPreview preview_printer_setup(const PrinterSetupRequest& request) const override;
    CommandResult apply_printer_setup(const PrinterSetupRequest& request, PrinterSetupPreview& applied) override;
    CommandResult start_slice(std::optional<PlateId> plate, bool preempt) override;
    SliceReport   slice_report(PlateId plate, const SliceReportRequest& request = {}) const override;
    PresetListResult list_presets(const PresetQuery& query) const override;
    std::vector<PrinterDevice> printers() const override;
    SettingsSearchResult search_settings(const SettingsQuery& query) const override;
    SettingsReadResult read_settings(const std::vector<std::string>& keys, const SettingsTarget& target = {}) const override;
    SettingsPreview preview_settings(const SettingsPatch& patch) const override;
    CommandResult apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& expected,
                                 SettingsPreview& applied) override;
    CommandResult remove_filament_preset(const PresetRemoval& removal) override;
    SettingsCatalog settings_catalog() const override;
    CommandResult edit_own_setting(ObjectId id, const OwnSettingEdit& edit) override;
    std::string auxiliary_data_dir() const override;
    CommandResult export_project_archive(const std::string& file_path) override;
    ProjectDetails project_details() const override;
    CommandResult open_project(const ProjectOpenRequest& request, LoadReport& report) override;
    CommandResult generate_calibration(const CalibrationRequest& request, CalibrationResult& result) override;
    CommandResult import_objects(const ImportRequest& request, LoadReport& report,
                                 std::vector<ObjectId>& added) override;
    void set_load_report_listener(std::function<void(const LoadReport&)> listener) override;
    // Why OrcaSlicer did not slice the active plate, in the words of the one
    // that already reads them from OrcaSlicer: the print-issue monitor, which
    // re-asks OrcaSlicer's validation for the setting and object its notice
    // leaves out. Asked only at the moment a slice is refused or a run fails;
    // without a reader the record says only that slicing did not succeed.
    using SliceFailureReader = std::function<std::optional<SliceFailure>()>;
    void set_slice_failure_reader(SliceFailureReader reader) { m_slice_failure_reader = std::move(reader); }
    // The warnings of the current slice of a plate (by Orca id), from the one
    // that already reads them from OrcaSlicer with their setting and object:
    // the print-issue monitor. Nothing while it vouches for no finished slice
    // of that plate. Without a reader no plate carries a warnings summary.
    using SliceWarningsReader = std::function<std::optional<std::vector<SliceWarningFinding>>(std::uint64_t plate)>;
    void set_slice_warnings_reader(SliceWarningsReader reader) { m_slice_warnings_reader = std::move(reader); }
    // A tool's own open or import is running; it settles what the person
    // would otherwise be asked about the file.
    bool tool_load_active() const { return m_tool_loads != 0; }
    // Inside generate_calibration's call to Orca's generator, whose own
    // new_project replaces the empty project this adapter released the
    // person's for: the release hook has nothing of the person's to save.
    bool generating_calibration() const { return m_generating_calibration; }
    // The open project saved to its version history, through Orca's new
    // project and the autosave hook, then its unsaved preset edits dropped,
    // as Orca's UnsavedChangesDialog would otherwise ask. False when it could
    // not be saved, and nothing changed. next_name is UTF-8; empty is Untitled.
    // Also the shell's first step when a person's file opens as a new project.
    bool release_project(const std::string& next_name);
    CommandResult delete_items(const std::vector<DeleteItem>& items) override;
    CommandResult render_view(const RenderRequest& request, RenderedImage& image) override;
    CommandResult read_attachment(const std::string& id, AttachmentContent& content) const override;
    SliceInspection inspect_slice(const SliceInspectRequest& request) const override;
    CommandResult check_export(const ExportRequest& request) const override;
    CommandResult export_file(const ExportRequest& request, ExportResult& result) override;
    CommandResult cancel_slice(bool& stopped) override;
    CommandResult cancel_job(const std::string& handle, bool& stopped) override;
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
    // File loads (FileLoads.hpp). While one runs, every Orca dialog is
    // answered with the least change and written down; once it ends, it waits
    // to be reported, to the tool that started it or, a turn of the event loop
    // later, to the load report listener.
    void file_load_started(const std::vector<boost::filesystem::path>& files, LoadStrategy strategy) override;
    void file_settings_applied() override;
    void file_load_finished() override;
    void file_open_started() override;
    void file_open_finished() override;
    bool file_load_message(const std::string& text, const char* source) override;
    LoadSelectedSetup selected_load_setup() const;
    LoadReport take_load_report(bool started_by_agent);
    LoadedObjectFacts loaded_object_facts(std::size_t model_index, const BedShape& selected_bed,
                                          const std::vector<std::pair<std::string, BedShape>>& beds) const;
    void deliver_load_report();
    void on_slice_status_changed(wxCommandEvent& event);
    void on_process_completed(SlicingProcessCompletedEvent& event);
    // OrcaSlicer was asked to slice and started nothing (SliceRequests.hpp).
    void on_slice_declined();
    // Records on the active plate why its slice did not succeed, as the
    // reader words it; `kind` when the reader has nothing.
    // Whether that changed the record.
    bool record_slice_failure(SliceFailureKind kind);
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
    // The project began empty and has not had a model yet. Steps up to and
    // including the one that brings its first model in (a file opened or
    // dropped into it) are how the project was made, not edits to it; a
    // model added after that is one.
    bool                       m_first_model_pending{false};
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
    // Why each plate's last slice did not succeed, by plate id, recorded when
    // OrcaSlicer refused it or the run failed. Cleared when the plate slices.
    std::map<std::uint64_t, SliceFailure>          m_slice_failures;
    std::uint64_t                                  m_next_slice_failure{0};
    // The plate whose run just ended with an error. The completion event
    // comes before OrcaSlicer has finished with the plate, so the record is
    // made at the slice-status change that follows.
    std::optional<std::uint64_t>                   m_failed_run_plate;
    SliceFailureReader                             m_slice_failure_reader;
    // What each plate's slice last warned about, by plate id, read while the
    // plate is active and kept for as long as that slice is its current one.
    // The finding ids outlive the slice, so a re-slice that reports the same
    // warnings keeps the summary's sequence.
    struct PlateWarnings
    {
        std::uint64_t                result{0}; // the slice result the summary describes
        std::set<std::string>        ids;
        std::optional<SliceWarnings> summary;
    };
    void read_slice_warnings(std::uint64_t plate, std::uint64_t result) const;
    SliceWarningsReader                            m_slice_warnings_reader;
    mutable std::map<std::uint64_t, PlateWarnings> m_slice_warnings;
    mutable ProjectSessionId                       m_slice_warnings_session;
    mutable std::uint64_t                          m_next_slice_warnings{0};
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

    struct ActiveLoad;
    std::unique_ptr<ActiveLoad>            m_active_load;
    std::unique_ptr<ScopedLeastChangeAnswers> m_open_answers;
    std::vector<FileLoadRecord>            m_finished_loads;
    LoadSelectedSetup                      m_setup_before_loads;
    std::vector<std::uint64_t>             m_loaded_objects; // Orca ids of what the finished loads added
    std::uint64_t                          m_session_before_loads{0};
    bool                                   m_report_scheduled{false};
    void flush_file_open_messages();
    void schedule_load_report();
    int                                    m_tool_loads{0}; // a tool's own open or import is running
    bool                                   m_generating_calibration{false};
    std::function<void(const LoadReport&)> m_load_report_listener;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
