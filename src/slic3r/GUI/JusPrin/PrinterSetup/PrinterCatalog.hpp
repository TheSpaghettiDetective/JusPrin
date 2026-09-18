#pragma once

#include "PrinterSetupTypes.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// A vendor bundle kept out of the printer-identification flow only: the
// agent's whole-catalog prompt (PrinterConversation) and the in-panel
// "Browse the full list". resources/profiles/OrcaArena.json ships one
// machine model (Orca Arena X1 Carbon) and about 40 filament and process
// files with nothing in them marking a difference from an ordinary vendor;
// its purpose is undocumented. Everywhere else -- OrcaSlicer's own printer
// wizard, its preset lists, an already-installed printer of this vendor --
// reads the profile files directly and never sees this constant, so it
// stays unaffected.
inline constexpr std::string_view kVendorHiddenFromPrinterFlow = "OrcaArena";

class PrinterCatalog
{
public:
    // Reads the packaged profile indexes. It never reads or changes the user's
    // installed presets, which makes opening the flow side-effect free.
    static PrinterCatalog load(const std::string& resources_directory);

    explicit PrinterCatalog(std::vector<PrinterCandidate> candidates = {});

    const std::vector<PrinterCandidate>& candidates() const { return m_candidates; }
    const PrinterCandidate* find(const std::string& id) const;
    const PrinterCandidate* find_device_model(const std::string& device_model_id) const;

    // One entry per printer model, which is what recognition is asked to name:
    // the 0.4 mm variant where the model ships one, otherwise its first.
    std::vector<const PrinterCandidate*> models() const;

private:
    std::vector<PrinterCandidate> m_candidates;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
