#pragma once

#include "PrinterSetupTypes.hpp"
#include "slic3r/GUI/JusPrin/Printers/InstalledModels.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <optional>
#include <string>

class wxWindow;
namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

void show_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater);
void show_printer_setup(wxWindow* owner, bool dark, Plater& plater);

// What "Add this printer" does. Adding a printer adds a machine: the model's
// system profile is installed and selected, then saved as a named printer of
// its own, so a second printer of the same model is a second printer rather
// than the first one again. A network printer keeps the name it already has
// and stays linked to its device. False, with a message for the person, when
// the profile cannot be installed; the previous setup is then unchanged.
bool add_printer(Plater& plater, const PrinterCandidate& candidate, const std::optional<DiscoveredPrinter>& device,
                 std::string& error);

// "Set it up myself": Orca's own printer wizard, then a named printer for
// each model it newly enabled, as add_printer makes one for a recognized
// model. The wizard's own selection stays selected, as its named printer.
void run_manual_setup(Plater& plater);

// The second half of run_manual_setup, given the app config's vendor map from
// before the wizard ran.
void name_installed_printers(Plater& plater, const Printers::VendorMap& before);

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
