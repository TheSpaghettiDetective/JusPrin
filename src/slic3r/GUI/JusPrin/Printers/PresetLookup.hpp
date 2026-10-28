#pragma once

// Which presets a name may refer to. GUI-free so the rule is testable on its
// own.

#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <string>

namespace Slic3r::GUI::JusPrin::Printers {

// The printer, process or filament preset `name` finds, as find_preset finds
// it: one the person's list shows, or one the project uses. Null for any
// other name and for Orca's placeholder.
//
// is_visible says the preset belongs to a printer model the person enabled,
// so a 3MF can select one it hides. Orca's combo box marks a selected hidden
// preset visible as it draws, but not the edited copy find_preset returns for
// the selected preset, so the flag alone never answers "in use".
inline const Preset* preset_by_name(const PresetBundle& bundle, Preset::Type type, const std::string& name)
{
    const PresetCollection& collection = type == Preset::TYPE_FILAMENT ? static_cast<const PresetCollection&>(bundle.filaments) :
                                         type == Preset::TYPE_PRINTER  ? static_cast<const PresetCollection&>(bundle.printers) :
                                                                         static_cast<const PresetCollection&>(bundle.prints);
    const Preset* preset = collection.find_preset(name, false);
    if (preset == nullptr || preset->is_default)
        return nullptr;
    const auto& slots  = bundle.filament_presets;
    const bool  in_use = collection.get_selected_preset_name() == preset->name ||
                        (type == Preset::TYPE_FILAMENT && std::find(slots.begin(), slots.end(), preset->name) != slots.end());
    return preset->is_visible || in_use ? preset : nullptr;
}

} // namespace Slic3r::GUI::JusPrin::Printers
