// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaHomeBackend.hpp"

#include "slic3r/GUI/BindDialog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevHMS.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/HMS.hpp"
#include "slic3r/GUI/JusPrin/Home/ProjectStatus.hpp"
#include "slic3r/GUI/JusPrin/Shell/GcodeHistoryRecorder.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/V1Import/V1Import.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/SelectMachinePop.hpp"
#include "slic3r/GUI/JusPrin/Printers/HostLaneReader.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Printers/PrinterOrigin.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/OrcaPrinterBackend.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/V1Import/V1Import.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <wx/base64.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

// How long a project card keeps asking how a finished print came out (board
// 8d). The design session's guess; open in the design doc.
constexpr int  kReportOfferDays       = 14;
constexpr int  kReportThumbnailPixels = 96;
// A photo of tens of megabytes over a slow uplink.
constexpr long kReportUploadSeconds   = 300;

// Upstream's own predicate, not a copy of it. The first version of this listed
// RUNNING, PAUSE and SLICING and left out PREPARE, so a printer that was
// preparing a job showed on Home as idle. One implementation cannot drift from
// itself.
bool machine_is_printing(const MachineObject& machine)
{
    return MachineObject::is_in_printing_status(machine.print_status);
}

// The separator the design uses inside a status line, as the shell's status
// row does. Kept local for the same reason it is local there: it is a
// punctuation constant, not an interface.
const wxString& middle_dot()
{
    static const wxString s = wxString::FromUTF8(" \xC2\xB7 ");
    return s;
}

// A card id says what the card stands for, so an action never has to guess
// whether a name is a printer or a device serial.
const std::string kNamedPrefix  = "named:";
const std::string kDevicePrefix = "device:";
const char* const kOnboardingSection = "jusprin_onboarding";

bool config_true(const AppConfig* config, const char* key)
{
    if (config == nullptr)
        return false;
    const std::string value = config->get(kOnboardingSection, key);
    return value == "true" || value == "1";
}

// The printer first run installed for a person who skipped choosing one, by
// the name it was saved under; Home marks its card "assumed".
const char* const kAssumedPrinter = "assumed_printer";
// Home's Agent group showed the account's credits and the person closed it.
const char* const kAccountCardDismissed = "account_card_dismissed";

// The rest of `id` after `prefix`, or nothing when it has another prefix.
std::optional<std::string> strip(const std::string& id, const std::string& prefix)
{
    if (id.compare(0, prefix.size(), prefix) != 0)
        return std::nullopt;
    return id.substr(prefix.size());
}

std::string utf8(const wxString& text) { return std::string(text.ToUTF8()); }

// "X1 Carbon" for the model a profile or a device names: the profile's
// "Bambu Lab X1 Carbon", or a device's own id, "BL-P001". The loaded vendor
// profiles answer, as the catalogue would without reading its files again.
// The brand stays when the rest is only a number: "2.4 350" names no printer,
// "Voron 2.4 350" does. A profile model no vendor lists keeps its own name; a
// device id, nothing.
std::string model_name(const std::string& printer_model, const std::string& device_model_id = {})
{
    for (const auto& [id, vendor] : wxGetApp().preset_bundle->vendors)
        for (const VendorProfile::PrinterModel& model : vendor.models)
            if (printer_model.empty() ? !device_model_id.empty() && model.model_id == device_model_id
                                      : model.id == printer_model) {
                const std::string rest = PrinterSetup::split_brand(vendor.name, model.id).second;
                return !rest.empty() && std::isdigit(static_cast<unsigned char>(rest.front())) ? model.id : rest;
            }
    return printer_model;
}

// "X1 Carbon · 0.4 mm", or whichever half is known.
std::string model_text(const std::string& model, double nozzle)
{
    wxString text = wxString::FromUTF8(model);
    if (nozzle > 0.)
        text += (text.empty() ? wxString() : middle_dot()) + wxString::Format("%g mm", nozzle);
    return utf8(text);
}

// "192.168.1.42:7125" for "http://192.168.1.42:7125/": the host and port as
// saved, without the scheme, a path, or any credentials written into it.
std::string host_address(const std::string& print_host)
{
    std::string address = print_host;
    if (const size_t scheme = address.find("://"); scheme != std::string::npos)
        address = address.substr(scheme + 3);
    address = address.substr(0, address.find('/'));
    if (const size_t at = address.rfind('@'); at != std::string::npos)
        address = address.substr(at + 1);
    return address.empty() ? print_host : address;
}

double reported_nozzle(MachineObject& machine)
{
    const DevExtderSystem* extruders = machine.GetExtderSystem();
    return extruders != nullptr ? extruders->GetNozzleDiameter(0) : 0.;
}

// The name a print host type goes by on a card: Orca's own label for it, but
// for the two Orca labels that name more than the host.
std::string host_label(const DynamicPrintConfig& config)
{
    const auto* type = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (type == nullptr)
        return {};
    if (type->value == htMoonraker)
        return "Klipper";
    if (type->value == htOctoPrint)
        return "OctoPrint";
    const ConfigOptionDef* def = print_config_def.get("host_type");
    for (size_t i = 0; def != nullptr && i < def->enum_values.size() && i < def->enum_labels.size(); ++i)
        if (ConfigOptionEnum<PrintHostType>::get_enum_values().at(def->enum_values[i]) == type->value)
            return def->enum_labels[i];
    return {};
}

std::string iso_utc(std::chrono::system_clock::time_point at)
{
    return utf8(wxDateTime(std::chrono::system_clock::to_time_t(at)).ToUTC().FormatISOCombined() + "Z");
}

// "Vent grille" for "Vent grille.gcode", "plates/Vent grille.gcode" or a
// Bambu job's "Vent grille.gcode.3mf": the job as the person named it.
std::string job_name(const std::string& filename)
{
    std::string name = filename.substr(filename.find_last_of('/') == std::string::npos ? 0 : filename.find_last_of('/') + 1);
    for (const char* extension : {".gcode.3mf", ".3mf", ".gcode", ".GCODE", ".gco", ".g", ".bgcode"})
        if (const size_t length = std::strlen(extension); name.size() > length && name.compare(name.size() - length, length, extension) == 0) {
            name.resize(name.size() - length);
            break;
        }
    return name;
}

// Bambu's code for a print that was stopped: "The task was canceled."
constexpr int kBambuPrintCancelled = 0x0300400C;

// An error that stopped the job for a person to deal with, as upstream's
// device page tells it: not a cancel, and not one Bambu keeps to itself.
bool has_print_error(MachineObject& machine)
{
    return machine.print_error > 0 && machine.print_error != kBambuPrintCancelled &&
           !wxGetApp().get_hms_query()->is_internal_error(&machine, machine.print_error);
}

// What a Bambu printer says is wrong, in its own words: the job's error, else
// its most serious current health message. A code the app has no words for
// is still said, as the code.
std::string device_message(MachineObject& machine)
{
    HMSQuery& query = *wxGetApp().get_hms_query();
    if (has_print_error(machine)) {
        const wxString text = query.query_print_error_msg(&machine, machine.print_error);
        return utf8(!text.empty() ? text :
                                    wxString::Format(_L("The printer reports error %s."), machine.get_print_error_str()));
    }
    for (const DevHMSItem& item : machine.GetHMS()->GetHMSItems())
        if (item.get_level() == HMS_FATAL || item.get_level() == HMS_SERIOUS) {
            const wxString text = query.query_hms_msg(&machine, item.get_long_error_code());
            return utf8(!text.empty() ? text :
                                        wxString::Format(_L("The printer reports error %s."), item.get_long_error_code()));
        }
    return {};
}

