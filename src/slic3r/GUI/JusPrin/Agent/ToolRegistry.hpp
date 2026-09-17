#pragma once

// Canonical, immutable definitions for every command accepted by the JusPrin
// tool coordinator. Adapters may project or filter these definitions, but do
// not get to redefine schemas, approval policy, or executor association.

#include "ToolExecution.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

enum class ToolExposure : std::uint8_t {
    None     = 0,
    InApp    = 1u << 0,
    Mcp      = 1u << 1,
    Internal = 1u << 2,
    // The printer panel's own session. Deliberately outside InApp: the
    // project conversation has no printer panel to draw a card in.
    Printer  = 1u << 3
};

// How a print request goes, for both adapters' instructions: the tools'
// own descriptions say how each one works.
inline constexpr const char* kPrintJourneyGuidance =
    "For a print request: record what the user said with intent_update, ask only the questions whose answer would change what "
    "you do, and record your plan, with everything you assumed instead of asking, through plan_set before you change the "
    "project. Check the slice with slice_report before you export; a G-code file is written with export_file.";

constexpr ToolExposure operator|(ToolExposure lhs, ToolExposure rhs)
{
    return static_cast<ToolExposure>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr bool has_exposure(ToolExposure exposures, ToolExposure exposure)
{
    return (static_cast<std::uint8_t>(exposures) & static_cast<std::uint8_t>(exposure)) != 0;
}

// Availability is evaluated by an adapter from its own request context. It is
// deliberately not live project state: the registry and the advertised MCP
// catalog stay immutable for the process lifetime.
enum class ToolAvailability : std::uint8_t { Always, ImportableAttachment };

// Stable executor association. The registry locates behavior without storing
// workspace state or embedding Orca access in metadata lambdas.
enum class ToolHandler : std::uint8_t {
    WorkspaceInspect,
    SettingsSearch,
    SettingsGet,
    SettingsPreviewPatch,
    SettingsApplyPatch,
    IntentUpdate,
    PlanSet,
    PresetsList,
    ObjectImport,
    ObjectImportFile,
    ProjectDeleteItems,
    PlateLayout,
    ObjectPlace,
    ObjectAnalyze,
    RegionAnnotate,
    ObjectDividePreview,
    ObjectDivide,
    ObjectMerge,
    ObjectRepair,
    ViewRender,
    AttachmentRead,
    SliceInspect,
    ActivityCancel,
    ExportFile,
    PrinterSetupPreview,
    PrinterSetup,
    HistoryRestore,
    ProjectSave,
    ProjectOpen,
    PrinterList,
    SliceStart,
    SliceReportRead,
    RecordBuild,
    RecordExportCopy,
    RecordPhysicalPrint,
    PrinterCatalogSearch,
    PrinterPropose,
    PrinterSuggest,
    PrinterChange
};

struct ToolDefinition
{
    std::string       name;
    std::string       title;
    std::string       description;
    nlohmann::json    input_schema;
    nlohmann::json    output_schema;
    ActionClass       action_class{ActionClass::ReadOnly};
    ToolExposure      exposure{ToolExposure::None};
    ToolAvailability  availability{ToolAvailability::Always};
    ToolHandler       handler{ToolHandler::WorkspaceInspect};
    // Qualifies this mutation for the computation-only exemption in
    // approval_required(). Declared here, beside the action class, so the
    // registry stays the only place a policy distinction is made. Last in the
    // struct because every definition is a positional brace literal.
    bool              computation_only{false};
};

// Whether a call to this tool may carry a planId: every change to the
// project, not plan_set, which records the agent's own words.
inline bool joins_plans(const ToolDefinition& definition)
{
    return definition.action_class != ActionClass::ReadOnly && definition.handler != ToolHandler::PlanSet;
}

struct ToolValidationResult
{
    std::string              arguments_json;
    std::optional<ToolError> error;

    bool valid() const { return !error.has_value(); }
};

class ToolRegistry
{
public:
    static const ToolRegistry& instance();

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    const std::vector<ToolDefinition>& definitions() const { return m_definitions; }
    const ToolDefinition* find(std::string_view name) const;
    std::vector<std::reference_wrapper<const ToolDefinition>> exposed(ToolExposure exposure) const;

    ToolValidationResult validate_call(const ToolDefinition& definition, const std::string& arguments_json) const;
    // Whether this call needs a card. Almost always a property of the
    // definition alone; slice_start is the exception, because pre-empting a
    // run that may be the user's is a decision only they can take.
    bool requires_approval(const ToolDefinition& definition, const std::string& normalized_arguments_json) const;
    bool validate_output(const ToolDefinition& definition, const nlohmann::json& result) const;
    std::string approval_title(const ToolDefinition& definition, const std::string& normalized_arguments_json) const;

private:
    ToolRegistry();

    const std::vector<ToolDefinition> m_definitions;
};

} // namespace Slic3r::GUI::JusPrin::Agent
