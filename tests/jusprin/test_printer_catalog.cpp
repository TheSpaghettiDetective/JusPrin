// Contract test for kVendorHiddenFromPrinterFlow: it names a real vendor
// bundle in the packaged profiles, so the constant is proven against the
// actual data rather than a fixture that would still pass if the vendor
// folder were renamed or removed. OrcaPrinterBackend applies the same
// constant to build the agent's catalogue (PrinterConversation) and, later,
// the in-panel browse list; it cannot be linked here without wx, so this
// test stands in for it at the data layer both read.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"

#include <algorithm>
#include <string>

using namespace Slic3r::GUI::JusPrin::PrinterSetup;

TEST_CASE("the vendor kept out of the printer-identification flow is a real bundle", "[printer-catalog]")
{
    const PrinterCatalog catalog = PrinterCatalog::load(std::string(JUSPRIN_SOURCE_DIR) + "/resources");
    const auto&           all    = catalog.candidates();

    // Present in the raw data: OrcaSlicer's own printer wizard and preset
    // lists read the profile files directly, never through this constant,
    // so they are unaffected by hiding it from the identification flow.
    const bool present = std::any_of(all.begin(), all.end(),
                                     [](const PrinterCandidate& candidate) { return candidate.vendor_id == kVendorHiddenFromPrinterFlow; });
    CHECK(present);

    // The same predicate the identification flow applies actually removes
    // something -- catching a typo in the vendor id, or the folder having
    // been renamed, rather than the filter silently doing nothing.
    const auto kept = std::count_if(all.begin(), all.end(),
                                    [](const PrinterCandidate& candidate) { return candidate.vendor_id != kVendorHiddenFromPrinterFlow; });
    CHECK(static_cast<std::size_t>(kept) < all.size());
}
