// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaPrinterBackend.hpp"

#include "PrinterDiscovery.hpp"
#include "SetupOptionsLoader.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Printers/ConnectionEvents.hpp"
#include "slic3r/GUI/JusPrin/Printers/HostLaneReader.hpp"
#include "slic3r/GUI/JusPrin/Printers/InstalledModels.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Printers/PresetLookup.hpp"
#include "slic3r/GUI/JusPrin/Printers/PrinterNames.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/JusPrin/Testing/FakeBambuAgent.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Moonraker.hpp"
#include "slic3r/Utils/OctoPrint.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/Utils/NetworkAgentFactory.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <wx/utils.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

// The port a Bambu Lab printer in LAN mode takes its login on, and how long the
// app waits for it to answer after a failed connection.
constexpr int                       kBambuLanPort = 8883;
constexpr std::chrono::milliseconds kPortLookLimit{5000};

// What the plug-in did, as observed. -1 came with a wrong code and with the
// right one on a slow network, so it is not read as either.
std::string connection_failure_text(const Printers::ConnectionEvent& answer, std::chrono::steady_clock::time_point asked)
{
    if (answer.kind == Printers::ConnectionEvent::Kind::Lost)
        return "The connection to the printer was lost before it answered.";
    if (answer.code == "5")
        return "The printer refused the access code. The app has dropped the code it had saved for this printer; the person "
               "gives the code shown on the printer's screen again to try again.";
    const auto took = std::chrono::duration_cast<std::chrono::seconds>(answer.at - asked).count();
    return "Bambu's network plug-in reported code " + answer.code + " " + std::to_string(took) +
           " seconds after being asked to connect, which does not say why.";
}

// A host's answer as one bounded line: a response body can be a page.
std::string one_line(std::string text)
{
    for (char& c : text)
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    std::string out;
    for (char c : text)
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out += c;
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    constexpr std::size_t kLimit = 240;
    if (out.size() > kLimit)
        out = out.substr(0, kLimit) + "...";
    return out;
}

// Records a failed request as the host or curl gave it.
void record_error(HostAnswer& answer, const std::string& body, const std::string& error, unsigned status)
{
    answer.passed = false;
    answer.status = status;
    answer.said   = status != 0 ? "HTTP " + std::to_string(status) + ": " + one_line(body) : one_line(error);
}

// OrcaSlicer's Moonraker and OctoPrint tests, each copied with two changes.
// The request gives up after kConnectionWait: upstream's has no upper limit
// (Http's DEFAULT_TIMEOUT_MAX is 0), so a host that accepts the connection and
// never replies would hold the test, and the chat's tool call, for good. And
// the HTTP status and the host's own words come back, where upstream returns
// a message that puts a cause on curl's error and only logs Klipper's state.
// The requests and checks are upstream's: when Moonraker::test or
// OctoPrint::test changes, change these.
class MoonrakerTest : public Moonraker
{
public:
    using Moonraker::Moonraker;

    HostAnswer answer() const
    {
        HostAnswer answer{true};
        auto       http = Http::get(make_url("server/info"));
        set_auth(http);
        http.timeout_max(kConnectionWait.count())
            .on_error([&](std::string body, std::string error, unsigned status) { record_error(answer, body, error, status); })
            .on_complete([&](std::string body, unsigned) {
                try {
                    std::stringstream           stream(body);
                    boost::property_tree::ptree tree;
                    boost::property_tree::read_json(stream, tree);
                    const auto klippy_state = tree.get_optional<std::string>("result.klippy_state");
                    if (!klippy_state) {
                        answer = {false, 0, "The host responded but it doesn't look like Moonraker (missing result.klippy_state)."};
                        return;
                    }
                    answer.said = *klippy_state;
                } catch (const std::exception& ex) {
                    answer = {false, 0, std::string("Could not parse Moonraker server response: ") + ex.what()};
                }
            })
#ifdef WIN32
            .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
            .perform_sync();
        return answer;
    }
};

class OctoPrintTest : public OctoPrint
{
public:
    using OctoPrint::OctoPrint;

