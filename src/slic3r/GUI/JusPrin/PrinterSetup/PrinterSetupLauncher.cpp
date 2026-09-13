#include "PrinterSetupLauncher.hpp"

#include "PrinterCatalog.hpp"
#include "PrinterDiscovery.hpp"
#include "PrinterRecognition.hpp"
#include "PrinterSetupController.hpp"
#include "PrinterSetupDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/frame.h>

#include <cstdlib>
#include <memory>
#include <utility>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

class ModalScrim
{
public:
    ModalScrim(wxWindow* owner, const ShellPalette& palette, int alpha)
    {
        if (!owner) return;
        m_window = new wxFrame(owner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                               wxFRAME_NO_TASKBAR | wxBORDER_NONE);
        if (!m_window->CanSetTransparent()) {
            m_window->Destroy();
            m_window = nullptr;
            return;
        }
        m_window->SetBackgroundColour(palette.overlay_scrim);
        m_window->SetSize(owner->GetScreenRect());
        m_window->SetTransparent(alpha);
        m_window->Show();
    }

    ~ModalScrim()
    {
        if (m_window) {
            m_window->Hide();
            m_window->Destroy();
        }
    }

    wxWindow* owner_or(wxWindow* fallback) const { return m_window ? m_window : fallback; }

private:
    wxFrame* m_window{nullptr};
};

} // namespace

void show_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater)
{
    PrinterCatalog catalog = PrinterCatalog::load(Slic3r::resources_dir());
    const bool use_mock = std::getenv("JUSPRIN_PRINTER_RECOGNITION_MOCK") != nullptr;
    OpenAIPrinterRecognitionConfig config;
    if (!use_mock && wxGetApp().app_config) {
        const std::string consent = wxGetApp().app_config->get("jusprin_agent", "cloud_consent");
        if (consent == "true" || consent == "1")
            config.api_key = Agent::load_provider_api_key("openai");
        const std::string model = wxGetApp().app_config->get("jusprin_agent", "model");
        if (!model.empty()) config.model = model;
    }
    if (const char* endpoint = std::getenv("JUSPRIN_OPENAI_ENDPOINT"); endpoint && *endpoint)
        config.endpoint = endpoint;
    std::unique_ptr<IPrinterRecognitionService> recognition;
    if (use_mock)
        recognition = std::make_unique<DeterministicPrinterRecognition>();
    else
        recognition = std::make_unique<OpenAIPrinterRecognition>(std::move(config),
                                                                  Agent::make_openai_http_transport());
    auto apply = [&plater](const PrinterCandidate& candidate, std::string& error) {
        return SetupCommands::install_and_select_printer(plater, candidate.vendor_id, candidate.model_id,
                                                         candidate.variant, candidate.default_material, error);
    };
    auto controller = std::make_unique<PrinterSetupController>(std::move(catalog), std::move(recognition),
                                                                std::move(apply));
    std::vector<DiscoveredPrinter> discovered = discover_printers();
    // Explicit developer scenarios make every handed-off Figma state
    // inspectable in an isolated app without a live API request or hardware.
    if (use_mock) {
        const std::string scenario = std::getenv("JUSPRIN_PRINTER_SETUP_SCENARIO")
            ? std::getenv("JUSPRIN_PRINTER_SETUP_SCENARIO") : "";
        if (scenario == "recognized" || scenario == "ambiguous") {
            PrinterEvidence evidence;
            evidence.description = scenario == "recognized"
                ? "Bambu Lab A1 mini" : "the ender with the touchscreen";
            controller->recognize(std::move(evidence));
            controller->poll();
        } else if (scenario == "network") {
            discovered = {{"01P00A3B", "Bambu Lab A1 mini", "192.0.2.2", "N1", "LAN", true}};
            controller->use_discovered(discovered.front());
        }
    }
    ModalScrim scrim(owner, theme.palette(dark), theme.metrics().printer_setup.scrim_alpha);
    PrinterSetupDialog dialog(scrim.owner_or(owner), theme, dark, std::move(controller), std::move(discovered));
    dialog.ShowModal();
}

void show_printer_setup(wxWindow* owner, bool dark, Plater& plater)
{
    // A missing token file or profile index is a packaging defect, not a state
    // this flow can recover from, so it propagates like it does from the
    // printer menu's entry point.
    const ShellTheme theme = ShellTheme::load_from_resources();
    show_printer_setup(owner, theme, dark, plater);
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
