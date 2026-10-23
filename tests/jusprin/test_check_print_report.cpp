#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Shell/CheckPrintReportModel.hpp"

using namespace Slic3r::GUI::JusPrin;
namespace W = Slic3r::GUI::JusPrin::Workspace;

namespace {

W::SliceReport clean_report()
{
    W::SliceReport report;
    report.valid              = true;
    report.print_time_seconds = 6120;
    report.filaments.push_back({0, 9400.0, 28.0, 0.84, true});
    report.has_cost   = true;
    report.total_cost = 0.84;
    report.supports   = W::SliceSupports{};
    report.seams      = W::SliceSeams{};
    report.islands    = W::SliceIslands{};
    W::SliceFirstLayer first;
    first.height = 0.2;
    first.objects.push_back({std::nullopt, "Phone stand", 1980.0, true});
    report.first_layer = first;
    return report;
}

std::vector<PrintCheck> section_order(const CheckPrintReportView& view)
{
    std::vector<PrintCheck> order;
    for (const CheckSection& section : view.sections)
        order.push_back(section.check);
    return order;
}

W::SupportContact contact(const std::string& region_id, double z)
{
    W::SupportContact c;
    c.object_name = "Phone stand";
    c.region_id   = region_id;
    c.target      = region_id.empty() ? "a 6 mm hole" : "region " + region_id;
    c.area        = 12.0;
    c.layers      = 30;
    c.at          = {10.0, 10.0, z};
    return c;
}

} // namespace

TEST_CASE("a plate with nothing to flag names every check in the closing line", "[check_print]")
{
    const CheckPrintReportView view = build_check_print_report(clean_report(), {});
    REQUIRE(view.valid);
    // Only the informational first-layer section is drawn.
    REQUIRE(section_order(view) == std::vector<PrintCheck>{PrintCheck::FirstLayer});
    CHECK(view.sections.front().worst == CheckSeverity::None);
    CHECK_FALSE(view.supports_generated);
    CHECK(view.clean == std::vector<PrintCheck>{PrintCheck::Supports, PrintCheck::Seams, PrintCheck::Islands,
                                                PrintCheck::CrossingPaths, PrintCheck::OutsidePlate,
                                                PrintCheck::OrcaWarnings});
}

TEST_CASE("a check slice_report did not run is neither a section nor clean", "[check_print]")
{
    W::SliceReport report = clean_report();
    report.seams.reset();
    report.islands.reset();
    report.supports.reset();
    const CheckPrintReportView view = build_check_print_report(report, {});
    CHECK(view.clean == std::vector<PrintCheck>{PrintCheck::CrossingPaths, PrintCheck::OutsidePlate, PrintCheck::OrcaWarnings});
}

TEST_CASE("supports lead whenever the slice generated any", "[check_print]")
{
    W::SliceReport report = clean_report();
    report.supports->generated = true;
    report.supports->layers    = 96;
    report.supports->objects   = 2;
    // A critical OrcaSlicer warning would otherwise lead.
    report.findings.push_back({"", "Empty layers between 14.2 and 14.6 mm", true, "Phone stand", ""});
    const CheckPrintReportView view = build_check_print_report(report, {});
    REQUIRE(section_order(view) ==
            std::vector<PrintCheck>{PrintCheck::Supports, PrintCheck::OrcaWarnings, PrintCheck::FirstLayer});
    // No contact was found, so the section says the support is clear.
    REQUIRE(view.sections.front().rows.size() == 1);
    CHECK(view.sections.front().rows.front().kind == CheckRowKind::SupportClear);
    CHECK(view.sections.front().rows.front().count == 96);
    CHECK(view.sections.front().support_layers == 96);
    CHECK(view.sections.front().support_objects == 2);
    // Supports were generated, so they are not in the closing line.
    CHECK(view.clean == std::vector<PrintCheck>{PrintCheck::Seams, PrintCheck::Islands, PrintCheck::CrossingPaths,
                                                PrintCheck::OutsidePlate});
}

