// Contract tests for reading which printer models Orca's setup wizard newly
// enabled: each becomes one named printer, and only those do.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Printers/InstalledModels.hpp"

using namespace Slic3r::GUI::JusPrin::Printers;

TEST_CASE("only a newly enabled model counts as a new printer", "[installed-models]")
{
    const VendorMap before{{"BBL", {{"Bambu Lab A1 mini", {"0.4"}}}}};
    const VendorMap after{
        {"BBL", {{"Bambu Lab A1 mini", {"0.2", "0.4"}}}},                      // a nozzle added: same printer
        {"Custom", {{"Generic Klipper Printer", {"0.2", "0.4", "0.6", "0.8"}}}}, // new model
    };
    const auto installed = newly_installed_models(before, after, {}, {});
    REQUIRE(installed.size() == 1);
    CHECK(installed.front().vendor == "Custom");
    CHECK(installed.front().model == "Generic Klipper Printer");
    CHECK(installed.front().variant == "0.4");
}

TEST_CASE("a new printer starts on the nozzle the wizard selected, else 0.4, else the first", "[installed-models]")
{
    const VendorMap after{{"Custom",
                           {{"Generic Klipper Printer", {"0.2", "0.4", "0.6"}},
                            {"Generic Marlin Printer", {"0.6", "0.8"}}}}};
    const auto installed = newly_installed_models({}, after, "Generic Klipper Printer", "0.6");
    REQUIRE(installed.size() == 2);
    CHECK(installed[0].model == "Generic Klipper Printer");
    CHECK(installed[0].variant == "0.6");
    CHECK(installed[1].model == "Generic Marlin Printer");
    CHECK(installed[1].variant == "0.6");
}

TEST_CASE("a model listed with no variants is not enabled", "[installed-models]")
{
    const VendorMap before{{"Custom", {{"Generic Klipper Printer", {}}}}};
    const VendorMap after{{"Custom", {{"Generic Klipper Printer", {"0.4"}}, {"Generic Marlin Printer", {}}}}};
    const auto installed = newly_installed_models(before, after, {}, {});
    REQUIRE(installed.size() == 1);
    CHECK(installed.front().model == "Generic Klipper Printer");
    CHECK(newly_installed_models(after, after, {}, {}).empty());
}
