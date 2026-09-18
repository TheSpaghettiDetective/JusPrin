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
#include "slic3r/GUI/JusPrin/Printers/InstalledModels.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

CatalogPrinter entry_of(const PrinterCandidate& candidate)
{
    CatalogPrinter printer;
    printer.id               = candidate.vendor_id + "/" + candidate.model_id;
    printer.vendor_id        = candidate.vendor_id;
    printer.vendor_name      = candidate.vendor_name;
    printer.model_id         = candidate.model_id;
    printer.model_name       = candidate.model_name;
    printer.device_model_id  = candidate.device_model_id;
    printer.build_volume     = candidate.build_volume;
    printer.picture          = candidate.artwork_path;
    printer.default_plate    = candidate.default_plate;
    printer.default_material = candidate.default_material;
    return printer;
}

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
    // The catalogue carries one candidate per model and nozzle; a person has
    // one printer with one nozzle, so fold the variants into the model and
    // keep the sizes it ships as a fact about it.
    for (const PrinterCandidate& candidate : m_catalog.candidates()) {
        const std::string id = candidate.vendor_id + "/" + candidate.model_id;
        const auto        known = std::find_if(m_models.begin(), m_models.end(),
                                               [&id](const CatalogPrinter& printer) { return printer.id == id; });
        CatalogPrinter&   printer = known == m_models.end() ? m_models.emplace_back(entry_of(candidate)) : *known;
        const double      nozzle  = nozzle_of(candidate.variant);
        if (nozzle > 0. && std::find(printer.nozzles.begin(), printer.nozzles.end(), nozzle) == printer.nozzles.end())
            printer.nozzles.push_back(nozzle);
    }
    for (CatalogPrinter& printer : m_models)
        std::sort(printer.nozzles.begin(), printer.nozzles.end());
}

std::vector<CatalogPrinter> OrcaPrinterBackend::catalog_models() const
{
    std::vector<CatalogPrinter> visible;
    for (const CatalogPrinter& printer : m_models)
        if (printer.vendor_id != kVendorHiddenFromPrinterFlow)
            visible.push_back(printer);
    return visible;
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

std::string OrcaPrinterBackend::material_word(const std::string& filament_preset) const
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || filament_preset.empty())
        return filament_preset;
    const Preset* preset = presets->filaments.find_preset(filament_preset, false);
    // A preset the store still names but this app no longer has is reported
    // as it was recorded, not dropped: the person can still tell it apart.
    if (preset == nullptr)
        return filament_preset;
    const std::string type = preset->config.opt_string("filament_type", 0u);
    return type.empty() ? filament_preset : type;
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
            saved.model     = model->vendor_name + " " + model->model_name;
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
                saved.spools.push_back(PrinterSpool{spool.name, material_word(spool.filament_preset), spool.colour});
        }
        printers.push_back(std::move(saved));
    }
    return printers;
}

std::string OrcaPrinterBackend::add_printer(const AddPrinterRequest& request, SavedPrinter& added)
{
    std::string error;
    if (!SetupCommands::install_and_select_printer(m_plater, request.vendor_id, request.model_id, request.variant,
                                                  request.material, error))
        return error.empty() ? std::string("This printer could not be installed.") : error;

    const std::string name =
        Printers::add_named_printer(m_plater, request.name.empty() ? request.model_id : request.name, request.device_id);

    if (!request.access_code.empty() && !request.device_id.empty()) {
        wxString connect_error;
        // The printer is saved either way: a code that does not take is a
        // connection that has to be made again, not a printer that was lost.
        if (!SetupCommands::set_printer_access_code(request.device_id, request.access_code, connect_error)) {
            for (const SavedPrinter& saved : saved_printers())
                if (saved.name == name)
                    added = saved;
            return connect_error.empty() ? std::string("That access code was not accepted.") : connect_error.ToStdString();
        }
    }

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

    if (request.access_code) {
        if (found->device_id.empty())
            return "This printer is not one this app found on your network, so it has no access code.";
        wxString code_error;
        if (!SetupCommands::set_printer_access_code(found->device_id, *request.access_code, code_error))
            return code_error.empty() ? std::string("That access code was not accepted.") : code_error.ToStdString();
    }

    for (const SavedPrinter& saved : saved_printers())
        if (saved.name == request.name)
            changed = saved;
    return {};
}

void OrcaPrinterBackend::run_manual_setup()
{
    // OrcaSlicer's own printer wizard, unchanged, and the printers it
    // installs are saved under their own names the way the old dialog's
    // manual path did.
    const Printers::VendorMap before = wxGetApp().app_config->vendors();
    // False when the person closed the wizard without applying it.
    if (!wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS))
        return;
    Printers::name_installed_printers(m_plater, before);
}

void OrcaPrinterBackend::open_printer_settings(const std::string& name)
{
    Printers::open_named_printer_settings(m_plater, name);
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
