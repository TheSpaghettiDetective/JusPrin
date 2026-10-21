#pragma once

// What the printer conversation is allowed to know and do about printers.
//
// A printer is app-level, not project-level, so this follows Home rather than
// the workspace contract: the surface owns a narrow interface, and one Orca
// implementation behind it calls the existing owners -- PrinterCatalog,
// PrinterDiscovery, Printers::, SetupCommands::. GUI-free and
// Orca-free so the conversation can be tested without either.

#include "PrinterSetupTypes.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// One spool as a connected printer reports it.
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
    // One of OrcaSlicer's own printer profiles, selected in the project, not
    // a printer the person saved: nothing is saved on it, and a change to its
    // settings is saved as a copy under `copy_name`, which is then theirs.
    bool                      stock{false};
    std::string               copy_name;
};

// What the packaged profiles offer, one entry per model.
struct CatalogPrinter
{
    std::string         id; // catalogue id, stable for this session
    std::string         vendor_id;
    std::string         vendor_name;  // the brand, as its vendor file names it
    std::string         model_id;     // the profile's own name, which Orca installs by
    std::string         model_name;   // that name with the brand not repeated
    std::string         device_model_id;
    std::string         build_volume; // "180 × 180 × 180 mm"
    std::string         picture;
    std::string         default_plate; // empty: the profile names none
    std::vector<double> nozzles;
    // The machine profile's own default filament for each nozzle size, in
    // the order of `nozzles`; empty where that profile names none.
    std::vector<std::string> filaments;

    std::string display_name() const { return vendor_name + " " + model_name; }
    std::string filament_for(double nozzle) const
    {
        for (std::size_t i = 0; i < nozzles.size() && i < filaments.size(); ++i)
            if (nozzles[i] == nozzle)
                return filaments[i];
        return {};
    }
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
    // The plate to start projects on, as OrcaSlicer's BedType value; absent
    // leaves the plate OrcaSlicer picks for the profile.
    std::optional<int> plate;
};

// One build plate OrcaSlicer offers for a printer: its BedType value and the
// name OrcaSlicer shows for it.
struct PlateOption
{
    int         type{0};
    std::string label;
};

// One filament preset the printer form offers.
struct FilamentOption
{
    std::string name;     // the preset's name, which installs and selects it
    std::string label;    // the name OrcaSlicer shows in its lists (the alias)
    std::string vendor;   // filament_vendor, e.g. "Generic", "eSUN"
    std::string material; // filament_type, e.g. "PLA"
    // Empty when OrcaSlicer counts it as fitting the printer; otherwise the
    // printers its profile names, as a phrase: "the Prusa CORE One and
    // Prusa XL", or "other printers" when it names none it can say.
    std::string made_for;
    // A person's own preset: the preset it was saved from, when it was.
    std::string base;
};

// What the printer form offers for one model and nozzle, read from the
// packaged profiles as OrcaSlicer would load them. Nothing in it is
// installed until the person finishes the form.
struct SetupOptions
{
    std::string catalog_id;
    double      nozzle{0.};
    // Empty when OrcaSlicer offers no plate choice for this printer.
    std::vector<PlateOption> plates;
    int                      default_plate{0}; // the plate OrcaSlicer starts it on
    std::vector<FilamentOption> filaments;      // compatible, by label
    // The chosen brand's and OrcaSlicer's library filaments that do not fit
    // this printer, by label; picking one saves a copy for the printer.
    std::vector<FilamentOption> others;
    // The person's own filament presets, by name; `made_for` says which do
    // not fit, and picking one of those saves a copy for the printer.
    std::vector<FilamentOption> own;
    std::vector<std::string>    suggested;      // the model's default_materials that are compatible
    std::string                 default_filament; // the profile's own default; may be empty
    // Set when the profiles could not be read; nothing else is then.
    std::string error;
};

// A correction to a printer that already exists. Every field is optional;
// what is left out stays as it is.
struct ChangePrinterRequest
{
    std::string                  name; // the saved printer to change
    std::optional<double>        nozzle;
};

struct ConnectionCandidate
{
    std::string id;
    std::string name;
    std::string address;
    bool lan_mode{false};
};

// How long a connection attempt waits for the printer before it is reported
// as failed. The model is told the same number when an attempt starts.
inline constexpr std::chrono::seconds kConnectionWait{30};

