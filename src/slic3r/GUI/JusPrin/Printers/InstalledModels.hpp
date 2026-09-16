#pragma once

// Which printer models a setup step enabled, read from the app config's
// vendor map before and after it. Orca's setup wizard is a list of models to
// install profiles for, not a list of machines, so JusPrin decides afterwards
// which new printers it stands for: one per model it newly enabled. Enabling
// another nozzle on a model already installed adds no printer, because the
// nozzle is a setting of the printer.
//
// GUI-free and Orca-free: the map is AppConfig::VendorMap's shape.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Printers {

// vendor -> model -> enabled nozzle variants ("0.4")
using VendorMap = std::map<std::string, std::map<std::string, std::set<std::string>>>;

struct InstalledModel
{
    std::string vendor;
    std::string model;
    std::string variant; // the nozzle the new printer starts on
};

// Models with at least one variant in `after` and none in `before`, in map
// order. Each starts on `selected_variant` when it is `selected_model` and
// that variant is enabled -- the printer the wizard left selected -- else on
// 0.4 when enabled, else on its first enabled variant.
std::vector<InstalledModel> newly_installed_models(const VendorMap& before, const VendorMap& after,
                                                   const std::string& selected_model,
                                                   const std::string& selected_variant);

} // namespace Slic3r::GUI::JusPrin::Printers