TEST_CASE("without supports the most consequential finding leads", "[check_print]")
{
    W::SliceReport report = clean_report();
    W::SeamPlacement on_front{"r1", "visible", "Phone stand", 2};
    report.seams->regions.push_back(on_front);
    report.toolpath_outside = true;
    report.findings.push_back({"", "Floating regions", false, "", ""});
    const CheckPrintReportView view = build_check_print_report(report, {});
    // Critical before warning; between the two warnings, OrcaSlicer's own
    // warning is more consequential than a seam on a visible face.
    CHECK(section_order(view) == std::vector<PrintCheck>{PrintCheck::OutsidePlate, PrintCheck::OrcaWarnings,
                                                         PrintCheck::Seams, PrintCheck::FirstLayer});
    CHECK(view.clean == std::vector<PrintCheck>{PrintCheck::Supports, PrintCheck::Islands, PrintCheck::CrossingPaths});
}

TEST_CASE("the ordering rule stands on its own", "[check_print]")
{
    std::vector<CheckSection> sections(4);
    sections[0].check = PrintCheck::FirstLayer;   sections[0].worst = CheckSeverity::None;
    sections[1].check = PrintCheck::Seams;        sections[1].worst = CheckSeverity::Warning;
    sections[2].check = PrintCheck::Supports;     sections[2].worst = CheckSeverity::None;
    sections[3].check = PrintCheck::CrossingPaths; sections[3].worst = CheckSeverity::Critical;

    SECTION("supports generated")
    {
        order_check_sections(sections, true);
        CHECK(sections[0].check == PrintCheck::Supports);
        CHECK(sections[1].check == PrintCheck::CrossingPaths);
        CHECK(sections[2].check == PrintCheck::Seams);
        CHECK(sections[3].check == PrintCheck::FirstLayer);
    }
    SECTION("no supports generated")
    {
        order_check_sections(sections, false);
        CHECK(sections[0].check == PrintCheck::CrossingPaths);
        CHECK(sections[1].check == PrintCheck::Seams);
        // Equal severity: supports are the more consequential of the two.
        CHECK(sections[2].check == PrintCheck::Supports);
        CHECK(sections[3].check == PrintCheck::FirstLayer);
    }
}

TEST_CASE("support in a marked region is critical, in a found hole a warning", "[check_print]")
{
    W::SliceReport report = clean_report();
    report.supports->generated = true;
    report.supports->contacts.push_back(contact("", 6.0));
    report.supports->contacts.push_back(contact("r2", 12.2));
    W::RegionRecord front;
    front.id    = "r2";
    front.label = "front face";
    const CheckPrintReportView view = build_check_print_report(report, {front});
    const CheckSection& supports = view.sections.front();
    REQUIRE(supports.check == PrintCheck::Supports);
    REQUIRE(supports.rows.size() == 2);
    CHECK(supports.worst == CheckSeverity::Critical);
    // The person's region sorts before the hole and carries its name.
    CHECK(supports.rows[0].kind == CheckRowKind::SupportInRegion);
    CHECK(supports.rows[0].severity == CheckSeverity::Critical);
    CHECK(supports.rows[0].region_label == "front face");
    REQUIRE(supports.rows[0].z.has_value());
    CHECK_THAT(*supports.rows[0].z, Catch::Matchers::WithinAbs(12.2, 1e-9));
    CHECK(supports.rows[1].kind == CheckRowKind::SupportInHole);
    CHECK(supports.rows[1].severity == CheckSeverity::Warning);
    CHECK(supports.rows[1].region_label == "a 6 mm hole");
}

TEST_CASE("seams count only on faces that do not want them", "[check_print]")
{
    W::SliceReport report = clean_report();
    report.seams->regions.push_back({"r1", "seam_preferred", "Phone stand", 40});
    report.seams->regions.push_back({"r2", "hidden", "Phone stand", 12});
    report.seams->regions.push_back({"r3", "seam_forbidden", "Phone stand", 0});
    const CheckPrintReportView view = build_check_print_report(report, {});
    CHECK(section_order(view) == std::vector<PrintCheck>{PrintCheck::FirstLayer});
    CHECK(std::count(view.clean.begin(), view.clean.end(), PrintCheck::Seams) == 1);
}

