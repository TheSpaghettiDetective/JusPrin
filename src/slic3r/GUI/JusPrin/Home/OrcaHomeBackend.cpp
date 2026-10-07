// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaHomeBackend.hpp"

#include "slic3r/GUI/BindDialog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/SelectMachinePop.hpp"
#include "slic3r/GUI/JusPrin/Printers/HostLaneReader.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

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

// "2h 14m left", "14m left", or empty when the device has not estimated yet.
// The card omits the clause rather than showing a zero.
wxString remaining_text(int seconds)
{
    if (seconds <= 0)
        return wxEmptyString;
    const int minutes = seconds / 60;
    if (minutes >= 60)
        return wxString::Format(_L("%dh %dm left"), minutes / 60, minutes % 60);
    return wxString::Format(_L("%dm left"), std::max(minutes, 1));
}

// A card id says what the card stands for, so an action never has to guess
// whether a name is a printer or a device serial.
const std::string kNamedPrefix       = "named:";
const std::string kDevicePrefix      = "device:";
const char* const kOnboardingSection = "jusprin_onboarding";

bool config_true(const AppConfig* config, const char* key)
{
    if (config == nullptr)
        return false;
    const std::string value = config->get(kOnboardingSection, key);
    return value == "true" || value == "1";
}

// The two lists an import that left presets behind keeps until the user
// retries, accepts the result, or skips: the source files that were not
// copied, and the ones that were.
const char* const kImportFailures = "profile_import_failures";
const char* const kImportCopied   = "profile_import_copied";

std::vector<std::string> stored_profile_files(const AppConfig* config, const char* key)
{
    if (config == nullptr)
        return {};
    try {
        const nlohmann::json names = nlohmann::json::parse(config->get(kOnboardingSection, key));
        return names.is_array() ? names.get<std::vector<std::string>>() : std::vector<std::string>();
    } catch (const std::exception&) {
        return {};
    }
}

void save_profile_files(AppConfig* config, const char* key, const std::vector<std::string>& files)
{
    if (config != nullptr)
        config->set(kOnboardingSection, key, nlohmann::json(files).dump());
}

void clear_profile_import_result(AppConfig* config)
{
    save_profile_files(config, kImportFailures, {});
    save_profile_files(config, kImportCopied, {});
}

// "<directory>/<file>.json", the form the page receives: it names the preset
// and its kind without exposing where the user's folders are.
std::string profile_entry(const std::string& file)
{
    const boost::filesystem::path path(file);
    return (path.parent_path().filename() / path.filename()).generic_string();
}

boost::filesystem::path active_user_preset_root(const boost::filesystem::path& root, const AppConfig* config)
{
    const std::string folder = config == nullptr ? std::string() : config->get("preset_folder");
    return root / PRESET_USER_DIR / (folder.empty() ? DEFAULT_USER_FOLDER_NAME : folder);
}

int local_profile_count(const AppConfig* config)
{
    namespace fs        = boost::filesystem;
    const fs::path root = active_user_preset_root(fs::path(data_dir()), config);
    int count           = 0;
    for (const char* subdir : {PRESET_PRINTER_NAME, PRESET_FILAMENT_NAME, PRESET_PRINT_NAME}) {
        const fs::path directory = root / subdir;
        if (!fs::is_directory(directory))
            continue;
        for (fs::directory_iterator item(directory), end; item != end; ++item)
            if (fs::is_regular_file(item->path()) && boost::iequals(item->path().extension().string(), ".json"))
                ++count;
    }
    return count;
}

struct OrcaProfileSource
{
    boost::filesystem::path root;
    std::vector<std::string> files;
    std::vector<std::string> printer_names;
    std::vector<std::string> filament_names;
    std::vector<std::string> process_names;
};

