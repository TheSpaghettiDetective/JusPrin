// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaPrinterBackend.hpp"

#include "PrinterDiscovery.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Printers/InstalledModels.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/JusPrin/Testing/FakeBambuAgent.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/NetworkAgentFactory.hpp"

#include <algorithm>
#include <wx/utils.h>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

double nozzle_of(const std::string& variant)
{
    try {
        return std::stod(variant);
    } catch (const std::exception&) {
        return 0.;
    }
}

} // namespace

OrcaPrinterBackend::OrcaPrinterBackend(Plater& plater, PrinterCatalog catalog, Workspace::SpoolStore* spools)
    : m_plater(plater), m_catalog(std::move(catalog)), m_spools(spools)
{
    m_models = m_catalog.panel_printers();
}

std::vector<DiscoveredPrinter> OrcaPrinterBackend::network_printers() const { return discover_printers(); }

const CatalogPrinter* OrcaPrinterBackend::model_of(const std::string& printer_name) const
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return nullptr;
    const Preset* preset = presets->printers.find_preset(printer_name, false);
    if (preset == nullptr)
        return nullptr;
    const std::string model = preset->config.opt_string("printer_model");
    const auto        found = std::find_if(m_models.begin(), m_models.end(),
                                           [&model](const CatalogPrinter& printer) { return printer.model_id == model; });
    return found == m_models.end() ? nullptr : &*found;
}

std::vector<SavedPrinter> OrcaPrinterBackend::saved_printers() const
{
    const std::vector<DiscoveredPrinter> network = discover_printers(/*include_unreachable=*/true);
    const PresetBundle*                  presets = wxGetApp().preset_bundle;

    std::vector<SavedPrinter> printers;
    for (const Printers::NamedPrinter& named : Printers::named_printers()) {
        SavedPrinter saved;
        saved.name      = named.name;
        saved.device_id = named.device_id;
        saved.nozzle    = named.nozzle;

        if (const CatalogPrinter* model = model_of(named.name)) {
            saved.model     = model->display_name();
            saved.model_id  = model->model_id;
            saved.vendor_id = model->vendor_id;
            saved.picture   = model->picture;
            saved.nozzles   = model->nozzles;
            saved.plate     = model->default_plate;
        }
        if (presets != nullptr) {
            if (const Preset* preset = presets->printers.find_preset(named.name, false)) {
                const std::string plate = preset->config.opt_string("default_bed_type");
                if (!plate.empty())
                    saved.plate = plate;
            }
        }

        // What the machine says beats what the profile assumed.
        const auto found = std::find_if(network.begin(), network.end(),
                                        [&saved](const DiscoveredPrinter& device) { return device.stable_id == saved.device_id; });
        if (found != network.end() && !saved.device_id.empty()) {
            saved.connected = found->connected;
            saved.activity  = found->activity == PrinterActivity::Printing ? "printing" :
                              found->activity == PrinterActivity::Idle     ? "idle" :
                                                                            "offline";
            saved.ams       = found->ams_name;
            for (const DiscoveredPrinter::Spool& spool : found->spools)
                saved.spools.push_back(PrinterSpool{spool.name, spool.material, spool.colour});
        } else if (m_spools != nullptr) {
            // Not connected, or never was: what this app remembers is loaded.
            for (const Workspace::Spool& spool : m_spools->spools_for(named.name))
                saved.spools.push_back(PrinterSpool{spool.name, spool.filament_preset, spool.colour});
        }
        printers.push_back(std::move(saved));
    }
    return printers;
}

std::string OrcaPrinterBackend::add_printer(const AddPrinterRequest& request, SavedPrinter& added)
{
    // Installing and selecting the printer preset marks the open project
    // dirty the same way any preset switch does (Tab::select_preset ->
    // on_presets_changed -> update_project_dirty_from_presets), even though
    // nothing about the project itself changed. Only re-baseline a project
    // that was clean before this call, the same way Plater's own new-project
    // and load-project paths already do -- a real pending edit must stay
    // visible. And only a project with nothing saved to disk yet: for one
    // loaded from a file, this switch is now that project's own printer,
    // and re-baselining it would mean closing the project never prompts to
    // save that -- a real edit, made to look like nothing happened. F9 on
    // printer-panel-review-fixes-handoff.md.
    const bool was_clean       = !m_plater.is_project_dirty();
    const bool no_project_file = m_plater.get_project_filename().IsEmpty();

    std::string error;
    if (!SetupCommands::install_and_select_printer(m_plater, request.vendor_id, request.model_id, request.variant,
                                                  request.material, error))
        return error.empty() ? std::string("This printer could not be installed.") : error;

    if (was_clean && no_project_file) {
        m_plater.reset_project_dirty_initial_presets();
        m_plater.update_project_dirty_from_presets();
    }

    const std::string name =
        Printers::add_named_printer(m_plater, request.name.empty() ? request.model_id : request.name, request.device_id);

    for (const SavedPrinter& saved : saved_printers())
        if (saved.name == name)
            added = saved;
    return {};
}

