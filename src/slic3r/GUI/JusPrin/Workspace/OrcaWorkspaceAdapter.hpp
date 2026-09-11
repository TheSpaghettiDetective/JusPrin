#pragma once

#include "Workspace.hpp"
#include "ProjectState.hpp"

#include <map>
#include <set>

namespace Slic3r::GUI {
class Plater;
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
    SettingsSearchResult search_settings(const SettingsQuery& query) const override;
    SettingsReadResult read_settings(const std::vector<std::string>& keys) const override;
    SettingsPreview preview_settings(const SettingsPatch& patch) const override;
    CommandResult apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed,
                                 SettingsPreview& applied) override;
    std::string auxiliary_data_dir() const override;
    CommandResult export_project_archive(const std::string& file_path) override;
    CommandResult import_model(const std::string& file_path) override;
    WorkspaceSubscription subscribe(WorkspaceChangedCallback callback) override;

private:
    struct ResolvedObject
    {
        std::size_t index;
    };

    std::optional<ResolvedObject> resolve(ObjectId id) const;
    CommandResult id_error(ObjectId id) const;
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
};

} // namespace Slic3r::GUI::JusPrin::Workspace
