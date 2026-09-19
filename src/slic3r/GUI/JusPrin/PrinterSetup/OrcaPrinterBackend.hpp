#pragma once

// The one route from the printer panel to Orca. Every read comes from an
// existing owner -- the packaged catalogue, Bambu's device list, the printer
// preset collection, the spool store -- and every write goes through
// SetupCommands or Printers::, never through a preset or a device directly.

#include "PrinterBackend.hpp"
#include "PrinterCatalog.hpp"

namespace Slic3r::GUI { class Plater; }
namespace Slic3r::GUI::JusPrin::Workspace { class SpoolStore; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

class OrcaPrinterBackend : public IPrinterBackend
{
public:
    // `spools` is the app-level store the rest of the shell already shares;
    // without one this backend reports a printer's spools as unknown and
    // refuses to change them.
    OrcaPrinterBackend(Plater& plater, PrinterCatalog catalog, Workspace::SpoolStore* spools);

    const std::vector<CatalogPrinter>& catalog() const override { return m_models; }
    std::vector<DiscoveredPrinter> network_printers() const override;
    std::vector<SavedPrinter>      saved_printers() const override;

    std::string add_printer(const AddPrinterRequest& request, SavedPrinter& added) override;
    std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& changed) override;

    void run_manual_setup() override;
    void open_printer_settings(const std::string& name) override;

private:
    const CatalogPrinter* model_of(const std::string& printer_name) const;

    Plater&                     m_plater;
    PrinterCatalog              m_catalog;
    Workspace::SpoolStore*      m_spools{nullptr};
    // One entry per model, built once from the catalogue's per-variant
    // candidates: the panel proposes printers, and a nozzle is a fact about
    // the printer rather than a printer of its own.
    std::vector<CatalogPrinter> m_models;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