std::string OrcaPrinterBackend::change_printer(const ChangePrinterRequest& request, SavedPrinter& changed)
{
    std::vector<SavedPrinter> printers = saved_printers();
    const auto                found    = std::find_if(printers.begin(), printers.end(),
                                                      [&request](const SavedPrinter& saved) { return saved.name == request.name; });
    if (found == printers.end())
        return "This app no longer has a printer called \"" + request.name + "\".";

    if (request.nozzle) {
        const PrinterCandidate* variant = nullptr;
        for (const PrinterCandidate& candidate : m_catalog.candidates())
            if (candidate.model_id == found->model_id && nozzle_of(candidate.variant) == *request.nozzle)
                variant = &candidate;
        if (variant == nullptr)
            return found->model.empty() ? std::string("This printer does not come with that nozzle size.") :
                                          "The " + found->model + " does not come with that nozzle size.";
        const wxString problem = Printers::change_named_printer_nozzle(m_plater, request.name, variant->preset_name);
        if (!problem.empty())
            return problem.ToStdString();
    }

    if (request.spools) {
        if (m_spools == nullptr)
            return "This app cannot record spools yet.";
        for (const Workspace::Spool& spool : m_spools->spools_for(request.name))
            m_spools->remove(spool.id);
        for (const PrinterSpool& spool : *request.spools) {
            Workspace::Spool record;
            record.printer_preset  = request.name;
            record.filament_preset = spool.material;
            record.name            = spool.name;
            record.colour          = spool.colour;
            m_spools->add(std::move(record));
        }
    }

    for (const SavedPrinter& saved : saved_printers())
        if (saved.name == request.name)
            changed = saved;
    return {};
}

ManualPrinterResult OrcaPrinterBackend::run_manual_setup()
{
    // OrcaSlicer's own printer wizard, unchanged, and the printers it
    // installs are saved under their own names the way the old dialog's
    // manual path did.
    const Printers::VendorMap before = wxGetApp().app_config->vendors();
    // False when the person closed the wizard without applying it.
    if (!wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS))
        return {};
    const auto names = Printers::name_installed_printers(m_plater, before);
    ManualPrinterResult result;
    result.applied = true;
    for (const SavedPrinter& saved : saved_printers())
        if (std::find(names.begin(), names.end(), saved.name) != names.end())
            result.added.push_back(saved);
    return result;
}

void OrcaPrinterBackend::prepare_connection(const std::string& name)
{
    const CatalogPrinter* model = model_of(name);
    if (model == nullptr || model->vendor_id != "BBL")
        return;
    NetworkAgent* agent = wxGetApp().getAgent();
    const std::string fake = fake_bambu_printer_agent_id(wxGetApp().app_config);
    if (agent == nullptr || (fake.empty() && !NetworkAgent::is_network_module_loaded()))
        return;
    const std::string wanted = fake.empty() ? BBL_PRINTER_AGENT_ID : fake;
    if (!agent->get_printer_agent() || agent->get_printer_agent()->get_agent_info().id != wanted) {
        // A connection gesture selects the networking provider, not a slicing
        // preset. NetworkAgent owns callback transfer and disconnects the old
        // provider; the open project's settings and unsaved edits stay intact.
        // Orca's profile-driven policy takes the provider back the next time
        // the printer profile changes; connecting again reinstalls it.
        auto provider = NetworkAgentFactory::create_printer_agent_by_id(
            wanted, agent->get_cloud_agent(BBL_CLOUD_PROVIDER), Slic3r::data_dir());
        if (!provider)
            throw std::runtime_error("The registered Bambu printer agent could not be created");
        agent->set_printer_agent(std::move(provider));
        wxGetApp().sidebar().update_all_preset_comboboxes();
    }
    agent->start_discovery(true, false);
}

void OrcaPrinterBackend::sign_in_to_bambu()
{
    wxGetApp().request_login(false, BBL_CLOUD_PROVIDER);
}