// What the device reports about itself and its job, over whatever the card
// had. A device silent for 30 seconds is not connected, with Reconnect only
// where there is a connection to retry: a named printer's verified one.
void describe_device(MachineObject& machine, PrinterEntry& printer)
{
    const std::string kind = machine.connection_type();
    if (PrinterSetup::has_recent_printer_data(machine)) {
        printer.connection_label = utf8(kind == "lan" ? _L("LAN") : _L("Cloud"));
        printer.action           = PrinterAction::Monitor;
        if (machine_is_printing(machine)) {
            const bool paused        = machine.print_status == "PAUSE";
            printer.status           = paused ? PrinterStatus::Paused : PrinterStatus::Printing;
            printer.job              = job_name(machine.subtask_name);
            printer.progress_percent = machine.mc_print_percent;
            printer.seconds_left     = machine.mc_left_time;
            // A printer that paused itself on an error waits for a person;
            // one paused otherwise says why only if it has a health message.
            if (paused) {
                printer.message = device_message(machine);
                if (has_print_error(machine))
                    printer.status = PrinterStatus::NeedsAttention;
            }
        } else {
            printer.status = PrinterStatus::Ready;
            if (machine.print_status == "FINISH")
                printer.last_print = LastPrint::Finished;
        }
    } else if (printer.kind == PrinterKind::Named && PrinterSetup::has_verified_printer_connection(machine.get_dev_id())) {
        printer.connection_label = utf8(kind == "lan" ? _L("LAN") : _L("Cloud"));
        printer.status           = PrinterStatus::NotConnected;
        printer.message          = utf8(_L("The printer stopped sending updates."));
        printer.action           = PrinterAction::Reconnect;
    }
}

// What a print host's latest reading says, over the card's starting point of
// a saved address that answered. Moonraker says what Klipper is doing; any
// other host only whether it answers, so it stays Connected.
void describe_host(const Printers::HostStatus& status, PrinterEntry& printer)
{
    if (status.link == Printers::HostLink::Checking) {
        printer.status = PrinterStatus::Connecting;
        printer.action = PrinterAction::None;
        return;
    }
    if (status.link == Printers::HostLink::Unreachable) {
        printer.status  = PrinterStatus::NotConnected;
        printer.message = utf8(wxString::Format(_L("%s isn't answering."), wxString::FromUTF8(printer.address)));
        printer.action  = PrinterAction::Reconnect;
        if (status.last_connected)
            printer.last_connected_at = iso_utc(*status.last_connected);
        return;
    }
    printer.status = PrinterStatus::Connected;
    printer.action = PrinterAction::PrinterWindow;
    if (status.camera) {
        printer.camera_stream_url   = status.camera->stream_url;
        printer.camera_snapshot_url = status.camera->snapshot_url;
    }
    if (!status.klipper)
        return;
    const Printers::KlipperState& klipper = *status.klipper;
    switch (Printers::host_activity(klipper)) {
    case Printers::HostActivity::Starting:
        printer.status = PrinterStatus::Connecting;
        return;
    case Printers::HostActivity::KlipperDown:
        // The Pi answers and the printer does not: not connected, in
        // Klipper's own words, and the host's page is where to restart it.
        printer.status  = PrinterStatus::NotConnected;
        printer.message = klipper.klippy_message.empty()
                              ? utf8(_L("Klipper isn't running."))
                              : utf8(_L("Klipper stopped:") + " " + wxString::FromUTF8(klipper.klippy_message));
        if (status.last_connected)
            printer.last_connected_at = iso_utc(*status.last_connected);
        return;
    case Printers::HostActivity::Printing:
    case Printers::HostActivity::Paused: {
        const bool paused = Printers::host_activity(klipper) == Printers::HostActivity::Paused;
        printer.status    = paused ? PrinterStatus::Paused : PrinterStatus::Printing;
        printer.job       = job_name(klipper.filename);
        if (klipper.progress)
            printer.progress_percent = static_cast<int>(*klipper.progress * 100.);
        // Klipper gives no time left; the slicer's estimate less the time
        // printed so far is what Mainsail shows as its file estimate.
        if (status.last_job && status.last_job->status == "in_progress" && status.last_job->filename == klipper.filename &&
            status.last_job->estimated_time > 0. && klipper.print_duration > 0.)
            printer.seconds_left = static_cast<int>(std::max(0., status.last_job->estimated_time - klipper.print_duration));
        printer.message = klipper.message;
        return;
    }
    case Printers::HostActivity::Ready:
        printer.status = PrinterStatus::Ready;
        if (status.last_job && status.last_job->status == "completed")
            printer.last_print = LastPrint::Finished;
        return;
    }
}

MachineObject* my_machine(const std::string& device_id)
{
    DeviceManager* devices = wxGetApp().getDeviceManager();
    return devices != nullptr ? devices->get_my_machine(device_id) : nullptr;
}

std::string gone() { return utf8(_L("This printer no longer exists.")); }

// "Oct 20" in local time, or "Oct 20, 2025" outside this year; empty for a
// time that is not ISO 8601 in UTC, such as one a record was given by hand.
wxString short_date(const std::string& iso_utc)
{
    wxDateTime when;
    if (iso_utc.size() < 19 || !when.ParseISOCombined(wxString::FromUTF8(iso_utc.substr(0, 19))))
        return {};
    when.MakeFromTimezone(wxDateTime::UTC);
    const wxString day = wxString::Format("%s %d", wxDateTime::GetMonthName(when.GetMonth(), wxDateTime::Name_Abbr), when.GetDay());
    return when.GetYear() == wxDateTime::Now().GetYear() ? day : wxString::Format("%s, %d", day, when.GetYear());
}

// A recorded printer as the rest of Home names one, "X1 Carbon", and a
// filament as the sidebar does, "Bambu PLA Basic". A preset no longer
// installed keeps the name the record has.
wxString printer_label(const std::string& preset_name)
{
    const Preset*     preset = wxGetApp().preset_bundle->printers.find_preset(preset_name);
    const std::string model  = preset != nullptr ? model_name(preset->config.opt_string("printer_model")) : std::string();
    return wxString::FromUTF8(model.empty() ? preset_name : model);
}

wxString material_label(const std::string& preset_name)
{
    const Preset* preset = wxGetApp().preset_bundle->filaments.find_preset(preset_name);
    return preset != nullptr ? SetupCommands::display_alias(*preset) : wxString::FromUTF8(preset_name);
}

// "Draft", "Sent Oct 20 · X1 Carbon · PLA Matte", "Printed Oct 20 · X1
// Carbon · PLA".
wxString ledger_text(const LedgerStatus& status)
{
    const wxString date = short_date(status.at);
    wxString       text;
    switch (status.kind) {
    case ProjectStatusKind::Completed: text = date.empty() ? _L("Printed") : wxString::Format(_L("Printed %s"), date); break;
    case ProjectStatusKind::Sent: text = date.empty() ? _L("Sent") : wxString::Format(_L("Sent %s"), date); break;
    default: return _L("Draft");
    }
    for (const wxString& part : {printer_label(status.printer), material_label(status.material)})
        if (!part.empty())
            text += middle_dot() + part;
    return text;
}

} // namespace

OrcaHomeBackend::OrcaHomeBackend(MainFrame& frame)
    : m_frame(frame)
    , m_importer(frame)
{}

bool OrcaHomeBackend::dark() const { return wxGetApp().dark_mode(); }

OnboardingProgress OrcaHomeBackend::onboarding_progress() const
{
    OnboardingProgress progress;
    const AppConfig* config = wxGetApp().app_config;
    if (config == nullptr)
        return progress;
    // "dismissed" is the earlier onboarding's way out, and as done as finishing.
    const std::string status = config->get(kOnboardingSection, "status");
    progress.status          = status == "completed" || status == "dismissed" ? OnboardingStatus::Completed :
                                                                                OnboardingStatus::Unfinished;
    progress.welcome_left   = config_true(config, "welcome_left");
    progress.fresh_start    = config_true(config, "fresh_start");
    progress.printer_chosen = config_true(config, "printer_chosen");
    return progress;
}