    HostAnswer answer() const
    {
        HostAnswer answer{true};
        auto       http = Http::get(make_url("api/version"));
        set_auth(http);
        http.timeout_max(kConnectionWait.count())
            .on_error([&](std::string body, std::string error, unsigned status) { record_error(answer, body, error, status); })
            .on_complete([&](std::string body, unsigned) {
                try {
                    std::stringstream           stream(body);
                    boost::property_tree::ptree tree;
                    boost::property_tree::read_json(stream, tree);
                    if (!tree.get_optional<std::string>("api")) {
                        answer = {false, 0, "The host responded but its answer has no OctoPrint API version."};
                        return;
                    }
                    const auto text = tree.get_optional<std::string>("text");
                    if (!validate_version_text(text))
                        answer = {false, 0, "Mismatched type of print host: " + (text ? *text : std::string(get_name()))};
                } catch (const std::exception&) {
                    answer = {false, 0, "Could not parse server response."};
                }
            })
#ifdef WIN32
            .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
            .perform_sync();
        return answer;
    }
};

} // namespace

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

OrcaPrinterBackend::OrcaPrinterBackend(Plater& plater, PrinterCatalog catalog)
    : m_plater(plater), m_catalog(std::move(catalog)), m_profiles_directory(Slic3r::resources_dir() + "/profiles")
{
    m_models = m_catalog.panel_printers();
}

OrcaPrinterBackend::~OrcaPrinterBackend() = default;

std::vector<DiscoveredPrinter> OrcaPrinterBackend::network_printers() const { return discover_printers(); }

std::vector<DiscoveredPrinter> OrcaPrinterBackend::nearby_printers() const { return discover_nearby_printers(); }

SetupOptions OrcaPrinterBackend::setup_options(const std::string& catalog_id, double nozzle) const
{
    // The catalogue and the options read the same files, so a model and size
    // the catalogue offered has a machine profile there.
    for (const PrinterCandidate& candidate : m_catalog.candidates())
        if (candidate.vendor_id + "/" + candidate.model_id == catalog_id && nozzle_of(candidate.variant) == nozzle) {
            SetupOptions options = load_setup_options(m_profiles_directory, candidate.vendor_id, candidate.preset_name, m_own_filaments);
            options.catalog_id   = catalog_id;
            options.nozzle       = nozzle;
            return options;
        }
    throw std::logic_error("The printer form asked about a printer the catalogue does not offer: " + catalog_id);
}

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
        // A Moonraker host that answered lately is connected, and says what
        // its multi-filament unit holds -- nothing, when it has none.
        const auto host = Printers::host_status(named.name);
        const bool host_answered = host && host->link == Printers::HostLink::Reachable;
        if (host_answered)
            saved.connected = true;
        if (found != network.end() && !saved.device_id.empty()) {
            saved.connected = found->connected;
            saved.activity  = found->activity == PrinterActivity::Printing ? "printing" :
                              found->activity == PrinterActivity::Idle     ? "idle" :
                                                                            "offline";
            saved.ams       = found->ams_name;
            for (const DiscoveredPrinter::Spool& spool : found->spools)
                saved.spools.push_back(PrinterSpool{spool.name, spool.material, spool.colour});
        } else if (host_answered && host->loaded) {
            // The unit names a material, not a product.
            for (const Printers::HostLane& lane : *host->loaded)
                saved.spools.push_back(PrinterSpool{lane.material, lane.material, lane.colour});
        }
        // Not connected, or never was: nothing is known to be loaded, and
        // nothing is claimed.
        printers.push_back(std::move(saved));
    }
    return printers;
}

SavedPrinter OrcaPrinterBackend::stock_printer(const std::string& name) const
{
    SavedPrinter stock;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    const Preset*       preset  = presets != nullptr ? Printers::preset_by_name(*presets, Preset::TYPE_PRINTER, name) : nullptr;
    if (preset == nullptr || preset->name != name || !preset->is_system)
        return stock;
    stock.name      = preset->name;
    stock.stock     = true;
    stock.copy_name = Printers::copy_name(name, [presets](const std::string& candidate) {
        return std::any_of(presets->printers.begin(), presets->printers.end(),
                           [&candidate](const Preset& preset) { return boost::iequals(preset.name, candidate); });
    });
    if (const auto* nozzle = preset->config.option<ConfigOptionFloats>("nozzle_diameter"); nozzle && !nozzle->values.empty())
        stock.nozzle = nozzle->values.front();
    if (const CatalogPrinter* model = model_of(name)) {
        stock.model     = model->display_name();
        stock.model_id  = model->model_id;
        stock.vendor_id = model->vendor_id;
        stock.picture   = model->picture;
        stock.nozzles   = model->nozzles;
        stock.plate     = model->default_plate;
    }
    return stock;
}

