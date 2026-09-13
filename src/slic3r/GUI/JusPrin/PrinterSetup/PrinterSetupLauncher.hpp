#pragma once

#include "PrinterSetupTypes.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <memory>
#include <vector>

class wxWindow;
namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

class IPrinterRecognitionService;
class PrinterSetupController;

// Add a printer from Home or the printer menu: live recognition, the printers
// on the network, and the Agent panel's own answer to "is an agent set up".
void show_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater);
void show_printer_setup(wxWindow* owner, bool dark, Plater& plater);

// The two halves show_printer_setup joins, for a caller that brings its own
// recognition, network printers, or agent state.
std::unique_ptr<PrinterSetupController> make_printer_setup_controller(
    Plater& plater, std::unique_ptr<IPrinterRecognitionService> recognition);
void run_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark,
                       std::unique_ptr<PrinterSetupController> controller,
                       std::vector<DiscoveredPrinter> discovered, bool agent_connected);

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
