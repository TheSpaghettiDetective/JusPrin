// decimal_text writes the numbers JusPrin's own windows show (nozzle sizes,
// lengths, costs) from digits, with the separator the app's language uses.
// The C library's printf cannot be trusted for this in the app: its decimal
// point depends on the thread's locale, and the Home printer cards came out
// "0.4 mm" in German while OrcaSlicer's settings fields wrote "0,4".

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Support/DecimalText.hpp"

using Slic3r::GUI::JusPrin::decimal_text;

TEST_CASE("a decimal keeps its language's separator and drops trailing zeros", "[jusprin][locale]")
{
    CHECK(decimal_text(0.4, 0, 2, ".") == "0.4");
    CHECK(decimal_text(0.4, 0, 2, ",") == "0,4");
    CHECK(decimal_text(0.25, 0, 2, ",") == "0,25");
    CHECK(decimal_text(1.0, 0, 2, ",") == "1");
    CHECK(decimal_text(1.0, 1, 2, ",") == "1,0");
    CHECK(decimal_text(12.35, 1, 1, ",") == "12,4");
    CHECK(decimal_text(3.0, 2, 2, ".") == "3.00");
    CHECK(decimal_text(-0.5, 1, 1, ",") == "-0,5");
    CHECK(decimal_text(-0.01, 0, 1, ".") == "0");
    CHECK(decimal_text(7.9, 0, 0, ",") == "8");
}