TEST_CASE("islands held by support are not findings", "[check_print]")
{
    W::SliceReport report = clean_report();
    report.islands->items.push_back({std::nullopt, "Phone stand", 8.4, 3.0, true, {0, 0, 8.4}});
    const CheckPrintReportView held = build_check_print_report(report, {});
    CHECK(std::count(held.clean.begin(), held.clean.end(), PrintCheck::Islands) == 1);
    report.islands->items.push_back({std::nullopt, "Cable clip", 4.0, 1.5, false, {0, 0, 4.0}});
    const CheckPrintReportView view = build_check_print_report(report, {});
    REQUIRE(view.sections.front().check == PrintCheck::Islands);
    REQUIRE(view.sections.front().rows.size() == 1);
    CHECK(view.sections.front().rows.front().object == "Cable clip");
}

TEST_CASE("a warning OrcaSlicer holds twice is one row", "[check_print]")
{
    W::SliceReport report = clean_report();
    report.findings.push_back({"", "Empty layers between 10 and 12.2 mm", true, "Slab", ""});
    report.findings.push_back({"", "Empty layers between 10 and 12.2 mm", true, "Slab", ""});
    report.findings.push_back({"", "Empty layers between 10 and 12.2 mm", true, "Post", ""});
    const CheckPrintReportView view = build_check_print_report(report, {});
    REQUIRE(view.sections.front().check == PrintCheck::OrcaWarnings);
    CHECK(view.sections.front().rows.size() == 2);
}

TEST_CASE("a report without a current slice has no sections", "[check_print]")
{
    const CheckPrintReportView view = build_check_print_report(W::SliceReport{}, {});
    CHECK_FALSE(view.valid);
    CHECK(view.sections.empty());
    CHECK(view.clean.empty());
}

TEST_CASE("a height finds the layer that contains it", "[check_print]")
{
    const std::vector<double> tops{0.2, 0.4, 0.6, 0.8};
    CHECK_FALSE(layer_index_at({}, 1.0).has_value());
    CHECK(layer_index_at(tops, 0.0) == std::optional<std::size_t>(0));
    CHECK(layer_index_at(tops, 0.4) == std::optional<std::size_t>(1));
    CHECK(layer_index_at(tops, 0.40000001) == std::optional<std::size_t>(1));
    CHECK(layer_index_at(tops, 0.5) == std::optional<std::size_t>(2));
    CHECK(layer_index_at(tops, 9.0) == std::optional<std::size_t>(3));
}

TEST_CASE("a row reached from the keyboard is scrolled into view", "[check_print]")
{
    // A 500 px panel over 1300 px of content, 16 px kept around a row.
    CHECK(scroll_to_show(100, 140, 0, 500, 16, 1300) == 0);      // already in view
    CHECK(scroll_to_show(1200, 1240, 0, 500, 16, 1300) == 756);  // below: its bottom plus the margin at the panel's foot
    CHECK(scroll_to_show(200, 240, 600, 500, 16, 1300) == 184);  // above: its top less the margin at the panel's head
    CHECK(scroll_to_show(1260, 1300, 0, 500, 16, 1300) == 800);  // never past the end of the content
    CHECK(scroll_to_show(0, 40, 300, 500, 16, 1300) == 0);       // never before its start
    CHECK(scroll_to_show(10, 40, 0, 500, 16, 300) == 0);         // content that fits never scrolls
}

TEST_CASE("the scrollbar thumb shows the part in view", "[check_print]")
{
    // Content that fits: the thumb is the whole track.
    CHECK(scrollbar_thumb(400, 500, 400, 0, 24).length == 400);
    // Half the content in view: half the track, at the top, then at the bottom.
    CHECK(scrollbar_thumb(400, 500, 1000, 0, 24).length == 200);
    CHECK(scrollbar_thumb(400, 500, 1000, 0, 24).offset == 0);
    CHECK(scrollbar_thumb(400, 500, 1000, 500, 24).offset == 200);
    // Very long content keeps a thumb that can still be seen.
    CHECK(scrollbar_thumb(400, 500, 100000, 0, 24).length == 24);
    CHECK(scrollbar_thumb(400, 500, 100000, 99500, 24).offset == 376);
}