void OrcaPrinterBackend::prepare_setup_options()
{
    m_own_filaments.clear();
    for (const Preset& preset : wxGetApp().preset_bundle->filaments)
        if (preset.is_user())
            m_own_filaments.push_back(preset);
}

std::string OrcaPrinterBackend::start_with_filament(const std::string& printer, const std::string& filament, bool copy,
                                                    std::string& used)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets->printers.get_selected_preset_name() != printer)
        throw std::logic_error("A filament was chosen for " + printer + ", which is not the selected printer");
    const Preset* source  = presets->filaments.find_preset(filament, false);
    if (source == nullptr || source->name != filament)
        return "The filament " + filament + " is no longer there.";
    std::string chosen = filament;
    if (copy) {
        // OrcaSlicer's own "Add preset for new printer"
        // (CreatePresetForPrinterDialog): a copy named "<name> @<printer>",
        // made for that printer alone, so a printer change keeps it.
        const std::string name = source->alias.empty() ? source->name : source->alias;
        chosen                 = name + " @" + printer;
        if (const Preset* existing = presets->filaments.find_preset(chosen, false); existing == nullptr || existing->name != chosen) {
            const auto first = [source](const char* key) {
                const auto* values = source->config.option<ConfigOptionStrings>(key);
                return values != nullptr && !values->values.empty() ? values->values.front() : std::string();
            };
            DynamicConfig made_for;
            made_for.set_key_value("filament_vendor", new ConfigOptionStrings({first("filament_vendor")}));
            made_for.set_key_value("compatible_printers", new ConfigOptionStrings({printer}));
            made_for.set_key_value("filament_type", new ConfigOptionStrings({first("filament_type")}));
            std::vector<std::string> failures;
            if (!presets->filaments.clone_presets_for_filament(source, failures, name, source->filament_id, made_for, printer))
                return "A copy of " + name + " for " + printer + " could not be saved" + (failures.empty() ? "." : ": a preset named " + failures.front() + " already exists.");
            if (wxGetApp().mainframe)
                wxGetApp().mainframe->update_side_preset_ui();
        }
    }
    if (!SetupCommands::select_filament_preset(m_plater, 0, chosen))
        return "The filament " + chosen + " could not be put in slot 1.";
    used = chosen;
    return {};
}

std::string OrcaPrinterBackend::create_filament_preset(const std::string& printer)
{
    PresetBundle*         presets = wxGetApp().preset_bundle;
    if (presets->printers.get_selected_preset_name() != printer)
        throw std::logic_error("A filament preset was to be made for " + printer + ", which is not the selected printer");
    std::set<std::string> before;
    for (const Preset& preset : presets->filaments)
        if (preset.is_user())
            before.insert(preset.name);
    // OrcaSlicer's own handler, as its setup guide reaches it: its Create
    // filament window, and its note when a preset was made.
    SimpleEvent create(EVT_CREATE_FILAMENT);
    m_plater.ProcessWindowEvent(create);
    const PresetWithVendorProfile selected = presets->printers.get_edited_preset_with_vendor_profile();
    for (const Preset& preset : presets->filaments)
        if (preset.is_user() && before.count(preset.name) == 0 &&
            is_compatible_with_printer(presets->filaments.get_preset_with_vendor_profile(preset), selected)) {
            const std::string name = preset.name;
            if (!SetupCommands::select_filament_preset(m_plater, 0, name))
                return {};
            return name;
        }
    return {};
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
    // With the new printer selected, the plate goes through the sidebar's
    // own selection, which records it as this printer's plate
    // (app_config printer_settings), as a pick in the sidebar would.
    if (request.plate && !SetupCommands::select_bed_type(m_plater, *request.plate))
        throw std::logic_error("The printer form offered a plate OrcaSlicer does not offer for " + name);

    for (const SavedPrinter& saved : saved_printers())
        if (saved.name == name)
            added = saved;
    return {};
}

std::string OrcaPrinterBackend::change_printer(const ChangePrinterRequest& request, SavedPrinter& changed,
                                               std::vector<std::string>& discarded)
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
            return found->model.empty() ? std::string("The app has no preset of this printer with that nozzle size.") :
                                          "The app has no " + found->model + " preset with that nozzle size.";
        const wxString problem = Printers::change_named_printer_nozzle(m_plater, request.name, variant->preset_name, discarded);
        if (!problem.empty())
            return problem.ToStdString();
    }

    for (const SavedPrinter& saved : saved_printers())
        if (saved.name == request.name)
            changed = saved;
    return {};
}