OrcaProfileSource detect_orca_profiles()
{
    namespace fs = boost::filesystem;
    OrcaProfileSource source;
    source.root = fs::path(data_dir()).parent_path() / "OrcaSlicer";
    if (!fs::is_directory(source.root) || source.root == fs::path(data_dir()))
        return source;

    std::string preset_folder  = DEFAULT_USER_FOLDER_NAME;
    const fs::path config_path = source.root / "OrcaSlicer.conf";
    if (fs::is_regular_file(config_path)) {
        try {
            boost::nowide::ifstream stream(config_path.string());
            nlohmann::json config;
            stream >> config;
            const std::string configured = config.value("app", nlohmann::json::object()).value("preset_folder", std::string());
            if (!configured.empty())
                preset_folder = configured;
        } catch (const std::exception&) {
            // The source directory is still useful as an honest empty state;
            // importing from an unknown account folder would be guesswork.
            return source;
        }
    }

    const fs::path user_root = source.root / PRESET_USER_DIR / preset_folder;
    const auto scan          = [&](const char* subdir, std::vector<std::string>& names) {
        const fs::path directory = user_root / subdir;
        if (!fs::is_directory(directory))
            return;
        std::vector<std::string> files;
        for (fs::directory_iterator item(directory), end; item != end; ++item) {
            if (fs::is_regular_file(item->path()) && boost::iequals(item->path().extension().string(), ".json"))
                files.push_back(item->path().string());
        }
        std::sort(files.begin(), files.end());
        for (const std::string& file : files)
            names.push_back(fs::path(file).stem().string());
        source.files.insert(source.files.end(), files.begin(), files.end());
    };
    // Base machine profiles first, then their material and process children.
    // Stable ordering also makes a retry deterministic.
    scan(PRESET_PRINTER_NAME, source.printer_names);
    scan(PRESET_FILAMENT_NAME, source.filament_names);
    scan(PRESET_PRINT_NAME, source.process_names);
    return source;
}

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

// What the device reports about itself and its job. The connection line is
// the device's only when it has something verified to say; otherwise the
// card keeps what it had.
void describe_device(MachineObject& machine, PrinterEntry& printer)
{
    const bool printing        = machine_is_printing(machine);
    const bool connected       = PrinterSetup::has_recent_printer_data(machine);
    printer.state              = !connected ? PrinterState::Offline
                                 : printing ? PrinterState::Printing
                                            : PrinterState::Idle;
    printer.can_launch_monitor = connected;
    if (printing) {
        printer.progress_percent = machine.mc_print_percent;
        wxString status          = _L("Printing") + middle_dot() + wxString::Format("%d%%", machine.mc_print_percent);
        const wxString remaining = remaining_text(machine.mc_left_time);
        if (!remaining.empty())
            status += middle_dot() + remaining;
        printer.status_text = std::string(status.ToUTF8());
    }
    const std::string kind = machine.connection_type();
    if (connected) {
        printer.connection_state  = ConnectionState::Online;
        printer.connection_kind   = kind;
        printer.connection_text   = utf8(_L("Online") + middle_dot() + (kind == "lan" ? _L("LAN") : _L("Cloud")));
        printer.connection_action = "";
    } else if (PrinterSetup::has_verified_printer_connection(machine.get_dev_id())) {
        printer.connection_state  = ConnectionState::Offline;
        printer.connection_kind   = kind;
        printer.connection_text   = utf8(_L("Offline"));
        printer.connection_action = "reconnect";
    }
}

MachineObject* my_machine(const std::string& device_id)
{
    DeviceManager* devices = wxGetApp().getDeviceManager();
    return devices != nullptr ? devices->get_my_machine(device_id) : nullptr;
}

std::string gone() { return utf8(_L("This printer no longer exists.")); }

} // namespace

OrcaHomeBackend::OrcaHomeBackend(MainFrame& frame)
    : m_frame(frame)
{}

bool OrcaHomeBackend::dark() const { return wxGetApp().dark_mode(); }

OnboardingProgress OrcaHomeBackend::onboarding_progress() const
{
    OnboardingProgress progress;
    const AppConfig* config = wxGetApp().app_config;
    if (config == nullptr)
        return progress;
    const std::string status         = config->get(kOnboardingSection, "status");
    progress.status                  = status == "completed" ? OnboardingStatus::Completed :
                                       status == "dismissed" ? OnboardingStatus::Dismissed :
                                                               OnboardingStatus::Unfinished;
    progress.started                 = config_true(config, "started");
    progress.profile_import_deferred = config_true(config, "profile_import_deferred");
    progress.partial_import_pending  = config_true(config, "partial_import_pending");
    progress.setup_confirmed         = config_true(config, "setup_confirmed");
    progress.offline_example         = config_true(config, "offline_example");
    return progress;
}