void OrcaHomeBackend::set_autosave(Workspace::ProjectAutosave* autosave)
{
    m_autosave = autosave;
    AppConfig* config = wxGetApp().app_config;
    if (m_autosave == nullptr || config == nullptr || !config->get(kOnboardingSection, "status").empty())
        return;
    // Someone who used JusPrin before first run existed has nothing to be
    // welcomed to. Decided here, before a file opened at launch is saved as
    // a project; a JusPrin v1 import is welcomed like a new install.
    const auto managed = m_autosave->projects();
    if (managed && !managed->empty() && !V1Import::read_report(std::filesystem::u8path(data_dir())).has_value())
        complete_onboarding();
}

OnboardingFacts OrcaHomeBackend::onboarding_facts() const
{
    OnboardingFacts facts;
    facts.v1_imported = V1Import::read_report(std::filesystem::u8path(data_dir())).has_value();
    if (const PresetBundle* presets = wxGetApp().preset_bundle)
        facts.printer_from_file = Printers::printer_from_file(presets->printers.get_selected_preset());
    facts.import_result_open = m_import_result.has_value();
    return facts;
}

bool OrcaHomeBackend::agent_configured() const
{
    const AppConfig* config = wxGetApp().app_config;
    return config != nullptr && (config->get("jusprin_agent", "enabled") == "true" || config_true(config, "tool_connected"));
}

void OrcaHomeBackend::save_onboarding(const OnboardingProgress& progress)
{
    AppConfig* config = wxGetApp().app_config;
    if (config == nullptr)
        return;
    // AppConfig::set takes a string or a bool, and a const char* goes to the bool.
    config->set(kOnboardingSection, "status", std::string(to_string(progress.status)));
    config->set(kOnboardingSection, "welcome_left", progress.welcome_left);
    config->set(kOnboardingSection, "fresh_start", progress.fresh_start);
    config->set(kOnboardingSection, "printer_chosen", progress.printer_chosen);
    config->save();
}

void OrcaHomeBackend::complete_onboarding()
{
    OnboardingProgress progress = onboarding_progress();
    if (progress.status != OnboardingStatus::Unfinished)
        return;
    progress.status = OnboardingStatus::Completed;
    save_onboarding(progress);
}

// The add-printer form on the Printers step: the printer panel's own form and
// the conversation that owns it, with no thread around it. What the form
// would tell the model has no one to read it here, and no turn starts.
class OrcaHomeBackend::PrinterFormSession final : public PrinterSetup::IConversationHost
{
public:
    explicit PrinterFormSession(OrcaHomeBackend& owner)
        : m_owner(owner)
        , m_backend(*wxGetApp().plater(), owner.profile_catalog())
        , m_conversation(m_backend, *this)
    {
        m_conversation.start(PrinterSetup::ConversationMode::Add);
        m_conversation.handle_page_message("printer_action", nlohmann::json{{"action", "manual_setup"}});
    }

    PrinterSetup::PrinterConversation& conversation() { return m_conversation; }
    nlohmann::json form() const { return m_conversation.state_json().value("form", nlohmann::json()); }

    // No thread and no model: no messages, and no tool call to finish.
    nlohmann::json chat_record() const override
    {
        nlohmann::json record = PrinterSetup::connection_chat_record({}, {});
        record["app"]         = SLIC3R_VERSION;
        return record;
    }
    bool finish_tool(const std::string&, const Agent::ToolExecutionCoordinator::ExtensionResult&) override { return false; }
    bool update_tool(const std::string&, const std::string&, bool) override { return false; }
    bool open_in_browser(const std::string& url) override { return wxLaunchDefaultBrowser(wxString::FromUTF8(url)); }
    std::string app_version() const override { return SLIC3R_VERSION; }
    std::string post_note(const std::string&) override { return "home-note-" + std::to_string(++m_notes); }
    std::string post_opening(const std::string&) override { return "home-note-" + std::to_string(++m_notes); }
    void        start_turn() override {}
    void        session_changed() override { m_owner.m_onboarding_changed = true; }
    void        profile_changed() override {}
    void        close_panel() override {}
    void        printers_changed(const std::string& added) override
    {
        if (!added.empty())
            m_owner.printer_chosen();
    }

private:
    OrcaHomeBackend&                  m_owner;
    PrinterSetup::OrcaPrinterBackend  m_backend;
    PrinterSetup::PrinterConversation m_conversation;
    unsigned                          m_notes{0};
};

void OrcaHomeBackend::printer_chosen()
{
    OnboardingProgress progress = onboarding_progress();
    progress.printer_chosen     = true;
    save_onboarding(progress);
    m_onboarding_changed = true;
}

Agent::ICloudSignIn& OrcaHomeBackend::sign_in()
{
    if (!m_sign_in)
        m_sign_in = Agent::make_cloud_sign_in(wxGetApp().app_config);
    return *m_sign_in;
}

void OrcaHomeBackend::request_account()
{
    if (!m_models) {
        std::weak_ptr<void> alive = m_alive;
        m_models = Agent::make_chat_model_source(wxGetApp().app_config, [alive](std::function<void()> run) {
            wxGetApp().CallAfter([alive, run = std::move(run)] {
                if (!alive.expired())
                    run();
            });
        });
    }
    m_account_requested = true;
    m_models->fetch_account([this](std::optional<nlohmann::json> account) {
        m_account            = account ? std::move(*account) : nlohmann::json();
        m_onboarding_changed = true;
    });
}

Snapshot::Onboarding OrcaHomeBackend::onboarding()
{
    Snapshot::Onboarding state;
    state.progress = onboarding_progress();
    state.facts    = onboarding_facts();
    state.step     = next_onboarding_step(state.progress, state.facts);
    // Home is on screen: the sequence is over.
    if (state.step == OnboardingStep::Hidden && state.progress.status == OnboardingStatus::Unfinished) {
        complete_onboarding();
        state.progress.status = OnboardingStatus::Completed;
    }

    if (state.step == OnboardingStep::Printers) {
        // Read only on the Printers step: this runs on every Home refresh.
        OrcaProfileSource source   = m_importer.detect(std::nullopt);
        state.facts.orca_printers  = source.has_printers();
        state.printers_screen      = printers_screen(state.progress, state.facts);
        if (state.printers_screen == PrintersScreen::OrcaImport) {
            state.orca_source    = source.root;
            state.orca_printers  = source.printer_names();
            // The page says which printer JusPrin starts with.
            if (std::find(state.orca_printers.begin(), state.orca_printers.end(), source.selected) != state.orca_printers.end())
                state.orca_selected = source.selected;
            // Only saved presets bring filaments; a ticked printer starts with its profile's.
            state.orca_filaments = int(source.presets.filaments.size());
        }
        if (m_import_result)
            state.import_result = import_result_lines(*m_import_result);
    }
    const bool form_shown = state.step == OnboardingStep::Printers && state.printers_screen == PrintersScreen::AddPrinter;
    if (form_shown && !m_printer_form)
        m_printer_form = std::make_unique<PrinterFormSession>(*this);
    else if (!form_shown)
        m_printer_form.reset();
    if (m_printer_form)
        state.printer_form = m_printer_form->form();

    const AppConfig* config = wxGetApp().app_config;
    const auto account      = sign_in().account();
    state.account_email     = account ? account->email : std::string();
    switch (sign_in().phase()) {
    case Agent::CloudSignInPhase::Browser: state.sign_in = "browser"; break;
    case Agent::CloudSignInPhase::Email: state.sign_in = "email"; break;
    case Agent::CloudSignInPhase::Checking: state.sign_in = "checking"; break;
    case Agent::CloudSignInPhase::Idle: state.sign_in = "idle"; break;
    }
    if (const std::optional<Agent::CloudEmailStep> email = sign_in().email_step()) {
        using Step = Agent::CloudEmailStep::Step;
        state.email_step = Snapshot::Onboarding::EmailStep{email->step == Step::Code ? "code" : "choose", email->email,
                                                            email->busy, email->refusal, email->code_resent};
    }
    state.sign_in_error    = m_sign_in_error;
    state.agent_configured = agent_configured();
    // The agent on the person's own key, by the provider's name.
    if (config != nullptr && config->get("jusprin_agent", "enabled") == "true")
        if (const Agent::ProviderProfile* profile = Agent::profile_for(config->get("jusprin_agent", "provider"));
            profile != nullptr && !profile->account_sign_in)
            state.agent_key_provider = profile->display_name;
    state.agent_card       = home_agent_card(state.agent_configured, !state.account_email.empty(),
                                             config_true(config, kAccountCardDismissed));
    // The credits the welcome's card and Home's account group show.
    if (!state.account_email.empty() && !m_account_requested)
        request_account();
    state.account = m_account;
    return state;
}