std::string OrcaPrinterBackend::remove_printer(const std::string& name)
{
    return std::string(Printers::remove_named_printer(m_plater, name).ToUTF8());
}

ManualPrinterResult OrcaPrinterBackend::run_manual_setup()
{
    // OrcaSlicer's own printer wizard, unchanged, and the printers it
    // installs are saved under their own names the way the old dialog's
    // manual path did.
    const Printers::VendorMap before = wxGetApp().app_config->vendors();
    // False when the person closed the wizard without applying it.
    if (!wxGetApp().run_wizard(RunReason::RR_USER, StartPage::SP_PRINTERS))
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

DiscoveryPort OrcaPrinterBackend::discovery_port()
{
    // The fake printer has no socket; on Windows this is not implemented, and
    // whether a held port hides printers there has not been tested. "Ours" makes
    // no claim: the search reports as if nothing were in the way.
#ifdef _WIN32
    return DiscoveryPort::Ours;
#else
    if (!fake_bambu_printer_agent_id(wxGetApp().app_config).empty())
        return DiscoveryPort::Ours;
    constexpr int port = 2021;
    // The plug-in's socket lives in this process and has no API to ask about.
    for (int fd = 0; fd < std::min(getdtablesize(), 4096); ++fd) {
        sockaddr_in address{};
        socklen_t   size = sizeof(address);
        int         type = 0;
        socklen_t   type_size = sizeof(type);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0 && address.sin_family == AF_INET &&
            ntohs(address.sin_port) == port && getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_size) == 0 && type == SOCK_DGRAM)
            return DiscoveryPort::Ours;
    }
    const int probe = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (probe < 0)
        return DiscoveryPort::Ours;
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port        = htons(port);
    const bool bound        = ::bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    const int  why          = errno;
    ::close(probe);
    return bound ? DiscoveryPort::Free : why == EADDRINUSE ? DiscoveryPort::Taken : DiscoveryPort::Ours;
#endif
}

void OrcaPrinterBackend::sign_in_to_bambu()
{
    wxGetApp().request_login(false, BBL_CLOUD_PROVIDER);
}

