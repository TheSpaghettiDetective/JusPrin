#pragma once

#include "ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/PrinterFactsStore.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {
// Bounded canonical read results, shared by all adapters.
inline constexpr std::size_t kToolListLimit = 64;
inline constexpr std::size_t kToolLabelLimit = 256;
// Prose the agent writes for a person to read -- one intent answer, one plan
// decision -- rather than a label projected from Orca state.
inline constexpr std::size_t kToolTextLimit = 2048;
// Which sections of workspace_inspect a call asked for. Summary is what every
// caller got before sections existed, and what a caller that names none gets.
struct InspectSections
{
    bool summary{true};
    bool intent{false};
    bool plan{false};
    bool slicing{false};
    bool history{false};
    bool printer{false};
};

nlohmann::json workspace_inspection(const Workspace::WorkspaceSnapshot& snapshot, InspectSections sections = {});
nlohmann::json intent_section_result(const std::vector<IntentField>& fields);
nlohmann::json plan_section_result(const PlanRecord& plan);
// `handle` names the slice_start call that started the run in flight, when the
// tool system started it; empty when the run is the person's or there is none.
// The key a person's printer facts are filed under: the machine the app is
// working with when there is one, otherwise the printer preset.
std::string printer_fact_key(const Workspace::ConfiguredPrinter& configured,
                             const std::vector<Workspace::PrinterDevice>& devices);
// Configured versus observed, the facts the person confirmed, and where they
// disagree. `facts` are the unexpired facts under printer_fact_key.
nlohmann::json printer_section_result(const Workspace::ConfiguredPrinter& configured,
                                      const std::vector<Workspace::PrinterDevice>& devices,
                                      const std::vector<Workspace::PrinterFact>& facts);
nlohmann::json printer_device_result(const Workspace::PrinterDevice& device);

// The summary's undo flags with the steps themselves.
nlohmann::json history_section_result(const Workspace::WorkspaceSnapshot& snapshot, const Workspace::WorkspaceHistory& history);
nlohmann::json slicing_section_result(const Workspace::WorkspaceSnapshot& snapshot, const std::string& handle);

// Which parts of a sliced plate's report a call asked for. Summary is what a
// caller that names none gets: the numbers anyone checking a print wants first.
struct SliceReportSections
{
    bool summary{true};
    bool findings{false};
    bool material{false};
};

nlohmann::json slice_report_result(const Workspace::SliceReport& report, Workspace::PlateId plate,
                                   const Workspace::WorkspaceSnapshot& snapshot, SliceReportSections sections);
nlohmann::json presets_list_result(const Workspace::PresetListResult& presets, const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json selection_inspection(const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json settings_search_result(const Workspace::SettingsSearchResult&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_read_result(const Workspace::SettingsReadResult&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_preview_result(const Workspace::SettingsPreview&, const Workspace::WorkspaceSnapshot&);
nlohmann::json settings_apply_result(const Workspace::SettingsPreview&, const Workspace::WorkspaceSnapshot&, bool applied);
nlohmann::json setting_issue_result(const Workspace::SettingIssue&);
} // namespace Slic3r::GUI::JusPrin::Agent
