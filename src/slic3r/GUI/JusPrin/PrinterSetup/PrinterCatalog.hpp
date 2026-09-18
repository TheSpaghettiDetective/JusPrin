#pragma once

#include "PrinterBackend.hpp"
#include "PrinterSetupTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

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

    // The printers the printer panel offers: one per model, with the nozzle
    // sizes it ships and each size's default filament, in catalogue order.
    // The Orca Arena bundle is left out (it stays in OrcaSlicer's wizard).
    std::vector<CatalogPrinter> panel_printers() const;

private:
    std::vector<PrinterCandidate> m_candidates;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