void OrcaPrinterBackend::install_network_plugin()
{
    // Orca's own installer, as its "Click here to install it" hint runs it:
    // the download turns the plug-in's preference on and loads it in place.
    wxGetApp().ShowDownNetPluginDlg();
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
            // The type the printer is saved with, as OrcaSlicer labels it.
            const ConfigOptionDef* types = preset->config.def()->get("host_type");
            const std::string      saved = preset->config.option("host_type")->serialize();
            const auto             found = std::find(types->enum_values.begin(), types->enum_values.end(), saved);
            const std::string      label = found == types->enum_values.end() ? saved :
                                                                                types->enum_labels[found - types->enum_values.begin()];
            info.state   = "unavailable";
            info.message = "This printer is saved with the " + label + " connection type, which this chat does not connect: it "
                           "connects Moonraker and OctoPrint. You can still prepare prints and export a file.";
        }
        if (m_host_attempt && m_host_attempt->name == name) {
            auto& attempt = *m_host_attempt;
            // Only a verified attempt saved its settings; it holds while the
            // printer is still set up as it was tested.
            if (attempt.state == "verified" &&
                (info.address != attempt.address || info.host_type != attempt.host_type ||
                 preset->config.opt_string("printhost_apikey") != attempt.api_key)) {
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
    if (fake_bambu_printer_agent_id(wxGetApp().app_config).empty() && !NetworkAgent::is_network_module_loaded()) {
        info.state = "unavailable";
        info.needs_network_plugin = true;
        const auto load_error = NetworkAgent::get_load_error();
        info.message = "Connecting a Bambu Lab printer needs Bambu's network plug-in, which is not loaded" +
                       (load_error.has_error ? ": " + load_error.message : std::string()) +
                       ". The person can already prepare prints for this printer without it.";
        return info;
    }
    if (!devices || !agent) {
        info.state = "unavailable";
        info.message = "Connecting is not available right now. The person can already prepare prints for this printer.";
        return info;
    }
    // The local list is what has announced itself on this network; the cloud
    // list is the person's account, wherever those printers are.
    auto nearby = devices->get_local_machinelist();
    // For the harness's no-printer chat (JUSPRIN_TEST_HEAR_NOTHING): no printer
    // is heard, whatever the real printer announces.
    if (std::getenv("JUSPRIN_TEST_HEAR_NOTHING") != nullptr)
        nearby.clear();
    auto candidates = nearby;
    for (const auto& entry : devices->get_my_cloud_machine_list())
        candidates[entry.first] = entry.second;
    // Every printer the app knows of is listed. One it does not offer carries
    // the reason, so the model is never told "found no printer" about a
    // printer the app chose not to offer (the P1S case of 2026-10-31, and the
    // sweep after it).
    for (const auto& [id, machine] : candidates) {
        if (!machine)
            continue;
        ConnectionCandidate candidate{id, machine->get_dev_name(), machine->get_dev_ip(), machine->is_lan_mode_printer(),
                                      nearby.find(id) == nearby.end()};
        const auto linked_to = std::find_if(saved.begin(), saved.end(), [&](const SavedPrinter& p) {
            return p.name != name && p.device_id == id;
        });
        if (machine->printer_type != model->device_model_id) {
            candidate.reason = "other_model";
            candidate.detail = machine->get_printer_type_display_str().ToUTF8().data();
        } else if (!info.device_id.empty() && id != info.device_id) {
            candidate.reason = "other_device";
            candidate.detail = info.device_id;
        } else if (linked_to != saved.end()) {
            candidate.reason = "linked_elsewhere";
            candidate.detail = linked_to->name;
        } else if (machine->is_lan_mode_printer() && !machine->is_avaliable()) {
            candidate.reason = "occupied";
        }
        info.candidates.push_back(std::move(candidate));
    }
    MachineObject* machine = info.device_id.empty() ? nullptr : devices->get_my_machine(info.device_id);
    // is_connected() alone is optimistic immediately after reset(). Require
    // actual parsed push data, and fresh LAN data for a LAN connection.
    const bool communicating = machine && has_recent_printer_data(*machine);
    const bool configured = has_verified_printer_connection(info.device_id);
    info.state = communicating ? "verified" : configured ? "unknown" : "not_configured";
    if (info.state == "unknown")
        info.message = "The app has no recent data from this printer.";
    if (m_connection_attempt && m_connection_attempt->name == name) {
        const auto& attempt = *m_connection_attempt;
        if (info.device_id != attempt.device_id)
            throw std::logic_error("A connection attempt lost its saved printer association");
        const bool observed = communicating &&
            (machine->is_lan_mode_printer() ? machine->last_lan_msg_time_ > attempt.observation_start
                                           : machine->last_cloud_msg_time_ > attempt.observation_start);
        // What the plug-in answered to this attempt comes first: upstream's
        // handler clears the selection and erases a wrong access code on a
        // failure, and reading that state alone named the wrong cause.
        const auto answered = Printers::last_connection_event(attempt.device_id);
        if (answered && !attempt.reselecting && answered->at >= attempt.connect_requested &&
            answered->kind != Printers::ConnectionEvent::Kind::Ok) {
            // The printer answered a wrong code, so its port needs no look.
            report_failure(info, connection_failure_text(*answered, attempt.connect_requested), answered->code != "5");
        } else if (devices->get_selected_machine() != machine && machine != nullptr && !attempt.reselecting) {
            info.state = "failed";
            info.message = "The selected printer changed. Select this printer again to reconnect.";
        } else if (observed) {
            info.state = "verified";
            m_connection_attempt.reset();
        } else if (machine == nullptr) {
            info.state = "failed";
            info.message = "The printer is no longer available. Refresh the printer list and try again.";
        } else if (std::chrono::steady_clock::now() - attempt.started > kConnectionWait) {
            report_failure(info, "The printer did not respond within " + std::to_string(kConnectionWait.count()) + " seconds.", true);
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

// On a thread of its own: a printer that does not
// answer would hold the app for the whole limit.
std::shared_ptr<OrcaPrinterBackend::PortLook> OrcaPrinterBackend::start_port_look(const std::string& address)
{
    auto look = std::make_shared<PortLook>();
    std::thread([look, address] {
        look->said = Printers::look_at_port(address, kBambuLanPort, kPortLookLimit);
        look->done.store(true);
    }).detach();
    return look;
}

void OrcaPrinterBackend::report_failure(PrinterConnectionInfo& info, const std::string& failure, bool look)
{
    auto& attempt = *m_connection_attempt;
    if (!look || attempt.address.empty()) {
        info.state   = "failed";
        info.message = failure;
        return;
    }
    // The attempt keeps reading "connecting" until both looks are back, so its
    // note says everything at once.
    if (!attempt.port_after)
        attempt.port_after = start_port_look(attempt.address);
    if (!attempt.port_before->done.load() || !attempt.port_after->done.load()) {
        info.state = "connecting";
        info.message.clear();
        return;
    }
    info.state   = "failed";
    info.message = failure + " The app also tried the printer's port " + std::to_string(kBambuLanPort) + " at " + attempt.address +
                   ". Just before the attempt, " + attempt.port_before->said + ". Right after it failed, " + attempt.port_after->said + ".";
}

std::string OrcaPrinterBackend::keep_connection_report(const std::string& text)
{
    const std::filesystem::path folder = std::filesystem::u8path(Slic3r::data_dir()) / "jusprin" / "connection_reports";
    std::error_code             failed;
    std::filesystem::create_directories(folder, failed);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path path = folder / ("report-" + std::to_string(seconds) + ".json");
    std::ofstream               file(path, std::ios::binary);
    file << text;
    file.close();
    return file.good() ? path.u8string() : std::string();
}

std::string OrcaPrinterBackend::connect_printer(const std::string& name, const std::string& device_id,
                                               const std::string& access_code)
{
    // The same printer asked for again keeps waiting; anything else replaces
    // the attempt below. One that has failed and is only having its port looked
    // at is over, though it still reads "connecting".
    if (m_connection_attempt && m_connection_attempt->name == name && m_connection_attempt->device_id == device_id &&
        connection(name).state == "connecting" && !m_connection_attempt->port_after)
        return {};
    const auto info = connection(name);
    if (info.provider != "bambu" || info.state == "unavailable")
        return info.message;
    const auto chosen = std::find_if(info.candidates.begin(), info.candidates.end(), [&](const ConnectionCandidate& p) { return p.id == device_id; });
    if (chosen == info.candidates.end())
        return "That printer is not on the list for this connection. Choose one from the list.";
    if (!chosen->reason.empty())
        return "That printer cannot be chosen (" + chosen->reason + "). Choose another from the list.";
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
                                              std::chrono::system_clock::now(), std::chrono::steady_clock::now()};
    if (machine->is_lan_mode_printer() && !machine->get_dev_ip().empty()) {
        m_connection_attempt->address     = machine->get_dev_ip();
        m_connection_attempt->port_before = start_port_look(m_connection_attempt->address);
    }
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
        m_connection_attempt->reselecting       = false;
        m_connection_attempt->connect_requested = std::chrono::steady_clock::now();
        wxGetApp().getDeviceManager()->set_selected_machine(device_id);
    });
    return {};
}

std::string OrcaPrinterBackend::connect_host(const std::string& name, const std::string& host_type, const std::string& address,
                                             const std::string& api_key, std::function<void(const HostTestOutcome&)> tested)
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
    // A 401 reads the same for no key and a wrong one, so every outcome says
    // which was sent.
    const std::string key_sent = !api_key.empty() ? "typed" : !key.empty() ? "saved" : "none";
    // Tested on a copy: the printer's own settings change only once the host
    // has accepted the test.
    DynamicPrintConfig config = preset->config;
    config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(host_type == "moonraker" ? htMoonraker : htOctoPrint));
    config.set_key_value("print_host", new ConfigOptionString(address));
    config.set_key_value("printhost_apikey", new ConfigOptionString(key));
    std::function<HostAnswer()> test;
    if (host_type == "moonraker")
        test = [host = std::make_shared<MoonrakerTest>(&config)] { return host->answer(); };
    else
        test = [host = std::make_shared<OctoPrintTest>(&config)] { return host->answer(); };

    // The test blocks until the host answers or kConnectionWait passes, so it
    // runs on a worker of its own: the plater's is shared with arrange and
    // orient, and replace_job there would drop theirs. The worker is started
    // by the first test; most panels never test a host.
    if (!m_host_worker) {
        m_host_worker = std::make_unique<BoostThreadWorker>(nullptr, "jusprin_host_test");
        // Upstream's PlaterWorker delivers its jobs' ends from the plater's
        // idle events too; it is not used because it puts up a busy cursor
        // for every job. The panel's pump keeps idle events coming while a
        // chat is open.
        m_host_worker_events = EventGuard(&m_plater, wxEVT_IDLE, [this](wxIdleEvent& event) {
            event.Skip();
            m_host_worker->process_events();
        });
    }
    m_host_attempt = HostAttempt{name, host_type, address, key, "connecting", {}};
    auto answer    = std::make_shared<HostAnswer>();
    // A test still running is dropped: its end comes back canceled.
    replace_job(
        *m_host_worker,
        [test = std::move(test), answer](Job::Ctl&) {
            *answer = test();
            wxWakeUpIdle();
        },
        [this, answer, key_sent, tested = std::move(tested)](bool canceled, std::exception_ptr& failure) {
            // An exception from the test goes on to the worker, which rethrows it.
            if (canceled || failure)
                return;
            tested(settle(*answer, key_sent));
        });
    return {};
}