bool OrcaHomeBackend::poll_onboarding()
{
    if (m_sign_in && m_sign_in->phase() != Agent::CloudSignInPhase::Idle) {
        const Agent::CloudSignInPhase before = m_sign_in->phase();
        // The email steps move within one phase: a code asked for, refused, sent again.
        const std::optional<Agent::CloudEmailStep> email_before = m_sign_in->email_step();
        const std::optional<Agent::CloudSignInOutcome> outcome = m_sign_in->poll();
        m_onboarding_changed = m_onboarding_changed || m_sign_in->phase() != before || m_sign_in->email_step() != email_before;
        if (outcome) {
            m_onboarding_changed = true;
            switch (outcome->kind) {
            case Agent::CloudSignInOutcome::Kind::SignedIn:
                m_sign_in_error = outcome->saved ? std::string() :
                                                   utf8(_L("This sign-in could not be saved, so you will have to sign in again next time "
                                                           "JusPrin starts."));
                // The docked agent runs on the account from now on.
                if (ShellController* shell = installed_shell())
                    shell->reload_agent_settings();
                request_account();
                break;
            case Agent::CloudSignInOutcome::Kind::Expired:
                m_sign_in_error = utf8(_L("That sign-in expired. Try again."));
                break;
            case Agent::CloudSignInOutcome::Kind::Failed:
                m_sign_in_error = outcome->error.message.empty() ? utf8(_L("JusPrin couldn't sign you in. Try again.")) :
                                                                   outcome->error.message;
                break;
            }
        }
    }
    if (m_printer_form)
        m_printer_form->conversation().tick(std::chrono::steady_clock::now());
    return std::exchange(m_onboarding_changed, false);
}

void OrcaHomeBackend::start_sign_in()
{
    m_sign_in_error.clear();
    Agent::ICloudSignIn& cloud = sign_in();
    if (cloud.phase() != Agent::CloudSignInPhase::Idle)
        return; // A second click while the browser has the first.
    if (!cloud.start(Agent::CloudBrowserStart::Google))
        m_sign_in_error = utf8(_L("JusPrin couldn't start signing in. Try again."));
}

void OrcaHomeBackend::sign_in_with_email(const std::string& email)
{
    m_sign_in_error.clear();
    Agent::ICloudSignIn& cloud = sign_in();
    if (cloud.phase() != Agent::CloudSignInPhase::Idle)
        return; // A second click while the first is with the server.
    if (!cloud.start_email(email))
        m_sign_in_error = utf8(_L("JusPrin couldn't sign you in. Try again."));
}

// The page offers each of these only on the step that takes it; one sent as
// the step changes under it (a double click) is refused by the sign-in and
// changes nothing.
void OrcaHomeBackend::submit_sign_in_code(const std::string& code)
{
    if (m_sign_in)
        m_sign_in->submit_code(code);
}

void OrcaHomeBackend::submit_sign_in_password(const std::string& password)
{
    if (m_sign_in)
        m_sign_in->submit_password(password);
}

void OrcaHomeBackend::send_sign_in_code()
{
    if (m_sign_in)
        m_sign_in->send_code();
}

void OrcaHomeBackend::reopen_sign_in_browser()
{
    if (const std::string url = sign_in().browser_url(); !url.empty())
        wxLaunchDefaultBrowser(wxString::FromUTF8(url));
}

void OrcaHomeBackend::open_sign_in_link(const std::string& link)
{
    // JusPrin accounts are Obico accounts; the Terms and Privacy Policy are Obico's.
    const std::string url = link == "reset_password" ? Agent::cloud_password_reset_url(wxGetApp().app_config)
                            : link == "terms"        ? std::string("https://www.obico.io/terms.html")
                            : link == "privacy"      ? std::string("https://www.obico.io/privacy.html")
                                                     : std::string();
    if (url.empty())
        throw std::invalid_argument("Home asked to open an unknown sign-in link: " + link);
    wxLaunchDefaultBrowser(wxString::FromUTF8(url));
}

void OrcaHomeBackend::cancel_sign_in()
{
    // Cancelling one that already finished is a benign race.
    if (m_sign_in && m_sign_in->phase() != Agent::CloudSignInPhase::Idle)
        m_sign_in->cancel();
}

// Both doors open Preferences on its Agent section, which holds the keys and
// the AI tools alike.
void OrcaHomeBackend::open_agent_door(const std::string& /*door*/)
{
    ShellController* shell = installed_shell();
    if (shell == nullptr)
        return;
    // Preferences only closes; what it was closed with decides whether the
    // welcome is left. Closed unfinished, nothing was chosen.
    std::weak_ptr<void> alive = m_alive;
    shell->request_preferences("agent", [this, alive](const std::string& done) {
        if (alive.expired())
            return;
        // Whatever was saved there shows on Home's Agent group.
        m_onboarding_changed = true;
        if (done.empty())
            return;
        AppConfig* config = wxGetApp().app_config;
        if (done == "tool" && config != nullptr) {
            config->set(kOnboardingSection, "tool_connected", true);
            config->save();
        }
        OnboardingProgress progress = onboarding_progress();
        if (next_onboarding_step(progress, onboarding_facts()) == OnboardingStep::Welcome) {
            progress.welcome_left = true;
            save_onboarding(progress);
        }
    });
}

void OrcaHomeBackend::leave_welcome()
{
    OnboardingProgress progress = onboarding_progress();
    progress.welcome_left       = true;
    save_onboarding(progress);
}

void OrcaHomeBackend::step_back()
{
    OnboardingProgress progress = onboarding_progress();
    if (progress.fresh_start)
        progress.fresh_start = false;
    else
        progress.welcome_left = false;
    save_onboarding(progress);
}

void OrcaHomeBackend::ask_agent_for_printer()
{
    complete_onboarding();
    add_printer();
}

void OrcaHomeBackend::start_fresh()
{
    OnboardingProgress progress = onboarding_progress();
    progress.fresh_start        = true;
    save_onboarding(progress);
}

void OrcaHomeBackend::printer_form_message(const nlohmann::json& payload)
{
    const std::string action = payload.value("action", std::string());
    // The form's own steps; its Cancel is the step's Back.
    if (m_printer_form && (action == "form_options" || action == "form_finish"))
        m_printer_form->conversation().handle_page_message("printer_action", payload);
}

void OrcaHomeBackend::dismiss_account_card()
{
    if (AppConfig* config = wxGetApp().app_config) {
        config->set(kOnboardingSection, kAccountCardDismissed, true);
        config->save();
    }
}

std::string OrcaHomeBackend::import_orca_printers(const std::vector<std::string>& printers)
{
    OrcaProfileSource source = m_importer.detect(std::nullopt);
    if (!source.has_printers())
        return utf8(_L("JusPrin found no OrcaSlicer printers to bring. Choose your printer instead."));
    OrcaImportChoice chosen = orca_import_chosen(source.presets, source.ticked, printers);
    m_orca_selected         = source.selected;
    m_import_result         = bring_orca_items(std::move(chosen.profile_printers), chosen.files);
    return {};
}

std::vector<OrcaImportItem> OrcaHomeBackend::bring_orca_items(std::vector<OrcaImportItem> ticked, const std::vector<std::string>& files)
{
    std::vector<OrcaImportItem> items = m_importer.bring(std::move(ticked), files, m_orca_selected, OrcaImporter::Selection::Brought);
    if (std::any_of(items.begin(), items.end(), [](const OrcaImportItem& item) {
            return item.kind == OrcaImportItem::Kind::Printer && item.outcome == OrcaImportItem::Outcome::Imported;
        }))
        printer_chosen();
    return items;
}