bool OrcaHomeBackend::has_usable_setup() const
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    const AppConfig* config     = wxGetApp().app_config;
    if (presets == nullptr || config == nullptr || presets->filament_presets.empty())
        return false;

    // PresetBundle deliberately chooses a compatible fallback when the saved
    // selection is empty or "Default Printer". That keeps Prepare usable, but
    // it must not silently satisfy onboarding's physical-setup gate.
    const std::string selected_name = config->get("presets", PRESET_PRINTER_NAME);
    const Preset* printer           = presets->printers.find_preset(selected_name);
    if (selected_name.empty() || printer == nullptr)
        return false;
    const auto* nozzles = printer->config.option<ConfigOptionFloats>("nozzle_diameter");
    return !printer->is_default && printer->is_visible && nozzles != nullptr && !nozzles->values.empty() && nozzles->values.front() > 0. &&
           presets->filaments.find_preset(presets->filament_presets.front()) != nullptr;
}

OnboardingFacts OrcaHomeBackend::onboarding_facts() const
{
    OnboardingFacts facts;
    facts.profiles_available = local_profile_count(wxGetApp().app_config) > 0;
    facts.usable_setup       = has_usable_setup();
    if (const Plater* plater = wxGetApp().plater())
        facts.project_open = !plater->model().objects.empty();
    return facts;
}

void OrcaHomeBackend::save_onboarding(const OnboardingProgress& progress)
{
    AppConfig* config = wxGetApp().app_config;
    if (config == nullptr)
        return;
    config->set(kOnboardingSection, "status", to_string(progress.status));
    config->set(kOnboardingSection, "started", progress.started ? "true" : "false");
    config->set(kOnboardingSection, "profile_import_deferred", progress.profile_import_deferred ? "true" : "false");
    config->set(kOnboardingSection, "partial_import_pending", progress.partial_import_pending ? "true" : "false");
    config->set(kOnboardingSection, "setup_confirmed", progress.setup_confirmed ? "true" : "false");
    config->set(kOnboardingSection, "offline_example", progress.offline_example ? "true" : "false");
    config->save();
}

void OrcaHomeBackend::complete_onboarding()
{
    OnboardingProgress progress = onboarding_progress();
    if (progress.status != OnboardingStatus::Unfinished)
        return;
    progress.status  = OnboardingStatus::Completed;
    progress.started = true;
    save_onboarding(progress);
}

Snapshot::Onboarding OrcaHomeBackend::onboarding()
{
    Snapshot::Onboarding state;
    state.progress = onboarding_progress();
    state.facts    = onboarding_facts();
    state.step     = next_onboarding_step(state.progress, state.facts);
    if (state.step == OnboardingStep::Hidden && state.progress.status == OnboardingStatus::Unfinished && state.progress.started) {
        state.progress.status = OnboardingStatus::Completed;
        save_onboarding(state.progress);
    }

    if (const PresetBundle* presets = wxGetApp().preset_bundle) {
        if (state.facts.usable_setup) {
            const auto printer  = SetupCommands::current_printer();
            const auto filament = SetupCommands::current_filament();
            state.setup_summary = std::string(printer.nickname.ToUTF8());
            state.setup_printer = std::string(printer.nickname.ToUTF8());
            if (printer.nozzle > 0.)
                state.setup_summary += " · " + (state.setup_nozzle = std::string(wxString::Format("%g mm", printer.nozzle).ToUTF8()));
            if (filament.valid && !filament.material.empty()) {
                state.setup_material = std::string(filament.material.ToUTF8());
                state.setup_summary += " · " + std::string(filament.material.ToUTF8());
            }
            for (const SetupCommands::BedTypeChoice& choice : SetupCommands::bed_types())
                if (choice.current) {
                    state.setup_plate = std::string(choice.label.ToUTF8());
                    break;
                }
            state.setup_process = presets->prints.get_edited_preset().label(false);
        }
    }
    // Only the import step shows the Orca source, so only that step pays for
    // reading its folders: this runs on every Home refresh.
    if (state.step == OnboardingStep::Profiles) {
        OrcaProfileSource source     = detect_orca_profiles();
        state.printer_profiles       = int(source.printer_names.size());
        state.filament_profiles      = int(source.filament_names.size());
        state.process_profiles       = int(source.process_names.size());
        state.profile_source         = source.root.string();
        state.printer_profile_names  = std::move(source.printer_names);
        state.filament_profile_names = std::move(source.filament_names);
        state.process_profile_names  = std::move(source.process_names);
    }
    if (state.progress.partial_import_pending) {
        for (const std::string& file : stored_profile_files(wxGetApp().app_config, kImportFailures))
            state.failed_profiles.push_back(profile_entry(file));
        for (const std::string& file : stored_profile_files(wxGetApp().app_config, kImportCopied))
            state.imported_profiles.push_back(profile_entry(file));
    }
    if (const AppConfig* config = wxGetApp().app_config)
        state.agent_configured = config->get("jusprin_agent", "enabled") == "true";
    return state;
}

