#pragma once

// What the printer conversation is allowed to know and do about printers.
//
// A printer is app-level, not project-level, so this follows Home rather than
// the workspace contract: the surface owns a narrow interface, and one Orca
// implementation behind it calls the existing owners -- PrinterCatalog,
// PrinterDiscovery, Printers::, SetupCommands::, SpoolStore. GUI-free and
// Orca-free so the conversation can be tested without either.

#include "PrinterSetupTypes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// One spool as a printer reports it or as this app remembers it.
struct PrinterSpool
{
    std::string name;
    std::string material;
    std::string colour; // "#RRGGBB", empty when unknown
};

// A printer already saved in this app: an Orca user printer profile, with the
// facts it was set up with.
struct SavedPrinter
{
    std::string               name;
    std::string               device_id;
    std::string               model;        // display model name
    std::string               model_id;     // profile model id
    std::string               vendor_id;
    std::string               picture;      // cover image path, may be empty
    double                    nozzle{0.};
    std::vector<double>       nozzles;      // sizes this model ships
    std::string               plate;        // untranslated plate name
    std::string               ams;          // "AMS lite", "AMS", or empty
    std::vector<PrinterSpool> spools;
    std::string               activity;     // offline, idle, printing, or empty
    bool                      connected{false};
};

// What the packaged profiles offer, one entry per model.
struct CatalogPrinter
{
    std::string         id; // catalogue id, stable for this session
    std::string         vendor_id;
    std::string         vendor_name;
    std::string         model_id;
    std::string         model_name;
    std::string         device_model_id;
    std::string         build_volume; // "180 × 180 × 180 mm"
    std::string         picture;
    std::string         default_plate;
    std::string         default_material;
    std::vector<double> nozzles;
};

// Everything the app needs to save a printer the person has just agreed to.
struct AddPrinterRequest
{
    std::string vendor_id;
    std::string model_id;
    std::string variant;  // nozzle size as the profile spells it, e.g. "0.4"
    std::string material; // default filament preset, may be empty
    std::string name;     // display name; empty uses the model name
    std::string device_id;    // a discovered printer's id, when this is one
    std::string access_code;  // optional, saved only when a device was named
};

// A correction to a printer that already exists. Every field is optional;
// what is left out stays as it is.
struct ChangePrinterRequest
{
    std::string                  name; // the saved printer to change
    std::optional<double>        nozzle;
    std::optional<std::string>   access_code;
    std::optional<std::vector<PrinterSpool>> spools;
};

class IPrinterBackend
{
public:
    virtual ~IPrinterBackend() = default;

    // Reads.
    // The whole packaged catalogue, one entry per model, for the agent's
    // identification prompt and for validating the catalogIds it answers
    // with. Excludes kVendorHiddenFromPrinterFlow (see PrinterCatalog.hpp);
    // everywhere else in the app still sees it, including "Set it up myself".
    virtual std::vector<CatalogPrinter>   catalog_models() const = 0;
    virtual std::vector<DiscoveredPrinter> network_printers() const = 0;
    virtual std::vector<SavedPrinter>     saved_printers() const = 0;

    // Writes. Each returns an empty string on success, or a message written
    // for the person explaining what stopped it.
    virtual std::string add_printer(const AddPrinterRequest& request, SavedPrinter& added) = 0;
    virtual std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& changed) = 0;

    // The manual paths behind "Set it up myself": OrcaSlicer's own printer
    // wizard for a new printer, its printer settings for an existing one.
    virtual void run_manual_setup() = 0;
    virtual void open_printer_settings(const std::string& name) = 0;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
