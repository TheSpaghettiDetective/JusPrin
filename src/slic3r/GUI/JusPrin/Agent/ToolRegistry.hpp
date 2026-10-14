#pragma once

// Canonical, immutable definitions for every command accepted by the JusPrin
// tool coordinator. Adapters may project or filter these definitions, but do
// not get to redefine schemas or executor association.

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

// Preparation responsibilities for the in-app assistant's instructions: the
// tools' own descriptions say how each one works. Not sent to MCP clients: it
// names intent_update and plan_set, which only the in-app chat has.
inline constexpr const char* kPrintJourneyGuidance =
    "\n\nIntent and setup: For a print request, record the user's explicit purpose as setupTitle with intent_update, even "
    "when no settings change; record confirmed requirements as fields. When the intended use or meaningful print "
    "requirements become clear or change, assess the current material, filament preset and process preset against them. "
    "Use current workspace evidence and read tools to inspect relevant effective settings, including customizations and "
    "object overrides; preset names alone do not establish suitability. Retain suitable choices and customizations. "
    "If they do not fit, explain why and recommend a suitable material, an available compatible preset discovered with "
    "the tools, or a focused settings adjustment. Choosing a material and choosing its saved filament configuration "
    "are distinct decisions. Do not repeat an assessment just because a title was recorded or unchanged intent was restated."
    "\n\nPreparation: When asked to prepare or print a model, inspect the project and establish a suitable printer, "
    "filament and process configuration. Use known physical facts and ask about consequential unknowns that the "
    "available evidence cannot resolve. Material choice "
    "informs filament and process settings; revisit these choices as constraints emerge. Advice requests call for "
    "recommendations, not project changes. For preparation involving multiple changes, record the plan and consequential "
    "assumptions with plan_set before applying them. A clear request for a specific supported change can use its tools directly."
    "\n\nVerification: Slice and inspect with slice_report and, when needed, slice_inspect against the user's requirements "
    "before reporting print readiness. Revisit settings and recheck the slice when needed; explain unresolved issues. "
    "Check the current slice before exporting G-code or sliced 3MF with export_file. The person starts a physical print "
    "with the app's Send, after its preflight; preparing a print does not start one.";

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
    ProjectOpen,
    PrinterList,
    SliceStart,
    SliceReportRead,
    RecordBuild,
    RecordExportCopy,
    RecordPhysicalPrint,
    PrinterIdentify,
    PrinterAdd,
    PrinterChange,
    PrinterConnectionStatus,
    PrinterConnect
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
};

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
    bool validate_output(const ToolDefinition& definition, const nlohmann::json& result) const;
    std::string activity_title(const ToolDefinition& definition, const std::string& normalized_arguments_json) const;

private:
    ToolRegistry();

    const std::vector<ToolDefinition> m_definitions;
};

} // namespace Slic3r::GUI::JusPrin::Agent
