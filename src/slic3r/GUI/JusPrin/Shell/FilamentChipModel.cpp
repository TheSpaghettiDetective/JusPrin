#include "FilamentChipModel.hpp"

#include <algorithm>
#include <set>

namespace Slic3r::GUI::JusPrin {

FilamentChipModel describe_filament_chip(const std::vector<ChipSlot>& slots)
{
    FilamentChipModel model;
    const bool nothing_used = std::none_of(slots.begin(), slots.end(), [](const ChipSlot& slot) { return slot.used; });
    const auto in_use       = [&](const ChipSlot& slot) { return nothing_used || slot.used; };

    std::set<std::string> filaments;
    std::size_t           used = 0;
    for (const ChipSlot& slot : slots)
        if (in_use(slot)) {
            filaments.insert(slot.preset);
            ++used;
        }
    model.filaments = filaments.size();
    if (model.filaments == 1)
        for (const ChipSlot& slot : slots)
            if (in_use(slot)) {
                model.name = slot.name;
                break;
            }

    // Past the full-size count, unused slots give up their dots first; the
    // slots the plate prints with always keep theirs.
    const bool fold = slots.size() > kFullSizeDots;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const bool active = in_use(slots[index]);
        if (fold && !active) {
            ++model.folded;
            continue;
        }
        model.dots.push_back({index, slots[index].colour, !active});
    }
    model.shrunk = used > kFullSizeDots;
    return model;
}

} // namespace Slic3r::GUI::JusPrin