void OrcaHomeBackend::confirm_onboarding_setup()
{
    OnboardingProgress progress = onboarding_progress();
    progress.started            = true;
    progress.setup_confirmed    = true;
    save_onboarding(progress);
}

void OrcaHomeBackend::back_onboarding()
{
    OnboardingProgress progress = onboarding_progress();
    const OnboardingStep step   = next_onboarding_step(progress, onboarding_facts());
    if (step == OnboardingStep::Profiles)
        progress.started = false;
    else if (step == OnboardingStep::Setup)
        progress.profile_import_deferred = false;
    else if (step == OnboardingStep::Project) {
        progress.setup_confirmed = false;
        progress.offline_example = false;
    }
    save_onboarding(progress);
}

void OrcaHomeBackend::choose_profile_bundle() { m_frame.load_config_file(); }

void OrcaHomeBackend::choose_profile_folder() { m_frame.load_config_file(); }

void OrcaHomeBackend::begin_onboarding()
{
    OnboardingProgress progress = onboarding_progress();
    progress.started            = true;
    save_onboarding(progress);
}

void OrcaHomeBackend::dismiss_onboarding()
{
    OnboardingProgress progress = onboarding_progress();
    progress.status             = OnboardingStatus::Dismissed;
    save_onboarding(progress);
}

void OrcaHomeBackend::defer_profile_import()
{
    OnboardingProgress progress      = onboarding_progress();
    progress.started                 = true;
    progress.profile_import_deferred = true;
    progress.partial_import_pending  = false;
    clear_profile_import_result(wxGetApp().app_config);
    save_onboarding(progress);
}

void OrcaHomeBackend::accept_partial_profile_import()
{
    OnboardingProgress progress      = onboarding_progress();
    progress.started                 = true;
    progress.profile_import_deferred = true;
    progress.partial_import_pending  = false;
    clear_profile_import_result(wxGetApp().app_config);
    save_onboarding(progress);
}