void OrcaHomeBackend::finish_orca_import()
{
    if (!m_import_result)
        throw std::logic_error("Home continued from an OrcaSlicer import with no result on screen");
    const bool brought_printer = std::any_of(m_import_result->begin(), m_import_result->end(), [](const OrcaImportItem& item) {
        return item.kind == OrcaImportItem::Kind::Printer && item.outcome == OrcaImportItem::Outcome::Imported;
    });
    m_import_result.reset();
    // Nothing to print with yet: the add-printer form.
    if (!brought_printer)
        start_fresh();
}

const PrinterSetup::PrinterCatalog& OrcaHomeBackend::profile_catalog() { return m_importer.profile_catalog(); }

std::vector<Snapshot::Onboarding::ImportItem> OrcaHomeBackend::import_result_lines(const std::vector<OrcaImportItem>& items)
{
    std::vector<Snapshot::Onboarding::ImportItem> lines;
    for (const OrcaImportItem& item : items)
        // Nothing to say about a preset JusPrin already had.
        if (item.outcome != OrcaImportItem::Outcome::AlreadyThere)
            lines.push_back({orca_import_kind(item.kind), item.name, item.renamed_to,
                             item.outcome == OrcaImportItem::Outcome::Imported, orca_import_reason(item)});
    return lines;
}

std::string OrcaHomeBackend::assume_default_printer()
{
    Plater* plater = wxGetApp().plater();
    AppConfig* config = wxGetApp().app_config;
    if (plater == nullptr || config == nullptr)
        throw std::logic_error("Home installed a printer before the app finished starting");
    // A fresh install has no printer, and the person named none. They get
    // the bundled printer that belongs to no manufacturer, saved as the
    // generic printer the printer chip names.
    std::string problem;
    if (!SetupCommands::install_and_select_printer(*plater, "Custom", "Generic Klipper Printer", "0.4", "Generic PLA @System", problem))
        return problem;
    config->set(kOnboardingSection, kAssumedPrinter, Printers::add_named_printer(*plater, utf8(_L("Generic printer")), {}));
    printer_chosen();
    return {};
}

ProjectList OrcaHomeBackend::recent_projects() const
{
    if (m_autosave == nullptr)
        return {};
    const auto managed = m_autosave->projects();
    // The projects folder itself could not be read; a project in it that
    // cannot be read is left out by projects() on its own. Home says so
    // instead of showing an empty gallery, and HomeHost keeps reading the
    // folder while Home is on screen.
    if (!managed)
        return {{}, utf8(_L("Your projects couldn't be loaded. JusPrin will keep trying."))};
    // Match printing jobs by the project's display name, which is what Orca
    // uses for a job. The source file may have been deleted after import.
    std::map<std::string, MachineObject*> printing_by_stem;
    if (DeviceManager* devices = wxGetApp().getDeviceManager()) {
        for (const auto& entry : devices->get_my_machine_list()) {
            MachineObject* machine = entry.second;
            if (machine == nullptr)
                continue;
            if (machine_is_printing(*machine) && !machine->subtask_name.empty())
                printing_by_stem.emplace(machine->subtask_name, machine);
        }
    }

    ProjectList listed;
    const std::vector<LivePrint> live = live_prints();
    const bool        offers_reports      = reports_offered();
    const std::string report_offers_since = Workspace::utc_timestamp(std::chrono::system_clock::now() - std::chrono::hours(24 * kReportOfferDays));
    m_history.clear();
    for (const auto& saved : *managed) {
        ProjectEntry project;
        project.id = saved.id;
        project.name = saved.name.empty() || saved.name == utf8(_L("Untitled")) ?
            utf8(_L("Untitled project")) : saved.name;
        if (saved.gcodes) {
            project.history = summarize(*saved.gcodes, live);
            project.report_due = offers_reports && report_due(*saved.gcodes, report_offers_since, live);
            m_history.push_back({project.id, project.name, *saved.gcodes});
        }
        project.path = saved.store_path;
        project.thumbnail_url = saved.thumbnail_url;
        project.updated_at = saved.updated_at;

        if (const auto printing = printing_by_stem.find(project.name); printing != printing_by_stem.end()) {
            MachineObject& machine = *printing->second;
            project.status_kind = ProjectStatusKind::Printing;
            project.status_text = utf8(wxString::Format(_L("Printing on %s"), wxString::FromUTF8(machine.get_dev_name())) +
                                       middle_dot() + wxString::Format("%d%%", machine.mc_print_percent));
        } else if (saved.gcodes) {
            const LedgerStatus status = ledger_status(*saved.gcodes);
            project.status_kind = status.kind;
            project.status_text = utf8(ledger_text(status));
        }
        // A document this build cannot read leaves the kind Unknown: the card
        // shows no line rather than guess one.
        listed.projects.push_back(std::move(project));
    }
    return listed;
}

