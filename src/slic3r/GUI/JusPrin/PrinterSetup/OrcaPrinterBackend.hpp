#pragma once

// The one route from the printer panel to Orca. Every read comes from an
// existing owner -- the packaged catalogue, Bambu's device list, the printer
// preset collection -- and writes reuse the preset, device,
// networking and print-host owners rather than copying their mechanisms.

#include "PrinterBackend.hpp"
#include "PrinterCatalog.hpp"
#include "libslic3r/Preset.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include <atomic>
#include <chrono>
#include <memory>

namespace Slic3r::GUI { class Plater; class BoostThreadWorker; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// What a print host's test saw: whether it passed, the HTTP status it was
// answered with (0 for none), and what the host or curl said, or on a
// Moonraker pass Klipper's state.
struct HostAnswer
{
    bool        passed{false};
    unsigned    status{0};
    std::string said;
};

class OrcaPrinterBackend : public IPrinterBackend
{
public:
    OrcaPrinterBackend(Plater& plater, PrinterCatalog catalog);
    ~OrcaPrinterBackend() override;

    const std::vector<CatalogPrinter>& catalog() const override { return m_models; }
    std::vector<DiscoveredPrinter> network_printers() const override;
    std::vector<DiscoveredPrinter> nearby_printers() const override;
    SetupOptions                   setup_options(const std::string& catalog_id, double nozzle) const override;
    void                           prepare_setup_options() override;
    std::string start_with_filament(const std::string& printer, const std::string& filament, bool copy, std::string& used) override;
    std::string create_filament_preset(const std::string& printer) override;
    std::vector<SavedPrinter>      saved_printers() const override;
    SavedPrinter                   stock_printer(const std::string& name) const override;

    std::string add_printer(const AddPrinterRequest& request, SavedPrinter& added) override;
    std::string change_printer(const ChangePrinterRequest& request, SavedPrinter& changed,
                               std::vector<std::string>& discarded) override;
    std::string remove_printer(const std::string& name) override;

    ManualPrinterResult run_manual_setup() override;
    void open_printer_settings(const std::string& name) override;
    PrinterConnectionInfo connection(const std::string& name) override;
    void prepare_connection(const std::string& name) override;
    DiscoveryPort discovery_port() override;
    void sign_in_to_bambu() override;
    void install_network_plugin() override;
    std::string keep_connection_report(const std::string& text) override;
    std::string connect_printer(const std::string& name, const std::string& device_id,
                               const std::string& access_code) override;
    std::string connect_host(const std::string& name, const std::string& host_type, const std::string& address,
                             const std::string& api_key, std::function<void(const HostTestOutcome&)> tested) override;
    void cancel_connection(const std::string& name) override;

private:
    const CatalogPrinter* model_of(const std::string& printer_name) const;

    Plater&                     m_plater;
    PrinterCatalog              m_catalog;
    // Where the catalogue was read from, which the printer form's options
    // are read from too.
    std::string                 m_profiles_directory;
    // Copies of the person's own filament presets, taken on the GUI thread
    // by prepare_setup_options for setup_options to read off it.
    std::vector<Preset>         m_own_filaments;
    // One entry per model, built once from the catalogue's per-variant
    // candidates: the panel proposes printers, and a nozzle is a fact about
    // the printer rather than a printer of its own.
    std::vector<CatalogPrinter> m_models;
    // What the app saw at the printer's port around an attempt. The thread
    // writes `said` before `done`, which the reader loads first.
    struct PortLook {
        std::atomic<bool> done{false};
        std::string       said;
    };
    struct ConnectionAttempt {
        std::string name;
        std::string device_id;
        std::chrono::steady_clock::time_point started;
        std::chrono::system_clock::time_point observation_start;
        // When the device was asked to connect: an answer from the plug-in
        // before this (the disconnect a re-selection starts with) is not this
        // attempt's.
        std::chrono::steady_clock::time_point connect_requested;
        // Deselected, to be selected again on the next event turn.
        bool reselecting{false};
        // A printer in LAN mode: where the app looks, as the attempt starts
        // and when it fails.
        std::string address{};
        std::shared_ptr<PortLook> port_before{};
        std::shared_ptr<PortLook> port_after{};
    };
    std::optional<ConnectionAttempt> m_connection_attempt;
    static std::shared_ptr<PortLook> start_port_look(const std::string& address);
    void report_failure(PrinterConnectionInfo& info, const std::string& failure, bool look);
    // What a deferred reselection checks before touching this backend.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    // The print host test last started: "connecting" while its Job runs, then
    // what it came to, which connection() reports.
    struct HostAttempt {
        std::string name, host_type, address, api_key;
        std::string state;
        std::string message;
    };
    // Saves the settings a passed test used, and words the outcome. `key_sent`
    // is "typed", "saved" or "none".
    HostTestOutcome settle(const HostAnswer& answer, const std::string& key_sent);
    std::optional<HostAttempt> m_host_attempt;
    // Print host tests run here, one at a time, from the first test on. Last,
    // so the worker is stopped before anything its Jobs' ends read: its
    // destructor waits up to 10 seconds for a test still running, and the
    // test itself gives up after kConnectionWait.
    std::unique_ptr<BoostThreadWorker> m_host_worker;
    EventGuard                         m_host_worker_events;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
