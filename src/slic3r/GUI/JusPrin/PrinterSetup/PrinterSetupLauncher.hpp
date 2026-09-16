#pragma once

#include "PrinterSetupTypes.hpp"
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

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