std::vector<PrinterEntry> OrcaHomeBackend::printers() const
{
    std::map<std::string, MachineObject*> machines;
    if (DeviceManager* devices = wxGetApp().getDeviceManager()) {
        for (const auto& entry : devices->get_my_machine_list())
            if (entry.second != nullptr)
                machines.emplace(entry.first, entry.second);
    }

    // What each connected device has loaded, read once for the whole rail.
    // A connected device with nothing loaded has an empty entry: it is the
    // printer saying so, and the card believes it over what was remembered.
    std::map<std::string, std::vector<SpoolEntry>> loaded;
    if (!machines.empty())
        for (const PrinterSetup::DiscoveredPrinter& device : PrinterSetup::discover_printers())
            if (device.connected) {
                auto& spools = loaded[device.stable_id];
                for (const PrinterSetup::DiscoveredPrinter::Spool& spool : device.spools)
                    spools.push_back(SpoolEntry{spool.material, spool.colour});
            }

    // The printers the person added, each with the device it stands for when
    // that device is here. The device's own reading of the nozzle wins: a
    // printer whose hardware was changed says so.
    std::vector<PrinterEntry> printers;
    std::set<std::string>     represented;
    const std::string         assumed = wxGetApp().app_config->get(kOnboardingSection, kAssumedPrinter);
    const std::vector<Printers::NamedPrinter> all_named = Printers::named_printers();
    // The generic printer stands in only until the person adds their own;
    // the preset stays, so files opened for it still slice.
    const bool has_own = std::any_of(all_named.begin(), all_named.end(),
                                     [&assumed](const Printers::NamedPrinter& named) { return named.name != assumed; });
    for (const Printers::NamedPrinter& named : all_named) {
        if (has_own && named.name == assumed)
            continue;
        PrinterEntry printer;
        printer.id                = kNamedPrefix + named.name;
        printer.name              = named.name;
        printer.kind              = PrinterKind::Named;
        printer.can_open_settings = true;
        printer.can_rename        = true;
        printer.can_remove        = true;
        // Until a print host, a device or an earlier connection says
        // otherwise, it has never had a connection set up.
        printer.no_connection = true;
        printer.action        = PrinterAction::Connect;
        printer.assumed           = !assumed.empty() && named.name == assumed;
        std::string model;
        // What a Moonraker host last said its multi-filament unit holds.
        std::optional<std::vector<Printers::HostLane>> host_loaded;
        if (const auto* preset = wxGetApp().preset_bundle->printers.find_preset(named.name, false, true)) {
            model = model_name(preset->config.opt_string("printer_model"));
            // A print host's address is saved only once the host has answered
            // (or typed into Orca's own settings dialog, which is not tested:
            // an accepted gap).
            const std::string host = preset->config.opt_string("print_host");
            if (!host.empty()) {
                printer.connection_label = host_label(preset->config);
                printer.address          = host_address(host);
                printer.no_connection    = false;
                // Orca's own agent announces the selected print host as a
                // device of its own, named by its address; this card is it.
                represented.insert(host);
                represented.insert(printer.address);
            }
            if (const auto status = Printers::host_status(named.name)) {
                describe_host(*status, printer);
                if (status->link == Printers::HostLink::Reachable)
                    host_loaded = status->loaded;
            }
        }
        double nozzle = named.nozzle;
        if (const auto found = machines.find(named.device_id); !named.device_id.empty() && found != machines.end()) {
            printer.no_connection = false;
            printer.action        = PrinterAction::None;
            describe_device(*found->second, printer);
            if (printer.status == PrinterStatus::NotConnected && printer.action == PrinterAction::None)
                printer.action = PrinterAction::Connect;
            if (printer.action == PrinterAction::Monitor) {
                wxGetApp().app_config->set("jusprin_verified_connections", named.device_id, "true");
                if (const double reported = reported_nozzle(*found->second); reported > 0.)
                    nozzle = reported;
            }
            represented.insert(named.device_id);
        } else if (PrinterSetup::has_verified_printer_connection(named.device_id)) {
            // Connected before, and the app has not heard of the device since.
            printer.no_connection = false;
            printer.status  = PrinterStatus::NotConnected;
            printer.message = utf8(_L("The printer stopped sending updates."));
            printer.action  = PrinterAction::Reconnect;
        }
        printer.model_text = model_text(model, nozzle);
        // Only a connected printer says what it holds. Nothing else stands
        // in for it: the project's filament is what a print needs, not what
        // is on the machine. A Moonraker host that answered lately counts:
        // its multi-filament unit's lanes, or nothing when it has none.
        if (const auto held = loaded.find(named.device_id); !named.device_id.empty() && held != loaded.end())
            printer.spools = held->second;
        else if (host_loaded && printer.status != PrinterStatus::NotConnected)
            for (const Printers::HostLane& lane : *host_loaded)
                printer.spools.push_back(SpoolEntry{lane.material, lane.colour});
        printers.push_back(std::move(printer));
    }

    // Devices no named printer stands for. Their name and binding are the
    // device's, so renaming and removing them are upstream's device dialogs,
    // offered where upstream's device list offers them.
    for (const auto& [id, machine] : machines) {
        if (represented.count(id) > 0)
            continue;
        PrinterEntry printer;
        printer.id         = kDevicePrefix + id;
        printer.name       = machine->get_dev_name();
        printer.kind       = PrinterKind::Device;
        printer.can_rename = !machine->is_lan_mode_printer() && machine->is_online();
        printer.can_remove = !machine->is_lan_mode_printer();
        describe_device(*machine, printer);
        printer.model_text = model_text(model_name({}, machine->printer_type), reported_nozzle(*machine));
        if (const auto held = loaded.find(id); held != loaded.end())
            printer.spools = held->second;
        printers.push_back(std::move(printer));
    }

    const HistoryFacts           facts = history_facts();
    const std::vector<LivePrint> live  = live_prints();
    for (PrinterEntry& printer : printers)
        printer.history = summarize_printer(m_history, printer.id, facts, live);
    return printers;
}

HistoryFacts OrcaHomeBackend::history_facts() const
{
    HistoryFacts facts;
    facts.printer_label  = [](const std::string& preset) { return utf8(printer_label(preset)); };
    facts.material_label = [](const std::string& preset) { return utf8(material_label(preset)); };
    // "0.20mm Strong" for "0.20mm Strong @BBL X1C": the printer is said once,
    // beside it.
    facts.process_label = [](const std::string& preset) { return preset.substr(0, preset.find(" @")); };

    const std::vector<Printers::NamedPrinter> named = Printers::named_printers();
    std::set<std::string>                     devices;
    if (DeviceManager* manager = wxGetApp().getDeviceManager())
        for (const auto& entry : manager->get_my_machine_list())
            devices.insert(entry.first);
    facts.card_for_preset = [named](const std::string& preset) {
        for (const Printers::NamedPrinter& printer : named)
            if (printer.name == preset)
                return kNamedPrefix + printer.name;
        return std::string();
    };
    // A Bambu device by the named printer that stands for it, else its own
    // card; a print host by its name.
    facts.card_for_printer = [named, devices](const std::string& id, const std::string& name) {
        for (const Printers::NamedPrinter& printer : named)
            if (!printer.device_id.empty() && printer.device_id == id)
                return kNamedPrefix + printer.name;
        for (const Printers::NamedPrinter& printer : named)
            if (printer.name == name)
                return kNamedPrefix + printer.name;
        return devices.count(id) != 0 ? kDevicePrefix + id : std::string();
    };
    facts.check_export = [this](const Agent::GcodeDestination& exported) { return check_export(exported); };
    facts.kept = [](const std::string& project_id, const std::string& hash) {
        const ShellController* shell = installed_shell();
        return shell != nullptr && shell->gcode_history() != nullptr && shell->gcode_history()->kept_copy_intact(project_id, hash);
    };
    facts.print_again = [](const std::string& project_id, const Agent::GcodeRecord& record) -> std::string {
        const ShellController* shell = installed_shell();
        if (shell == nullptr || shell->gcode_history() == nullptr)
            return "no_printer";
        switch (shell->gcode_history()->print_again_state(project_id, record)) {
        case GcodeHistoryRecorder::PrintAgain::Ready: return "ready";
        case GcodeHistoryRecorder::PrintAgain::NotKept: return "not_kept";
        case GcodeHistoryRecorder::PrintAgain::NotForThisPrinter: return "not_for_this_printer";
        case GcodeHistoryRecorder::PrintAgain::NoPrinter: return "no_printer";
        case GcodeHistoryRecorder::PrintAgain::Changed: break;
        }
        // The kept file changed on disk: never sent, and said on the entry.
        BOOST_LOG_TRIVIAL(error) << "The kept file of G-code " << record.hash << " in project " << project_id
                                 << " no longer holds what was kept";
        return "changed";
    };
    return facts;
}

std::string OrcaHomeBackend::check_export(const Agent::GcodeDestination& exported) const
{
    // A file that is not there, or a drive that is not plugged in, is the
    // "not found" row, not an error.
    const std::filesystem::path path = std::filesystem::u8path(exported.path);
    std::error_code             absent;
    const std::uintmax_t        size = std::filesystem::file_size(path, absent);
    const auto                  written = absent ? std::filesystem::file_time_type() : std::filesystem::last_write_time(path, absent);
    if (absent) {
        m_file_checks.erase(exported.path);
        return "missing";
    }
    FileCheck& check = m_file_checks[exported.path];
    if (check.hash.empty() || check.size != size || check.written != written)
        check = {size, written, Agent::sha256_file_hex(exported.path)};
    return check.hash == exported.file_hash ? "matches" : "changed";
}

nlohmann::json OrcaHomeBackend::print_history()
{
    // Read now: the screen opens on what the projects hold this moment.
    recent_projects();
    return history_entries(m_history, history_facts(), live_prints());
}

void OrcaHomeBackend::leave_print_history()
{
    if (ShellController* shell = installed_shell())
        shell->leave_print_history();
}

void OrcaHomeBackend::open_in_chat(const std::string& project_id, const std::string& conversation_id, const std::string& marker_id)
{
    if (ShellController* shell = installed_shell())
        shell->open_in_chat(project_id, conversation_id, marker_id);
}

void OrcaHomeBackend::print_again(const std::string& project_id, const std::string& hash)
{
    if (ShellController* shell = installed_shell())
        shell->print_again(project_id, hash);
}

