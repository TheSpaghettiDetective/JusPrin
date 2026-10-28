// The header chip's "from the file" rule.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Printers/PrinterOrigin.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::JusPrin::Printers;

TEST_CASE("only a printer the person set up is not from the file", "[printers][origin]")
{
    Preset named(Preset::TYPE_PRINTER, "Bambu Lab A1 mini 2");
    CHECK_FALSE(printer_from_file(named));

    // What OrcaSlicer selects when a 3MF names a P1S: its system profile.
    Preset system(Preset::TYPE_PRINTER, "Bambu Lab P1S 0.4 nozzle");
    system.is_system = true;
    CHECK(printer_from_file(system));

    Preset embedded(Preset::TYPE_PRINTER, "Bambu Lab P1S 0.4 nozzle(project)");
    embedded.is_project_embedded = true;
    CHECK(printer_from_file(embedded));

    // A user profile the person hid is not one of their printers either.
    Preset hidden(Preset::TYPE_PRINTER, "Old printer");
    hidden.is_visible = false;
    CHECK(printer_from_file(hidden));

    // OrcaSlicer's placeholder is no printer, so there is nothing to mark.
    Preset placeholder(Preset::TYPE_PRINTER, "- default FFF -", true);
    CHECK_FALSE(printer_from_file(placeholder));
}
