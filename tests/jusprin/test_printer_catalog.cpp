#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "../jusprin_support/CommaLocale.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterBackend.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"

using namespace Slic3r::GUI::JusPrin::PrinterSetup;

namespace {

const CatalogPrinter& printer(const std::vector<CatalogPrinter>& printers, const std::string& id)
{
    const auto found = std::find_if(printers.begin(), printers.end(), [&id](const CatalogPrinter& p) { return p.id == id; });
    REQUIRE(found != printers.end());
    return *found;
}

} // namespace

// Profiles write sizes as "0.4" whatever language the app runs in, and the
// app runs with the C library set to the person's language, where "0.4"
// reads as 0 to a locale-aware parser.
TEST_CASE("printer nozzle sizes and bed sizes read the same in a language that writes 0,4", "[PrinterCatalog]")
{
    CommaLocale comma;
    if (!comma.active())
        SKIP("No comma-decimal locale is installed on this machine.");

    const std::vector<CatalogPrinter> printers = PrinterCatalog::load(std::string(JUSPRIN_SOURCE_DIR) + "/resources").panel_printers();

    // Its profiles declare 0.25, 0.4, 0.6 and 0.8 mm.
    const CatalogPrinter& creator = printer(printers, "Flashforge/Flashforge Creator 5 Pro");
    const std::vector<double> declared{0.25, 0.4, 0.6, 0.8};
    REQUIRE(creator.nozzles.size() == declared.size());
    for (std::size_t i = 0; i < declared.size(); ++i)
        CHECK_THAT(creator.nozzles[i], Catch::Matchers::WithinAbs(declared[i], 1e-9));

    const auto without_sizes = std::count_if(printers.begin(), printers.end(), [](const CatalogPrinter& p) { return p.nozzles.empty(); });
    CHECK(without_sizes == 0);

    // Its printable area runs to 233.5 x 228 mm.
    CHECK(printer(printers, "OpenEYE/OpenEYE Peacock V2").build_volume == "233.5 × 228 × 200 mm");
}