void OrcaHomeBackend::export_gcode(const std::string& project_id, const std::string& hash, const std::string& beside)
{
    ShellController*      shell    = installed_shell();
    GcodeHistoryRecorder* recorder = shell != nullptr ? shell->gcode_history() : nullptr;
    if (recorder == nullptr)
        return;
    if (recorder->kept_copy(project_id, hash).empty()) {
        MessageDialog(&m_frame, _L("JusPrin no longer keeps this G-code. Slice the project again to export it."),
                      _L("Couldn't export G-code"), wxOK | wxICON_WARNING)
            .ShowModal();
        return;
    }
    std::filesystem::path target;
    if (!beside.empty()) {
        // Beside the earlier export, under a name no file has: a changed file
        // is the person's, and is never written over.
        const std::filesystem::path earlier = std::filesystem::u8path(beside);
        target = earlier;
        for (int number = 2; std::filesystem::exists(target); ++number)
            target = earlier.parent_path() / std::filesystem::u8path(earlier.stem().u8string() + " (" + std::to_string(number) + ")" +
                                                                    earlier.extension().u8string());
    } else {
        std::string name = hash.substr(0, 8);
        for (const HistoryProject& project : m_history)
            if (project.id == project_id)
                for (const Agent::GcodeRecord& record : project.records)
                    if (record.hash == hash)
                        name = Agent::gcode_display_name(record);
        if (std::filesystem::u8path(name).extension() != ".gcode")
            name += ".gcode";
        std::string chosen;
        if (m_choose_export) {
            chosen = m_choose_export(name);
        } else {
            wxFileDialog dialog(&m_frame, _L("Export G-code"), wxEmptyString, wxString::FromUTF8(name),
                                "G-code (*.gcode)|*.gcode", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dialog.ShowModal() == wxID_OK)
                chosen = utf8(dialog.GetPath());
        }
        if (chosen.empty())
            return;
        target = std::filesystem::u8path(chosen);
    }
    try {
        recorder->export_kept_copy(project_id, hash, target);
    } catch (const std::filesystem::filesystem_error& error) {
        // The folder went away or cannot be written: the person picks again.
        BOOST_LOG_TRIVIAL(warning) << "Exporting G-code " << hash << " failed: " << error.what();
        MessageDialog(&m_frame, wxString::Format(_L("The G-code couldn't be written to %s."), wxString::FromUTF8(target.u8string())),
                      _L("Couldn't export G-code"), wxOK | wxICON_ERROR)
            .ShowModal();
    }
}

bool OrcaHomeBackend::reports_offered() const
{
    return !Agent::cloud_account_token(wxGetApp().app_config).empty();
}

std::optional<PrintReportPhoto> OrcaHomeBackend::choose_report_photo()
{
    std::string path;
    if (m_choose_report_photo) {
        path = m_choose_report_photo();
    } else {
        wxFileDialog dialog(&m_frame, _L("Choose a photo of the printed object"), wxEmptyString, wxEmptyString,
                            "Photos (*.jpg;*.jpeg;*.png;*.webp)|*.jpg;*.jpeg;*.png;*.webp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
            path = utf8(dialog.GetPath());
    }
    if (path.empty())
        return std::nullopt;
    const std::filesystem::path file = std::filesystem::u8path(path);
    PrintReportPhoto            photo;
    photo.id         = std::to_string(m_report_photos.size() + 1);
    photo.name       = file.filename().u8string();
    photo.size_bytes = std::filesystem::file_size(file);
    m_report_photos[photo.id] = path;

    // Only for the form's row: a file wxImage cannot read still uploads, and
    // the server decides whether it is a photo.
    wxLogNull no_log;
    wxImage   image;
    if (image.LoadFile(wxString::FromUTF8(path))) {
        const double scale = double(kReportThumbnailPixels) / std::max(image.GetWidth(), image.GetHeight());
        image.Rescale(std::max(1, int(image.GetWidth() * scale)), std::max(1, int(image.GetHeight() * scale)), wxIMAGE_QUALITY_HIGH);
        wxMemoryOutputStream stream;
        if (image.SaveFile(stream, wxBITMAP_TYPE_PNG)) {
            wxMemoryBuffer buffer(stream.GetSize());
            stream.CopyTo(buffer.GetWriteBuf(stream.GetSize()), stream.GetSize());
            buffer.UngetWriteBuf(stream.GetSize());
            photo.thumbnail = "data:image/png;base64," + utf8(wxBase64Encode(buffer));
        }
    }
    return photo;
}

void OrcaHomeBackend::send_print_report(const PrintReportForm& form, std::function<void(const PrintReportAnswer&)> done)
{
    const Agent::GcodeRecord* record = nullptr;
    std::string               project_name;
    for (const HistoryProject& project : m_history)
        if (project.id == form.project_id)
            for (const Agent::GcodeRecord& candidate : project.records)
                if (candidate.hash == form.hash) {
                    record       = &candidate;
                    project_name = project.name;
                }
    if (record == nullptr)
        throw std::logic_error("a print report names G-code " + form.hash + ", which project " + form.project_id + " does not hold");
    const std::filesystem::path photo = std::filesystem::u8path(m_report_photos.at(form.photo_id));

    AppConfig* config  = wxGetApp().app_config;
    const auto finish  = [this, alive = std::weak_ptr<void>(m_alive), form, done = std::move(done)](unsigned status, const std::string& body) {
        PrintReportAnswer answer = print_report_answer(status, body);
        if (answer.state == PrintReportAnswer::State::Failed)
            BOOST_LOG_TRIVIAL(warning) << "Print report for G-code " << form.hash << " answered " << status;
        wxGetApp().CallAfter([this, alive, form, done, answer] {
            if (alive.expired())
                return;
            ShellController* shell = installed_shell();
            if (answer.state != PrintReportAnswer::State::Failed && shell != nullptr && shell->gcode_history() != nullptr)
                shell->gcode_history()->record_report(form.project_id, form.hash, answer.credits);
            done(answer);
        });
    };
    Http::post(Agent::cloud_print_reports_url(config))
        .header("Authorization", "Bearer " + Agent::cloud_account_token(config))
        .form_add("gcode_hash", form.hash)
        .form_add("outcome", form.outcome)
        .form_add("text", form.text)
        .form_add("facts", report_facts(project_name, *record, history_facts()).dump())
        .form_add_file("photos", boost::filesystem::path(photo.native()))
        .tls_verify(true)
        .timeout_connect(10)
        .timeout_max(kReportUploadSeconds)
        .on_complete([finish](std::string body, unsigned status) { finish(status, body); })
        .on_error([finish](std::string body, std::string, unsigned status) { finish(status, body); })
        .perform();
}

void OrcaHomeBackend::open_project(const std::string& project_id)
{
    if (project_id.empty() || m_autosave == nullptr)
        return;
    if (m_project_opening)
        m_project_opening();
    if (!m_autosave->open_managed_project(project_id)) {
        wxMessageBox(_L("The project couldn't be opened. Try again."), _L("Couldn't open project"), wxOK | wxICON_ERROR, &m_frame);
        return;
    }
    complete_onboarding();
    m_frame.select_tab(size_t(MainFrame::tp3DEditor));
}

void OrcaHomeBackend::new_project()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    // new_project saves the open project through the autosave hook first;
    // when that save fails it cancels, and Home stays where it was.
    if (plater->new_project() != wxID_CANCEL)
        m_frame.select_tab(size_t(MainFrame::tp3DEditor));
}

void OrcaHomeBackend::open_file()
{
    wxArrayString files;
    if (m_choose_files)
        files = m_choose_files();
    else
        wxGetApp().import_model(&m_frame, files);
    // Opened as a file opened from Finder or Explorer is, in a new project,
    // and Home is left for Prepare. Cancelling the dialog leaves Home as it is.
    if (!files.empty())
        open_as_new_project(files);
}

std::filesystem::path dropped_files_folder()
{
    return std::filesystem::temp_directory_path() / "JusPrin-dropped";
}