std::string OrcaHomeBackend::use_detected_profiles(const Snapshot::ProfileSelection& selection)
{
    OrcaProfileSource source = detect_orca_profiles();
    if (source.files.empty())
        return "No Orca profiles were detected. Continue with bundled profiles or set up a printer manually.";
    PresetBundle* presets = wxGetApp().preset_bundle;
    AppConfig* config     = wxGetApp().app_config;
    if (presets == nullptr || config == nullptr)
        return "The local profile store is unavailable.";

    const OnboardingProgress prior = onboarding_progress();
    if (prior.partial_import_pending) {
        const std::vector<std::string> failed = stored_profile_files(config, kImportFailures);
        const std::set<std::string> retry(failed.begin(), failed.end());
        source.files.erase(std::remove_if(source.files.begin(), source.files.end(),
                                          [&](const std::string& file) { return retry.count(file) == 0; }),
                           source.files.end());
    } else {
        // A first import copies only the kinds the user left ticked.
        const auto left_out = [&](const std::string& file) {
            const std::string kind = boost::filesystem::path(file).parent_path().filename().string();
            return (kind == PRESET_PRINTER_NAME && !selection.printers) || (kind == PRESET_FILAMENT_NAME && !selection.filaments) ||
                   (kind == PRESET_PRINT_NAME && !selection.processes);
        };
        source.files.erase(std::remove_if(source.files.begin(), source.files.end(), left_out), source.files.end());
        if (source.files.empty())
            return "Choose at least one kind of preset to import.";
    }
    const std::vector<std::string> requested = source.files;
    presets->import_presets(
        source.files,
        [this](const std::string& name) {
            const int answer = wxMessageBox(wxString::Format(_L("A profile named %s already exists. Replace it with the Orca profile?"),
                                                             wxString::FromUTF8(name)),
                                            _L("Profile conflict"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, &m_frame);
            return answer == wxID_YES ? 1 : 0;
        },
        ForwardCompatibilitySubstitutionRule::Enable, *config);
    wxGetApp().load_current_presets();
    presets->update_compatible(PresetSelectCompatibleType::Always);
    m_frame.update_side_preset_ui();

    if (source.files.empty()) {
        OnboardingProgress progress     = onboarding_progress();
        progress.partial_import_pending = false;
        clear_profile_import_result(config);
        save_onboarding(progress);
        return "The detected Orca profiles could not be imported. Your Orca installation was not changed.";
    }

    std::set<std::string> imported(source.files.begin(), source.files.end());
    std::vector<std::string> failures;
    for (const std::string& file : requested)
        if (imported.count(file) == 0)
            failures.push_back(file);

    // A retry adds to what an earlier attempt already copied.
    std::vector<std::string> copied = prior.partial_import_pending ? stored_profile_files(config, kImportCopied) : std::vector<std::string>();
    copied.insert(copied.end(), source.files.begin(), source.files.end());

    OnboardingProgress progress     = onboarding_progress();
    progress.partial_import_pending = !failures.empty();
    save_profile_files(config, kImportFailures, failures);
    save_profile_files(config, kImportCopied, failures.empty() ? std::vector<std::string>() : copied);
    save_onboarding(progress);
    return {};
}

std::string OrcaHomeBackend::run_manual_setup()
{
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
    return has_usable_setup() ? std::string() : "No printer setup was saved. Choose a printer or use the offline example.";
}

std::string OrcaHomeBackend::choose_offline_example()
{
    Plater* plater        = wxGetApp().plater();
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (plater == nullptr || presets == nullptr)
        return "The local printer profiles are unavailable.";
    if (!has_usable_setup()) {
        const auto candidate = std::find_if(presets->printers.begin(), presets->printers.end(),
                                            [](const Preset& preset) { return preset.is_visible && !preset.is_default; });
        if (candidate == presets->printers.end() || !SetupCommands::select_printer_preset(*plater, candidate->name))
            return "No local printer profile is installed. Set up a printer manually first.";
    }
    OnboardingProgress progress = onboarding_progress();
    progress.started            = true;
    progress.offline_example    = true;
    save_onboarding(progress);
    return {};
}

void OrcaHomeBackend::open_account_stub() { wxLaunchDefaultBrowser("https://jusprin.com/account"); }

void OrcaHomeBackend::open_terms_stub() { wxLaunchDefaultBrowser("https://jusprin.com/terms"); }

void OrcaHomeBackend::open_privacy_stub() { wxLaunchDefaultBrowser("https://jusprin.com/privacy"); }

std::string OrcaHomeBackend::open_onboarding_example()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return "The project workspace is unavailable.";
    // Checked before anything is replaced: a missing example leaves the open
    // project, and the screen the user asked from, exactly as they were.
    const boost::filesystem::path example = boost::filesystem::path(resources_dir()) / "handy_models" / "OrcaSliced.3mf";
    if (!boost::filesystem::exists(example))
        return "The bundled example could not be opened.";
    if (plater->new_project() == wxID_CANCEL)
        return {};
    if (plater->load_files(std::vector<boost::filesystem::path>{example}, LoadStrategy::LoadModel).empty()) {
        // Starting the new project moved to Prepare. The failure is reported
        // on Home, so that is where the user has to be to read it.
        m_frame.select_tab(size_t(MainFrame::tpHome));
        return "The bundled example could not be opened.";
    }
    complete_onboarding();
    m_frame.select_tab(size_t(MainFrame::tp3DEditor));
    return {};
}