PrinterConnectionInfo OrcaPrinterBackend::connection(const std::string& name)
{
    PrinterConnectionInfo info;
    const auto saved = saved_printers();
    const auto printer = std::find_if(saved.begin(), saved.end(), [&](const SavedPrinter& p) { return p.name == name; });
    if (printer == saved.end()) {
        info.state = "unavailable";
        info.message = "This printer was renamed or removed. Open it again from Printers.";
        return info;
    }
    const CatalogPrinter* model = model_of(name);
    if (!model || model->vendor_id != "BBL") {
        const Preset* preset = wxGetApp().preset_bundle->printers.find_preset(name, false, true);
        if (!preset)
            throw std::logic_error("A saved printer has no preset");
        const auto type = preset->config.opt_enum<PrintHostType>("host_type");
        info.address = preset->config.opt_string("print_host");
        info.host_type = type == htMoonraker ? "moonraker" : type == htOctoPrint ? "octoprint" : "other";
        info.provider = "host";
        info.state = info.address.empty() ? "not_configured" : "unknown";
        if (info.state == "unknown")
            info.message = "A connection is saved. Test it to check access; live printer status is not available here.";
        if (info.host_type == "other") {
            info.state = "unavailable";
            info.message = "Configure this connection type in Printer settings. You can still prepare prints and export a file.";
        }
        if (m_host_attempt && m_host_attempt->name == name) {
            auto& attempt = *m_host_attempt;
            if (info.address != attempt.address || info.host_type != attempt.host_type ||
                preset->config.opt_string("printhost_apikey") != attempt.api_key) {
                info.message = "Connection settings changed. Test the new connection again.";
                return info;
            }
            info.state = attempt.state;
            info.message = attempt.message;
        }
        return info;
    }
    info.provider = "bambu";
    info.device_id = printer->device_id;
    auto* devices = wxGetApp().getDeviceManager();
    auto* agent = wxGetApp().getAgent();
    info.signed_in = agent && agent->is_user_login(BBL_CLOUD_PROVIDER);
    if (!devices || !agent || (fake_bambu_printer_agent_id(wxGetApp().app_config).empty() &&
                              !NetworkAgent::is_network_module_loaded())) {
        info.state = "unavailable";
        info.message = "Install or enable the Bambu network plugin in Preferences to connect. Your printer is already available for preparing prints.";
        return info;
    }
    auto candidates = devices->get_local_machinelist();
    for (const auto& entry : devices->get_my_cloud_machine_list())
        candidates[entry.first] = entry.second;
    for (const auto& [id, machine] : candidates) {
        if (!machine || machine->printer_type != model->device_model_id)
            continue;
        if (!info.device_id.empty() && id != info.device_id)
            continue;
        const bool used_elsewhere = std::any_of(saved.begin(), saved.end(), [&](const SavedPrinter& p) {
            return p.name != name && p.device_id == id;
        });
        if (!used_elsewhere && (!machine->is_lan_mode_printer() || machine->is_avaliable()))
            info.candidates.push_back({id, machine->get_dev_name(), machine->get_dev_ip(), machine->is_lan_mode_printer()});
    }
    MachineObject* machine = info.device_id.empty() ? nullptr : devices->get_my_machine(info.device_id);
    // is_connected() alone is optimistic immediately after reset(). Require
    // actual parsed push data, and fresh LAN data for a LAN connection.
    const bool communicating = machine && has_recent_printer_data(*machine);
    const bool configured = has_verified_printer_connection(info.device_id);
    info.state = communicating ? "verified" : configured ? "unknown" : "not_configured";
    if (info.state == "unknown")
        info.message = "Connection status is unknown. Check that the printer is on, then reconnect.";
    if (m_connection_attempt && m_connection_attempt->name == name) {
        const auto& attempt = *m_connection_attempt;
        if (info.device_id != attempt.device_id)
            throw std::logic_error("A connection attempt lost its saved printer association");
        const bool observed = communicating &&
            (machine->is_lan_mode_printer() ? machine->last_lan_msg_time_ > attempt.observation_start
                                           : machine->last_cloud_msg_time_ > attempt.observation_start);
        if (devices->get_selected_machine() != machine && machine != nullptr && !attempt.reselecting) {
            info.state = "failed";
            info.message = "The selected printer changed. Select this printer again to reconnect.";
        } else if (observed) {
            info.state = "verified";
            m_connection_attempt.reset();
        } else if (machine == nullptr) {
            info.state = "failed";
            info.message = "The printer is no longer available. Refresh the printer list and try again.";
        } else if (std::chrono::steady_clock::now() - attempt.started > std::chrono::seconds(30)) {
            info.state = "failed";
            info.message = "The printer did not respond. Check that it is on the network and try again.";
        } else {
            info.state = "connecting";
            info.message.clear();
        }
    }
    if (info.state == "verified")
        wxGetApp().app_config->set("jusprin_verified_connections", info.device_id, "true");
    if (communicating) {
        const auto* extruders = machine->GetExtderSystem();
        info.nozzle_mismatch = extruders && extruders->GetNozzleDiameter(0) > 0 &&
            std::abs(extruders->GetNozzleDiameter(0) - printer->nozzle) > 0.001;
    }
    return info;
}