void OrcaHomeBackend::open_dropped_files(const std::vector<DroppedFile>& files)
{
    // The page gives a dropped file's bytes but not its path. Each is kept
    // under a folder named by its content, so the same file dropped again
    // has the same path, and a .3mf among them can be recognized as opened
    // before. A file opened from its own folder has a different path.
    wxArrayString paths;
    for (const DroppedFile& file : files) {
        try {
            const std::filesystem::path name = std::filesystem::u8path(file.name).filename();
            if (name.empty() || name == "." || name == "..")
                continue;
            const std::filesystem::path root     = dropped_files_folder();
            const std::filesystem::path incoming = root / boost::filesystem::unique_path().string() / name;
            std::filesystem::create_directories(incoming.parent_path());
            std::ofstream(incoming, std::ios::binary).write(file.bytes.data(), std::streamsize(file.bytes.size()));
            std::string incoming_name = incoming.u8string(), digest;
            if (!bbl_calc_md5(incoming_name, digest) || digest.empty())
                throw std::runtime_error("its digest could not be read");
            const std::filesystem::path kept = root / digest / name;
            if (!std::filesystem::exists(kept)) {
                std::filesystem::create_directories(kept.parent_path());
                std::filesystem::rename(incoming, kept);
            }
            std::filesystem::remove_all(incoming.parent_path());
            paths.Add(wxString::FromUTF8(kept.u8string()));
        } catch (const std::exception& error) {
            BOOST_LOG_TRIVIAL(error) << "Home could not keep the dropped file " << file.name << ": " << error.what();
            MessageDialog(&m_frame, wxString::Format(_L("%s couldn't be opened. Try opening it with Open… instead."),
                                                     wxString::FromUTF8(file.name)),
                          _L("Couldn't open file"), wxOK | wxICON_ERROR)
                .ShowModal();
            return;
        }
    }
    if (!paths.empty())
        open_as_new_project(paths);
}

void OrcaHomeBackend::open_as_new_project(const wxArrayString& files)
{
    if (ShellController* shell = installed_shell())
        if (shell->open_files_as_new_project(files))
            complete_onboarding();
}

void OrcaHomeBackend::launch_monitor(const std::string& printer_id)
{
    // A named printer is monitored through the device it stands for.
    std::string device_id = strip(printer_id, kDevicePrefix).value_or(std::string());
    if (const auto name = strip(printer_id, kNamedPrefix))
        for (const Printers::NamedPrinter& named : Printers::named_printers())
            if (named.name == *name)
                device_id = named.device_id;
    if (const auto name = strip(printer_id, kNamedPrefix); name && device_id.empty()) {
        open_printer_window(*name);
        return;
    }
    if (device_id.empty())
        return; // the card changed under the click; the refreshed rail says so
    open_monitor_window(device_id);
}

void OrcaHomeBackend::open_monitor_window(const std::string& device_id)
{
    // One window per printer: a second click brings it back rather than
    // opening another beside it. A window that stopped updating is left to
    // its own Close, and the printer gets a new one.
    for (const auto& window : m_monitor_windows)
        if (window && !window->closing() && !window->outdated() && window->device_id() == device_id) {
            window->bring_forward();
            return;
        }
    m_monitor_windows.erase(std::remove_if(m_monitor_windows.begin(), m_monitor_windows.end(),
                                           [](const wxWeakRef<MonitorWindow>& window) { return !window; }),
                            m_monitor_windows.end());
    m_monitor_windows.emplace_back(new MonitorWindow(device_id));
}

void OrcaHomeBackend::open_printer_window(const std::string& name)
{
    const Preset* preset = wxGetApp().preset_bundle->printers.find_preset(name, false, true);
    if (preset == nullptr || preset->config.opt_string("print_host").empty())
        return; // the card changed under the click; the refreshed rail says so
    // One window per printer: a second click brings it back rather than
    // opening another beside it, at the address saved now.
    if (auto open = m_printer_windows.find(name); open != m_printer_windows.end() && open->second && !open->second->closing()) {
        open->second->show_printer(name, preset->config);
        return;
    }
    m_printer_windows[name] = new PrinterWindow(name, preset->config);
}

OrcaHomeBackend::~OrcaHomeBackend()
{
    // The windows have no parent, so they would outlive the app's main
    // window; they go with it, as the Device tab they stand in for does.
    for (auto& [name, window] : m_printer_windows)
        if (window)
            window->close();
    for (auto& window : m_monitor_windows)
        if (window)
            window->close();
}

void OrcaHomeBackend::add_printer()
{
    // The shell shows the temporary task chat over Home and owns its session.
    if (m_open_conversation)
        m_open_conversation({}, false);
}

void OrcaHomeBackend::open_preferences()
{
    // The shell's window, once however often the gear is pressed before it
    // opens; without a shell, OrcaSlicer's own entry point.
    if (ShellController* shell = installed_shell())
        shell->request_preferences();
    else
        wxGetApp().CallAfter([] { wxGetApp().open_preferences(); });
}

std::string OrcaHomeBackend::open_printer_settings(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    // Orca's settings window, as the printer chat's manual action opens it:
    // the printer becomes the project's, then its tab opens.
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return gone();
    return utf8(Printers::open_named_printer_settings(*plater, *name));
}

std::string OrcaHomeBackend::ask_about_printer_settings(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    // That printer's temporary conversation, which also offers the settings
    // window as its manual action.
    if (m_open_conversation)
        m_open_conversation(*name, false);
    return {};
}

std::string OrcaHomeBackend::connect_printer(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    const auto saved = Printers::named_printers();
    if (std::none_of(saved.begin(), saved.end(), [&](const auto& printer) { return printer.name == *name; }))
        return gone();
    if (m_open_conversation)
        m_open_conversation(*name, true);
    return {};
}

std::string OrcaHomeBackend::calibrate_printer(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    const auto saved = Printers::named_printers();
    if (std::none_of(saved.begin(), saved.end(), [&](const auto& printer) { return printer.name == *name; }))
        return gone();
    // Its loaded spools, as the card shows them, for the person to say which
    // one the test is for.
    std::vector<std::string> spools;
    for (const PrinterEntry& printer : printers())
        if (printer.id == printer_id)
            for (const SpoolEntry& spool : printer.spools)
                spools.push_back(spool.material + (spool.colour.empty() || spool.material.empty() ? "" : " ") + spool.colour);
    if (m_start_calibration)
        m_start_calibration(*name, spools);
    return {};
}

std::string OrcaHomeBackend::rename_printer(const std::string& printer_id, const std::string& new_name)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return gone();
    if (const auto name = strip(printer_id, kNamedPrefix)) {
        const wxString problem = Printers::rename_named_printer(*plater, *name, new_name);
        // An open window goes with its printer to the new name. An empty
        // answer is also a cancelled Orca prompt, which renamed nothing.
        const auto open = m_printer_windows.find(*name);
        if (problem.empty() && open != m_printer_windows.end() &&
            wxGetApp().preset_bundle->printers.find_preset(new_name, false, true) != nullptr) {
            wxWeakRef<PrinterWindow> window = open->second;
            m_printer_windows.erase(open);
            if (window && !window->closing()) {
                window->SetTitle(wxString::FromUTF8(new_name));
                m_printer_windows[new_name] = window;
            }
        }
        return utf8(problem);
    }
    // A device keeps its own name. Upstream's dialog, as the device list
    // opens it, validates and sends the new one.
    MachineObject* machine = my_machine(strip(printer_id, kDevicePrefix).value_or(std::string()));
    if (machine == nullptr)
        return gone();
    EditDevNameDialog dialog(plater);
    dialog.set_machine_obj(machine);
    dialog.ShowModal();
    return {};
}

std::string OrcaHomeBackend::remove_printer(const std::string& printer_id)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return gone();
    if (const auto name = strip(printer_id, kNamedPrefix))
        return utf8(Printers::remove_named_printer(*plater, *name));
    // Removing a device unbinds it from the account, through upstream's
    // dialog, which confirms and reports for itself, as the device list does.
    const std::string device_id = strip(printer_id, kDevicePrefix).value_or(std::string());
    MachineObject*    machine   = my_machine(device_id);
    if (machine == nullptr)
        return gone();
    UnBindMachineDialog dialog(plater);
    dialog.update_machine_info(machine);
    if (dialog.ShowModal() == wxID_OK)
        wxGetApp().getDeviceManager()->set_selected_machine("");
    return {};
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
