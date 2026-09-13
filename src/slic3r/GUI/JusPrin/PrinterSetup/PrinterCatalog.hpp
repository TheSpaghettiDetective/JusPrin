#pragma once

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

    // Keeps provider context bounded. Text evidence gets the best lexical
    // matches (including their nozzle variants); image-only evidence gets one
    // default variant per model because a photo cannot establish nozzle size.
    std::vector<const PrinterCandidate*> recognition_choices(const PrinterEvidence& evidence) const;

private:
    std::vector<PrinterCandidate> m_candidates;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
