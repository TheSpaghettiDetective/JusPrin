#pragma once

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

class wxWindow;
namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

void show_printer_setup(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater);
void show_printer_setup(wxWindow* owner, bool dark, Plater& plater);

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