HostTestOutcome OrcaPrinterBackend::settle(const HostAnswer& answer, const std::string& key_sent)
{
    HostAttempt&      attempt = *m_host_attempt;
    const std::string host    = attempt.host_type == "moonraker" ? "Moonraker" : "OctoPrint";
    const std::string key     = key_sent == "typed" ? "The test sent the API key typed in the form." :
                                key_sent == "saved" ? "The test sent the API key saved for this printer." :
                                                      "The test sent no API key.";
    HostTestOutcome   outcome{"failed", {}, key_sent};
    if (answer.passed) {
        const wxString problem = Printers::configure_named_printer_host(
            attempt.name, attempt.host_type == "moonraker" ? htMoonraker : htOctoPrint, attempt.address, attempt.api_key);
        if (problem.empty()) {
            // The test's answer is Home's first word on this host.
            Printers::note_host_answered(attempt.name);
            outcome.state   = "verified";
            outcome.message = host + " at " + attempt.address + " accepted the test, and this printer now connects there. " + key;
            // Moonraker answers while Klipper is shut down or in error, so a
            // pass says what state it reported.
            if (attempt.host_type == "moonraker")
                outcome.message += " Moonraker reports Klipper's state as " + answer.said + ".";
        } else
            outcome.message = host + " at " + attempt.address + " accepted the test, but the app could not save it as this "
                              "printer's connection: " + problem.ToStdString();
    } else if (answer.status == (attempt.host_type == "moonraker" ? 401u : 403u)) {
        outcome.state   = "needs_credential";
        outcome.message = host + " at " + attempt.address + " refused the test: " + answer.said + " " + key;
    } else if (answer.status == 0 && boost::starts_with(answer.said, "curl:Timeout was reached")) {
        // What the app saw, without a cause: a name this computer cannot look
        // up can wait here as long as a host that never answers.
        outcome.message = "The print host test had no answer from " + attempt.address + " within " +
                          std::to_string(kConnectionWait.count()) + " seconds. " + key;
    } else {
        // The host's own answer travels with the failure: an HTTP status is
        // the host answering, "doesn't look like Moonraker" another kind of
        // host, and curl's words the host not reached.
        outcome.message = (answer.said.empty() ? "The print host test failed, and it gave no reason." :
                                                 "The print host test failed: " + answer.said) + " " + key;
    }
    // connection() reports a refused key as the failed test it was.
    attempt.state   = outcome.state == "verified" ? "verified" : "failed";
    attempt.message = outcome.message;
    return outcome;
}

void OrcaPrinterBackend::cancel_connection(const std::string& name)
{
    if (m_host_attempt && m_host_attempt->name == name && m_host_attempt->state == "connecting") {
        m_host_attempt.reset();
        m_host_worker->cancel_all();
    }
    // A Bambu Lab printer stays linked to the device and selected, as after a
    // failure; undoing both is separate work.
    if (m_connection_attempt && m_connection_attempt->name == name)
        m_connection_attempt.reset();
}

void OrcaPrinterBackend::open_printer_settings(const std::string& name)
{
    // A stock profile is in a conversation only as the project's selected
    // printer, so its settings are the printer tab as it stands.
    if (!stock_printer(name).name.empty() && wxGetApp().preset_bundle->printers.get_selected_preset_name() == name) {
        SetupCommands::open_settings_tab(Preset::TYPE_PRINTER);
        return;
    }
    Printers::open_named_printer_settings(m_plater, name);
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
