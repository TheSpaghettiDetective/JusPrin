#include "HomeSnapshot.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

using nlohmann::json;

const char* to_string(ProjectStatusKind kind)
{
    switch (kind) {
    case ProjectStatusKind::Draft: return "draft";
    case ProjectStatusKind::Sliced: return "sliced";
    case ProjectStatusKind::Printing: return "printing";
    case ProjectStatusKind::Completed: return "completed";
    case ProjectStatusKind::Unknown: break;
    }
    return "unknown";
}

const char* to_string(PrinterState state)
{
    switch (state) {
    case PrinterState::Printing: return "printing";
    case PrinterState::Offline: return "offline";
    case PrinterState::Idle: break;
    }
    return "idle";
}

const char* to_string(PrinterKind kind)
{
    switch (kind) {
    case PrinterKind::Device: return "device";
    case PrinterKind::Named: break;
    }
    return "named";
}

namespace {

void set_if_present(json& target, const char* key, const std::string& value)
{
    if (!value.empty())
        target[key] = value;
}

json project_json(const ProjectEntry& project)
{
    json out{
        {"id", project.id},
        {"name", project.name},
        {"path", project.path},
        {"status", json{{"kind", to_string(project.status_kind)}, {"text", project.status_text}}},
    };
    set_if_present(out, "thumbnailUrl", project.thumbnail_url);
    return out;
}

json printer_json(const PrinterEntry& printer)
{
    json spools = json::array();
    for (const SpoolEntry& spool : printer.spools)
        spools.push_back(json{{"colour", spool.colour}});

    json out{
        {"id", printer.id},
        {"name", printer.name},
        {"kind", to_string(printer.kind)},
        {"state", to_string(printer.state)},
        {"spools", std::move(spools)},
        {"canLaunchMonitor", printer.can_launch_monitor},
        {"canOpenSettings", printer.can_open_settings},
        {"canRename", printer.can_rename},
        {"canRemove", printer.can_remove},
        {"justAdded", printer.just_added},
    };
    set_if_present(out, "statusText", printer.status_text);
    set_if_present(out, "connectionText", printer.connection_text);
    set_if_present(out, "nozzleText", printer.nozzle_text);
    set_if_present(out, "materialLabel", printer.material_label);
    // A bar belongs to a running job; a finished or idle printer sends none
    // rather than a zero-width one.
    if (printer.state == PrinterState::Printing && printer.progress_percent >= 0)
        out["progressPercent"] = printer.progress_percent;
    return out;
}

json receipt_json(const PrinterReceipt& receipt)
{
    return json{
        {"name", receipt.name},
        {"nozzle", receipt.nozzle},
        {"plate", receipt.plate},
        {"filament", receipt.filament},
        {"assumed", receipt.assumed},
    };
}

} // namespace

json state_payload(const Snapshot& snapshot)
{
    json projects = json::array();
    for (const ProjectEntry& project : snapshot.projects)
        projects.push_back(project_json(project));

    json printers = json::array();
    for (const PrinterEntry& printer : snapshot.printers)
        printers.push_back(printer_json(printer));

    json out{
        {"appearance", snapshot.dark ? "dark" : "light"},
        {"projects", std::move(projects)},
        {"printers", std::move(printers)},
        {"printerPanelOpen", snapshot.printer_panel_open},
    };
    if (snapshot.printer_receipt)
        out["printerReceipt"] = receipt_json(*snapshot.printer_receipt);
    return out;
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