std::vector<ProjectEntry> OrcaHomeBackend::recent_projects() const
{
    if (m_autosave == nullptr)
        return {};
    // Match printing jobs by the project's display name, which is what Orca
    // uses for a job. The source file may have been deleted after import.
    std::map<std::string, std::string> printing_by_stem;
    if (DeviceManager* devices = wxGetApp().getDeviceManager()) {
        for (const auto& entry : devices->get_my_machine_list()) {
            MachineObject* machine = entry.second;
            if (machine == nullptr)
                continue;
            if (machine_is_printing(*machine) && !machine->subtask_name.empty())
                printing_by_stem.emplace(machine->subtask_name, machine->get_dev_name());
        }
    }

    std::vector<ProjectEntry> projects;
    for (const auto& saved : m_autosave->projects()) {
        ProjectEntry project;
        project.id            = saved.id;
        project.name          = saved.name.empty() || saved.name == utf8(_L("Untitled")) ? utf8(_L("Untitled project")) : saved.name;
        project.path          = saved.store_path;
        project.thumbnail_url = saved.thumbnail_url;

        const auto printing = printing_by_stem.find(project.name);
        if (printing != printing_by_stem.end()) {
            project.status_kind = ProjectStatusKind::Printing;
            project.status_text = std::string(wxString::Format(_L("Printing on %s"), wxString::FromUTF8(printing->second)).ToUTF8());
        } else {
            project.status_kind = ProjectStatusKind::Unknown;
            project.status_text = utf8(_L("Saved"));
        }
        projects.push_back(std::move(project));
    }
    return projects;
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

    // Every card says something about its connection; one with nothing
    // verified says so in words, and draws no dot.
    const auto settle_connection = [](PrinterEntry& printer) {
        if (printer.connection_state != ConnectionState::None || !printer.connection_text.empty())
            return;
        printer.connection_text   = utf8(_L("Not connected"));
        printer.connection_kind   = "";
        printer.connection_action = "";
    };

    // The printers the person added, each with the device it stands for when
    // that device is here. The device's own reading of the nozzle wins: a
    // printer whose hardware was changed says so.
    std::vector<PrinterEntry> printers;
    std::set<std::string> represented;
    for (const Printers::NamedPrinter& named : Printers::named_printers()) {
        PrinterEntry printer;
        printer.id                = kNamedPrefix + named.name;
        printer.name              = named.name;
        printer.kind              = PrinterKind::Named;
        printer.can_open_settings = true;
        printer.can_rename        = true;
        printer.can_remove        = true;
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
                printer.connection_state   = ConnectionState::Connected;
                printer.connection_kind    = "host";
                printer.address            = host_address(host);
                printer.connection_text    = utf8(_L("Connected") + middle_dot() + wxString::FromUTF8(printer.address));
                printer.can_launch_monitor = true;
            }
            // A Moonraker host is read, so the line says whether it answers
            // now. Other hosts are not, and keep the saved address's word.
            if (const auto status = Printers::host_status(named.name)) {
                if (status->link == Printers::HostLink::Reachable) {
                    host_loaded = status->loaded;
                } else if (status->link == Printers::HostLink::Unreachable) {
                    printer.connection_state   = ConnectionState::Offline;
                    printer.connection_text    = utf8(_L("Offline"));
                    printer.connection_action  = "reconnect";
                    printer.can_launch_monitor = false;
                } else {
                    // Asked, and not answered yet: no dot until it has.
                    printer.connection_state   = ConnectionState::None;
                    printer.connection_text    = utf8(_L("Checking connection") + wxString::FromUTF8("\xE2\x80\xA6"));
                    printer.can_launch_monitor = false;
                }
            }
        }
        double nozzle = named.nozzle;
        if (const auto found = machines.find(named.device_id); !named.device_id.empty() && found != machines.end()) {
            describe_device(*found->second, printer);
            if (printer.can_launch_monitor) {
                wxGetApp().app_config->set("jusprin_verified_connections", named.device_id, "true");
                if (const double reported = reported_nozzle(*found->second); reported > 0.)
                    nozzle = reported;
            }
            represented.insert(named.device_id);
        } else if (PrinterSetup::has_verified_printer_connection(named.device_id)) {
            // Connected before, and the app has not heard of the device since.
            printer.connection_state  = ConnectionState::Offline;
            printer.connection_text   = utf8(_L("Offline"));
            printer.connection_action = "reconnect";
        }
        settle_connection(printer);
        printer.model_text = model_text(model, nozzle);
        // Only a connected printer says what it holds. Nothing else stands
        // in for it: the project's filament is what a print needs, not what
        // is on the machine. A Moonraker host that answered lately counts:
        // its multi-filament unit's lanes, or nothing when it has none.
        if (const auto held = loaded.find(named.device_id); !named.device_id.empty() && held != loaded.end())
            printer.spools = held->second;
        else if (host_loaded)
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
        settle_connection(printer);
        printer.model_text = model_text(model_name({}, machine->printer_type), reported_nozzle(*machine));
        if (const auto held = loaded.find(id); held != loaded.end())
            printer.spools = held->second;
        printers.push_back(std::move(printer));
    }
    return printers;
}

