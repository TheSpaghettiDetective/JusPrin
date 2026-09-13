#include "PrinterSetupLauncher.hpp"

#include "PrinterCatalog.hpp"
#include "PrinterDiscovery.hpp"
#include "PrinterRecognition.hpp"
#include "PrinterSetupController.hpp"
#include "PrinterSetupDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Shell/AgentPane.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/frame.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>
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

// Both entry points, Home and the printer menu, belong to the shell.
ShellController& required_shell()
{
    ShellController* shell = installed_shell();
    if (!shell || !shell->agent_pane())
        throw std::logic_error("Add a printer opened without the JusPrin shell installed");
    return *shell;
}

// ConnectPrinterDialog::on_button_confirm: an access code is letters and
// digits, and it is handed to the machine as the user's code.
bool connect_with_access_code(const DiscoveredPrinter& printer, const std::string& code, wxString& error)
{
    const bool valid = std::all_of(code.begin(), code.end(), [](char c) {
        return ('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
    });
    if (!valid) {
        error = _L("Invalid input.");
        return false;
    }
    DeviceManager* devices = wxGetApp().getDeviceManager();
    MachineObject* machine = devices ? devices->get_my_machine(printer.stable_id) : nullptr;
    if (!machine) {
        error = _L("This printer is no longer on your network.");
        return false;
    }
    machine->set_user_access_code(code);
    return true;
}

} // namespace

std::unique_ptr<PrinterSetupController> make_printer_setup_controller(
    Plater& plater, std::unique_ptr<IPrinterRecognitionService> recognition)
{
    auto apply = [&plater](const PrinterCandidate& candidate, std::string& error) {
        return SetupCommands::install_and_select_printer(plater, candidate.vendor_id, candidate.model_id,
                                                         candidate.variant, candidate.default_material, error);
    };
    return std::make_unique<PrinterSetupController>(PrinterCatalog::load(Slic3r::resources_dir()),
                                                    std::move(recognition), std::move(apply));
}

void run_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark,
                       std::unique_ptr<PrinterSetupController> controller,
                       std::vector<DiscoveredPrinter> discovered, bool agent_connected)
{
    ShellController* shell = &required_shell();
    ModalScrim scrim(owner, theme.palette(dark), theme.metrics().printer_setup.scrim_alpha);
    PrinterSetupDialog dialog(scrim.owner_or(owner), theme, dark, std::move(controller), std::move(discovered),
                              agent_connected, [shell] { shell->open_agent_setup(); }, connect_with_access_code);
    dialog.ShowModal();
}

void show_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater)
{
    ShellController& shell = required_shell();
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
    // The same answer the Agent panel gives when it shows its empty state.
    const bool agent_connected =
        shell.agent_pane()->web_view().host().availability() == Agent::AgentAvailability::Ready;
    run_printer_setup(owner, theme, dark, make_printer_setup_controller(plater, std::move(recognition)),
                      discover_printers(), agent_connected);
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