struct PrinterConnectionInfo
{
    std::string provider{"unavailable"};
    std::string state{"not_configured"};
    std::string message;
    std::string device_id;
    std::vector<ConnectionCandidate> candidates;
    std::string address;
    std::string host_type;
    bool signed_in{false};
    bool nozzle_mismatch{false};
    // A Bambu Lab printer, on a computer where Bambu's network plug-in is not
    // installed or not turned on: nothing reaches the printer until it is.
    bool needs_network_plugin{false};
};

struct ManualPrinterResult
{
    bool applied{false};
    std::vector<SavedPrinter> added;
};

class IPrinterBackend
{
public:
    virtual ~IPrinterBackend() = default;

    // Reads. The catalogue is every model a person can add from this panel,
    // in a stable order.
    virtual const std::vector<CatalogPrinter>& catalog() const = 0;
    virtual std::vector<DiscoveredPrinter> network_printers() const = 0;
    // Every Bambu Lab printer the app has found and could set up, including
    // one it holds no access code for yet. network_printers() leaves those out.
    virtual std::vector<DiscoveredPrinter> nearby_printers() const { return network_printers(); }
    // The plates and filaments the printer form offers for one catalogue
    // model and nozzle. Called off the GUI thread: it reads files, and what
    // prepare_setup_options took of the person's own presets.
    virtual SetupOptions setup_options(const std::string& catalog_id, double nozzle) const = 0;
    // Takes what setup_options needs of the app's state, on the GUI thread,
    // before any setup_options call it is for.
    virtual void prepare_setup_options() {}
    // Puts `filament` in slot 1 of the printer just added and selected. With
    // `copy`, first saves a copy of it for that printer, which OrcaSlicer
    // keeps in the slot across printer changes; `used` names the preset now
    // in the slot. Returns a message for the person when it could not.
    virtual std::string start_with_filament(const std::string& printer, const std::string& filament, bool copy, std::string& used)
    {
        return "Choosing a filament is unavailable.";
    }
    // Opens OrcaSlicer's Create filament window for the printer just added
    // and selected, and puts a preset made there for it in slot 1. Returns
    // that preset's name, or an empty one when none was made for it.
    virtual std::string create_filament_preset(const std::string& printer) { return {}; }
    virtual std::vector<SavedPrinter>     saved_printers() const = 0;
    // A printer profile OrcaSlicer ships, by its preset name, as a person
    // could pick it; `stock` is set. An empty name when there is none.
    virtual SavedPrinter                  stock_printer(const std::string& name) const = 0;

    // Writes. Each returns an empty string on success, or a message written
    // for the person explaining what stopped it. A change lists the keys of
    // the unsaved edits it dropped from the project's copy in `discarded`.
    virtual std::string add_printer(const AddPrinterRequest& request, SavedPrinter& added) = 0;
    virtual std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& changed,
                                       std::vector<std::string>& discarded) = 0;
    // Deletes a saved printer the way Home's "Remove printer…" does.
    virtual std::string remove_printer(const std::string& name) = 0;

    // The ways to OrcaSlicer's own screens: its printer wizard for a new
    // printer (only where the printer form cannot be shown), its printer
    // settings for an existing one.
    virtual ManualPrinterResult run_manual_setup() = 0;
    virtual void open_printer_settings(const std::string& name) = 0;

    // Connection is a separate operation against an existing saved printer.
    // Credentials are input-only and never part of a snapshot or conversation.
    // connect_printer and connect_host start an attempt and return at once;
    // connection() reports it as connecting until it settles, and a new
    // attempt replaces one still waiting. connect_host saves the address only
    // once the printer has answered.
    virtual PrinterConnectionInfo connection(const std::string& name) { return {}; }
    virtual void prepare_connection(const std::string& name) {}
    virtual void sign_in_to_bambu() {}
    // Downloads and loads Bambu's network plug-in behind Orca's own progress
    // dialog, as Orca's "Click here to install it" does. Returns once that
    // dialog closes, whether or not the plug-in is loaded then.
    virtual void install_network_plugin() {}
    virtual std::string connect_printer(const std::string& name, const std::string& device_id,
                                       const std::string& access_code) { return "Connection is unavailable."; }
    virtual std::string connect_host(const std::string& name, const std::string& host_type,
                                    const std::string& address, const std::string& api_key) { return "Connection is unavailable."; }
    // Abandons this printer's attempt if it is still waiting: whatever the
    // printer answers afterwards is dropped.
    virtual void cancel_connection(const std::string& name) {}
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