void OrcaHomeBackend::open_project(const std::string& project_id)
{
    if (project_id.empty() || m_autosave == nullptr)
        return;
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
    // Cancelling the unsaved-changes question leaves the old project in place,
    // so it leaves Home in place too.
    if (plater->new_project() != wxID_CANCEL) {
        m_frame.select_tab(size_t(MainFrame::tp3DEditor));
    }
}

void OrcaHomeBackend::import_model()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    // add_model imports into the project that is open and runs its own modal
    // file dialog. It returns void, so the object count before and after is
    // what says whether anything arrived: cancelling the dialog leaves Home
    // where it was, as cancelling New does.
    const size_t before = plater->model().objects.size();
    plater->add_model();
    if (plater->model().objects.size() != before) {
        complete_onboarding();
        m_frame.select_tab(size_t(MainFrame::tp3DEditor));
    }
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
    // Upstream's own path to the Monitor, as the device popup and the
    // multi-machine page use. Selecting the tab and the machine by hand
    // instead left Home on screen: jump_to_monitor also guards on the Monitor
    // panel existing and selects the machine inside it. Upstream may still
    // decline the switch -- it vetoes the Monitor tab when the network plugin
    // is missing -- and that refusal is upstream's to make.
    m_frame.jump_to_monitor(device_id);
}

void OrcaHomeBackend::open_printer_window(const std::string& name)
{
    const Preset* preset = wxGetApp().preset_bundle->printers.find_preset(name, false, true);
    if (preset == nullptr || preset->config.opt_string("print_host").empty())
        return; // the card changed under the click; the refreshed rail says so
    // One window per printer: a second click brings it back rather than
    // opening another beside it.
    if (auto open = m_printer_windows.find(name); open != m_printer_windows.end() && open->second && !open->second->closing()) {
        open->second->Iconize(false);
        open->second->Raise();
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
}

void OrcaHomeBackend::add_printer()
{
    // The shell shows the temporary task chat over Home and owns its session.
    if (m_open_conversation)
        m_open_conversation({}, false);
}

std::string OrcaHomeBackend::open_printer_settings(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    // "Printer settings…" opens that printer's temporary conversation, with
    // Orca's own settings window available from its manual action.
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

std::string OrcaHomeBackend::rename_printer(const std::string& printer_id, const std::string& new_name)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return gone();
    if (const auto name = strip(printer_id, kNamedPrefix))
        return utf8(Printers::rename_named_printer(*plater, *name, new_name));
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
    MachineObject* machine      = my_machine(device_id);
    if (machine == nullptr)
        return gone();
    UnBindMachineDialog dialog(plater);
    dialog.update_machine_info(machine);
    if (dialog.ShowModal() == wxID_OK)
        wxGetApp().getDeviceManager()->set_selected_machine("");
    return {};
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
