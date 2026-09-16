// Contract tests for named-printer naming: the rules a printer's name, which
// is its Orca profile name, has to obey, and the name a new printer gets.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Printers/PrinterNames.hpp"

#include <set>
#include <string>

using namespace Slic3r::GUI::JusPrin::Printers;

namespace {

NamePredicate in(std::set<std::string> names)
{
    return [names = std::move(names)](const std::string& name) { return names.count(name) > 0; };
}

const NamePredicate nothing = [](const std::string&) { return false; };

} // namespace

TEST_CASE("a printer name follows Orca's profile-name rules", "[printer-names]")
{
    CHECK(check_printer_name("Garage X1C", nothing, nothing) == NameProblem::None);
    CHECK(check_printer_name("", nothing, nothing) == NameProblem::Empty);
    CHECK(check_printer_name(" Garage", nothing, nothing) == NameProblem::EdgeSpace);
    CHECK(check_printer_name("Garage ", nothing, nothing) == NameProblem::EdgeSpace);
    for (const char c : std::string(kIllegalNameCharacters))
        CHECK(check_printer_name(std::string("Garage") + c + "X1C", nothing, nothing) == NameProblem::IllegalCharacter);
    CHECK(check_printer_name("Default Printer", in({"Default Printer"}), nothing) == NameProblem::Reserved);
    CHECK(check_printer_name("Office A1", nothing, in({"Office A1"})) == NameProblem::Taken);
}

TEST_CASE("a new printer takes the first free name and never an existing one", "[printer-names]")
{
    CHECK(first_free_printer_name("Bambu Lab X1 Carbon", nothing) == "Bambu Lab X1 Carbon");
    CHECK(first_free_printer_name("Bambu Lab X1 Carbon", in({"Bambu Lab X1 Carbon"})) == "Bambu Lab X1 Carbon (2)");
    CHECK(first_free_printer_name("Bambu Lab X1 Carbon",
                                  in({"Bambu Lab X1 Carbon", "Bambu Lab X1 Carbon (2)", "Bambu Lab X1 Carbon (3)"})) ==
          "Bambu Lab X1 Carbon (4)");
}
