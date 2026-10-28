#include "PrinterOrigin.hpp"

namespace Slic3r::GUI::JusPrin::Printers {

bool is_named_printer(const Preset& preset) { return preset.type == Preset::TYPE_PRINTER && preset.is_user() && preset.is_visible; }

bool printer_from_file(const Preset& preset) { return !preset.is_default && !is_named_printer(preset); }

} // namespace Slic3r::GUI::JusPrin::Printers