std::string OrcaPrinterBackend::connect_printer(const std::string& name, const std::string& device_id,
                                               const std::string& access_code)
{
    if (m_connection_attempt && connection(m_connection_attempt->name).state == "connecting")
        return m_connection_attempt->name == name && m_connection_attempt->device_id == device_id ?
            std::string() : "A connection is already in progress.";
    const auto info = connection(name);
    if (info.provider != "bambu" || info.state == "unavailable")
        return info.message;
    if (std::none_of(info.candidates.begin(), info.candidates.end(), [&](const ConnectionCandidate& p) { return p.id == device_id; }))
        return "Choose a matching printer from the refreshed list.";
    auto* devices = wxGetApp().getDeviceManager();
    MachineObject* machine = devices->get_my_machine(device_id);
    if (!machine)
        machine = devices->get_local_machine(device_id);
    if (!machine)
        return "That printer is no longer available. Refresh the list and try again.";
    if (machine->is_lan_mode_printer()) {
        if (access_code.empty() && !machine->has_access_right())
            return "Enter the access code shown on your printer.";
        if (!std::all_of(access_code.begin(), access_code.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            }))
            return "The access code can only contain letters and numbers.";
    }
    const wxString problem = Printers::link_named_printer(name, device_id);
    if (!problem.empty())
        return problem.ToStdString();
    prepare_connection(name);
    if (machine->is_lan_mode_printer() && !access_code.empty())
        machine->set_user_access_code(access_code);
    m_connection_attempt = ConnectionAttempt{name, device_id, std::chrono::steady_clock::now(),
                                              std::chrono::system_clock::now()};
    if (devices->get_selected_machine() != machine) {
        devices->set_selected_machine(device_id);
        return {};
    }
    // Selecting the selected LAN printer again disconnects and reconnects
    // it, and GUI_App's local-connect handler hears the disconnect one event
    // turn later and clears the selection -- after the reconnect. Deselect
    // now, and select it again once that notice has run.
    m_connection_attempt->reselecting = true;
    devices->set_selected_machine("");
    wxGetApp().CallAfter([alive = std::weak_ptr<bool>(m_alive), this, device_id] {
        if (alive.expired() || !m_connection_attempt || m_connection_attempt->device_id != device_id)
            return;
        m_connection_attempt->reselecting = false;
        wxGetApp().getDeviceManager()->set_selected_machine(device_id);
    });
    return {};
}

std::string OrcaPrinterBackend::connect_host(const std::string& name, const std::string& host_type,
                                            const std::string& address, const std::string& api_key)
{
    const auto info = connection(name);
    if (info.provider != "host" || info.state == "unavailable")
        return info.message;
    if (host_type != "moonraker" && host_type != "octoprint")
        return "Choose Moonraker or OctoPrint.";
    if (address.empty() || address.find_first_of("\r\n\t @") != std::string::npos)
        return "Enter the printer's host name, IP address or HTTP URL without credentials.";
    if (address.find("://") != std::string::npos && address.rfind("http://", 0) != 0 && address.rfind("https://", 0) != 0)
        return "Use an HTTP or HTTPS address.";
    const auto* preset = wxGetApp().preset_bundle->printers.find_preset(name, false, true);
    if (!preset)
        return "This printer was renamed or removed.";
    // Reuse a saved secret only for the same endpoint and provider.
    const std::string key = api_key.empty() && address == info.address && host_type == info.host_type ?
        preset->config.opt_string("printhost_apikey") : api_key;
    const auto problem = Printers::configure_named_printer_host(name, host_type == "moonraker" ? htMoonraker : htOctoPrint,
                                                               address, key);
    if (!problem.empty())
        return problem.ToStdString();
    DynamicPrintConfig config = preset->config;
    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&config));
    if (!host)
        throw std::logic_error("A supported print host has no adapter");
    // Reuse PhysicalPrinterDialog's synchronous test boundary. There is no
    // background callback that can outlive the panel or target a new selection.
    // Unexpected failures propagate through the native command boundary.
    wxBusyCursor wait;
    wxString detail;
    const bool passed = host->test(detail);
    m_host_attempt = HostAttempt{name, host_type, address, key, passed ? "verified" : "failed",
        passed ? std::string() : "Could not reach the print host. Check its address, API key and network, then try again."};
    return {};
}

void OrcaPrinterBackend::open_printer_settings(const std::string& name)
{
    Printers::open_named_printer_settings(m_plater, name);
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
