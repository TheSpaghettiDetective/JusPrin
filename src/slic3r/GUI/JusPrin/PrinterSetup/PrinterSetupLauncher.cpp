#include "PrinterSetupLauncher.hpp"

#include "PrinterCatalog.hpp"
#include "PrinterDiscovery.hpp"
#include "PrinterRecognition.hpp"
#include "PrinterSetupController.hpp"
#include "PrinterSetupDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Shell/AgentPane.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/frame.h>

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

} // namespace

bool add_printer(Plater& plater, const PrinterCandidate& candidate, const std::optional<DiscoveredPrinter>& device,
                 std::string& error)
{
    if (!SetupCommands::install_and_select_printer(plater, candidate.vendor_id, candidate.model_id, candidate.variant,
                                                   candidate.default_material, error))
        return false;
    Printers::add_named_printer(plater, device && !device->name.empty() ? device->name : candidate.model_name,
                                device ? device->stable_id : std::string());
    return true;
}

void run_manual_setup(Plater& plater)
{
    const Printers::VendorMap before = wxGetApp().app_config->vendors();
    // False when the person closed the wizard without applying it.
    if (wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS))
        name_installed_printers(plater, before);
}

void name_installed_printers(Plater& plater, const Printers::VendorMap& before)
{
    PresetBundle&     presets  = *wxGetApp().preset_bundle;
    const std::string selected = presets.printers.get_selected_preset_name();
    const Preset&     current  = presets.printers.get_selected_preset();
    const auto        models   = Printers::newly_installed_models(before, wxGetApp().app_config->vendors(),
                                                                  current.config.opt_string("printer_model"),
                                                                  current.config.opt_string("printer_variant"));
    std::string keep;
    for (const Printers::InstalledModel& model : models) {
        const Preset* profile = presets.printers.find_system_preset_by_model_and_variant(model.model, model.variant);
        if (profile == nullptr)
            throw std::runtime_error("The wizard enabled " + model.model + " " + model.variant +
                                     " but no system profile for it is loaded");
        const std::string profile_name = profile->name;
        if (!SetupCommands::select_printer_preset(plater, profile_name) ||
            presets.printers.get_selected_preset_name() != profile_name)
            return; // the person kept unsaved printer changes; nothing more is named
        const std::string name = Printers::add_named_printer(plater, model.model, {});
        if (profile_name == selected)
            keep = name;
    }
    // Naming selects each printer in turn; the wizard's own choice is the one
    // left selected, under its name when it was one of them.
    if (!models.empty())
        SetupCommands::select_printer_preset(plater, keep.empty() ? selected : keep);
}

void show_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater)
{
    // Both entry points, Home and the printer menu, belong to the shell.
    ShellController* shell = installed_shell();
    if (!shell || !shell->agent_pane())
        throw std::logic_error("Add a printer opened without the JusPrin shell installed");

    OpenAIPrinterRecognitionConfig config;
    if (wxGetApp().app_config) {
        const std::string consent = wxGetApp().app_config->get("jusprin_agent", "cloud_consent");
        if (consent == "true" || consent == "1")
            config.api_key = Agent::load_provider_api_key(wxGetApp().app_config, "openai");
        const std::string model = wxGetApp().app_config->get("jusprin_agent", "model");
        if (!model.empty()) config.model = model;
    }
    auto recognition = std::make_unique<OpenAIPrinterRecognition>(std::move(config), Agent::make_openai_http_transport());
    // The controller is built from `apply`, so the flow it answers for is
    // filled in once it exists; it outlives every call, which only happens
    // inside dialog.ShowModal() below.
    PrinterSetupController* flow = nullptr;
    auto apply = [&plater, &flow](const PrinterCandidate& candidate, std::string& error) {
        return add_printer(plater, candidate, flow->evidence().discovered_device, error);
    };
    auto controller = std::make_unique<PrinterSetupController>(PrinterCatalog::load(Slic3r::resources_dir()),
                                                                std::move(recognition), std::move(apply));
    flow = controller.get();
    // The same answer the Agent panel gives when it shows its empty state.
    const bool agent_connected =
        shell->agent_pane()->web_view().host().availability() == Agent::AgentAvailability::Ready;
    auto connect = [](const DiscoveredPrinter& printer, const std::string& code, wxString& error) {
        return SetupCommands::set_printer_access_code(printer.stable_id, code, error);
    };
    // Builds a second, throwaway AgentWebView the first time "Set up the
    // agent" is clicked: its own independent AgentService/AgentSetupService
    // (never the docked pane's), and deliberately no start_mcp() call, since
    // a setup-only surface has no need to announce itself for MCP discovery
    // or contend with the docked pane's port/discovery file.
    auto make_setup_webview = [&theme, dark, shell](wxWindow* parent) {
        Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
        auto webview = std::make_unique<AgentWebView>(
            parent, theme, *shell->workspace(), *shell->persistence(), runtime.availability,
            std::move(runtime.service), runtime.setup, /*embedded=*/true);
        webview->apply_appearance(dark);
        webview->SetName(_L("Agent setup"));
        return webview;
    };

    ModalScrim scrim(owner, theme.palette(dark), theme.metrics().printer_setup.scrim_alpha);
    PrinterSetupDialog dialog(scrim.owner_or(owner), theme, dark, std::move(controller), discover_printers(),
                              agent_connected, make_setup_webview, connect,
                              [shell] { shell->mark_agent_config_possibly_changed(); });
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
