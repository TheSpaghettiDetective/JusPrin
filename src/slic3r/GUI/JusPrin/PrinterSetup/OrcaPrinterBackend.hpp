#pragma once

// The one route from the printer panel to Orca. Every read comes from an
// existing owner -- the packaged catalogue, Bambu's device list, the printer
// preset collection, the spool store -- and every write goes through
// SetupCommands or Printers::, never through a preset or a device directly.

#include "PrinterBackend.hpp"
#include "PrinterCatalog.hpp"
#include <chrono>

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

    ManualPrinterResult run_manual_setup() override;
    void open_printer_settings(const std::string& name) override;
    PrinterConnectionInfo connection(const std::string& name) override;
    void prepare_connection(const std::string& name) override;
    void sign_in_to_bambu() override;
    std::string connect_printer(const std::string& name, const std::string& device_id,
                               const std::string& access_code) override;
    std::string connect_host(const std::string& name, const std::string& host_type,
                            const std::string& address, const std::string& api_key) override;

private:
    const CatalogPrinter* model_of(const std::string& printer_name) const;

    Plater&                     m_plater;
    PrinterCatalog              m_catalog;
    Workspace::SpoolStore*      m_spools{nullptr};
    // One entry per model, built once from the catalogue's per-variant
    // candidates: the panel proposes printers, and a nozzle is a fact about
    // the printer rather than a printer of its own.
    std::vector<CatalogPrinter> m_models;
    struct ConnectionAttempt {
        std::string name;
        std::string device_id;
        std::chrono::steady_clock::time_point started;
        std::chrono::system_clock::time_point observation_start;
    };
    std::optional<ConnectionAttempt> m_connection_attempt;
    struct HostAttempt {
        std::string name, host_type, address, api_key;
        std::string state;
        std::string message;
    };
    std::optional<HostAttempt> m_host_attempt;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
