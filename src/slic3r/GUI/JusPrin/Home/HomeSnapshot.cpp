#include "HomeSnapshot.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

using nlohmann::json;

const char* to_string(ProjectStatusKind kind)
{
    switch (kind) {
    case ProjectStatusKind::Draft: return "draft";
    case ProjectStatusKind::Sent: return "sent";
    case ProjectStatusKind::Printing: return "printing";
    case ProjectStatusKind::Completed: return "completed";
    case ProjectStatusKind::Unknown: break;
    }
    return "unknown";
}

const char* to_string(PrinterStatus status)
{
    switch (status) {
    case PrinterStatus::Connecting: return "connecting";
    case PrinterStatus::Connected: return "connected";
    case PrinterStatus::Ready: return "ready";
    case PrinterStatus::Printing: return "printing";
    case PrinterStatus::Paused: return "paused";
    case PrinterStatus::NeedsAttention: return "attention";
    case PrinterStatus::NotConnected: break;
    }
    return "not_connected";
}

const char* to_string(LastPrint last)
{
    switch (last) {
    case LastPrint::Finished: return "finished";
    case LastPrint::Unknown: break;
    }
    return "unknown";
}

const char* to_string(PrinterAction action)
{
    switch (action) {
    case PrinterAction::Monitor: return "monitor";
    case PrinterAction::PrinterWindow: return "printer_window";
    case PrinterAction::Reconnect: return "reconnect";
    case PrinterAction::Connect: return "connect";
    case PrinterAction::None: break;
    }
    return "none";
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

} // namespace

json summary_json(const HistorySummary& summary)
{
    json out{{"gcodes", summary.gcodes},
             {"exports", summary.exports},
             {"sends", summary.sends},
             {"prints", summary.prints},
             {"printingNow", summary.printing_now}};
    set_if_present(out, "lastFinishedAt", summary.last_finished_at);
    return out;
}

namespace {

json project_json(const ProjectEntry& project)
{
    json out{
        {"id", project.id},
        {"name", project.name},
        {"path", project.path},
        {"status", json{{"kind", to_string(project.status_kind)}, {"text", project.status_text}}},
    };
    set_if_present(out, "thumbnailUrl", project.thumbnail_url);
    set_if_present(out, "updatedAt", project.updated_at);
    if (!project.history.empty())
        out["history"] = summary_json(project.history);
    if (project.report_due)
        out["reportDue"] = true;
    return out;
}

json printer_json(const PrinterEntry& printer)
{
    json spools = json::array();
    for (const SpoolEntry& spool : printer.spools) {
        json entry = json::object();
        set_if_present(entry, "material", spool.material);
        set_if_present(entry, "colour", spool.colour);
        spools.push_back(std::move(entry));
    }

    json out{
        {"id", printer.id},
        {"name", printer.name},
        {"kind", to_string(printer.kind)},
        {"status", to_string(printer.status)},
        {"spools", std::move(spools)},
        {"action", to_string(printer.action)},
        {"canOpenSettings", printer.can_open_settings},
        {"canRename", printer.can_rename},
        {"canRemove", printer.can_remove},
    };
    set_if_present(out, "job", printer.job);
    // A bar and a time left belong to a job that is running or paused; a
    // finished or idle printer sends none rather than a zero.
    const bool job_running = printer.status == PrinterStatus::Printing || printer.status == PrinterStatus::Paused ||
                             printer.status == PrinterStatus::NeedsAttention;
    if (job_running && printer.progress_percent >= 0)
        out["progressPercent"] = printer.progress_percent;
    if (job_running && printer.seconds_left > 0)
        out["secondsLeft"] = printer.seconds_left;
    set_if_present(out, "message", printer.message);
    if (printer.status == PrinterStatus::Ready && printer.last_print != LastPrint::Unknown)
        out["lastPrint"] = to_string(printer.last_print);
    if (printer.status == PrinterStatus::NotConnected)
        set_if_present(out, "lastConnectedAt", printer.last_connected_at);
    if (!printer.camera_stream_url.empty() || !printer.camera_snapshot_url.empty()) {
        json camera = json::object();
        set_if_present(camera, "streamUrl", printer.camera_stream_url);
        set_if_present(camera, "snapshotUrl", printer.camera_snapshot_url);
        out["camera"] = std::move(camera);
    }
    set_if_present(out, "connectionLabel", printer.connection_label);
    set_if_present(out, "address", printer.address);
    set_if_present(out, "modelText", printer.model_text);
    if (!printer.history.empty())
        out["history"] = summary_json(printer.history);
    if (printer.assumed)
        out["assumed"] = true;
    if (printer.no_connection)
        out["noConnection"] = true;
    return out;
}

} // namespace

namespace {

json email_step_json(const std::optional<Snapshot::Onboarding::EmailStep>& step)
{
    if (!step)
        return nullptr;
    return {{"step", step->step},
            {"email", step->email},
            {"busy", step->busy},
            {"refusal", step->refusal},
            {"codeResent", step->code_resent}};
}

json import_result_json(const std::optional<std::vector<Snapshot::Onboarding::ImportItem>>& items)
{
    if (!items)
        return nullptr;
    json lines = json::array();
    for (const Snapshot::Onboarding::ImportItem& item : *items)
        lines.push_back({{"kind", item.kind},
                         {"name", item.name},
                         {"renamedTo", item.renamed_to},
                         {"imported", item.imported},
                         {"reason", item.reason}});
    return lines;
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

    const auto& onboarding = snapshot.onboarding;
    return json{
        {"appearance", snapshot.dark ? "dark" : "light"},
        {"projects", std::move(projects)},
        {"projectsProblem", snapshot.projects_problem},
        {"printers", std::move(printers)},
        {"highlightPrinter", snapshot.highlight_printer},
        {"onboarding",
         json{
             {"visible", onboarding.step != OnboardingStep::Hidden},
             {"step", to_string(onboarding.step)},
             {"printersScreen", to_string(onboarding.printers_screen)},
             {"signIn", onboarding.sign_in},
             {"email", email_step_json(onboarding.email_step)},
             {"v1Imported", onboarding.facts.v1_imported},
             {"signInError", onboarding.sign_in_error},
             {"accountEmail", onboarding.account_email},
             {"agentConfigured", onboarding.agent_configured},
             {"agentKeyProvider", onboarding.agent_key_provider},
             {"orca",
              json{
                  {"source", onboarding.orca_source},
                  {"printers", onboarding.orca_printers},
                  {"selected", onboarding.orca_selected},
                  {"filamentCount", onboarding.orca_filaments},
                  {"found", onboarding.facts.orca_printers},
                  {"importResult", import_result_json(onboarding.import_result)},
              }},
             {"printerForm", onboarding.printer_form},
             {"agentCard", to_string(onboarding.agent_card)},
             {"account", onboarding.account},
         }},
    };
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
