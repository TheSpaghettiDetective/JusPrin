// Which slice-result warnings JusPrin shows: OrcaSlicer's send-dialog rule,
// copied into SliceResultWarnings. Linked against libslic3r, because the
// kinds are OrcaSlicer's.

#include <catch2/catch_all.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceResultWarnings.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <string>

using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

// The ids in GCodeProcessor.hpp's block of slice warning constants, read
// from the file so a constant OrcaSlicer adds is seen without a code change.
std::set<std::string> defined_kinds()
{
    std::ifstream header(std::string(JUSPRIN_SOURCE_DIR) + "/src/libslic3r/GCode/GCodeProcessor.hpp");
    REQUIRE(header.good());
    const std::regex      define(R"(^\s*#define\s+\w+\s+"([^"]+)\"\s*$)");
    std::set<std::string> kinds;
    bool                  in_block = false;
    for (std::string line; std::getline(header, line);) {
        if (line.find("slice warnings enum strings") != std::string::npos) {
            in_block = true;
            continue;
        }
        std::smatch match;
        if (in_block && std::regex_match(line, match, define))
            kinds.insert(match[1]);
        else if (in_block && !kinds.empty())
            break;
    }
    return kinds;
}

} // namespace

TEST_CASE("every slice warning kind OrcaSlicer defines is classified", "[slice-warnings]")
{
    const std::set<std::string> defined = defined_kinds();
    REQUIRE(defined.size() >= 6);
    std::set<std::string> classified;
    for (const SliceResultWarningRule& rule : slice_result_warning_rules()) {
        CAPTURE(rule.kind);
        CHECK(classified.insert(rule.kind).second);
        CHECK(defined.count(rule.kind) == 1);
    }
    // A kind OrcaSlicer added: copy its choice from SelectMachineDialog's
    // loop over the slice result's warnings into the table.
    for (const std::string& kind : defined) {
        CAPTURE(kind);
        CHECK(classified.count(kind) == 1);
    }
}

TEST_CASE("the send dialog's choices", "[slice-warnings]")
{
    CHECK(slice_result_warning_shown(NOZZLE_HRC_CHECKER) == SliceResultWarningShown::Always);
    CHECK(slice_result_warning_shown(NOT_SUPPORT_TRADITIONAL_TIMELAPSE) == SliceResultWarningShown::WhenRecordingTimelapse);
    CHECK(slice_result_warning_shown(SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER) == SliceResultWarningShown::WhenRecordingTimelapse);
    CHECK(slice_result_warning_shown(NOT_GENERATE_TIMELAPSE) == SliceResultWarningShown::Never);
    CHECK(slice_result_warning_shown(BED_TEMP_TOO_HIGH_THAN_FILAMENT) == SliceResultWarningShown::Never);
    CHECK(slice_result_warning_shown(LONG_RETRACTION_WHEN_CUT) == SliceResultWarningShown::Never);
    // A file from a newer OrcaSlicer can carry a kind this one does not know.
    CHECK(slice_result_warning_shown("a_kind_from_a_newer_orcaslicer") == SliceResultWarningShown::Never);
}

TEST_CASE("a result's warnings as shown", "[slice-warnings]")
{
    // The hook on its side with supports, on an A1 mini: bed temperature,
    // then the traditional-timelapse warning stored twice (10018003, 1000C003).
    CHECK(shown_slice_result_warnings({BED_TEMP_TOO_HIGH_THAN_FILAMENT, NOT_SUPPORT_TRADITIONAL_TIMELAPSE,
                                       NOT_SUPPORT_TRADITIONAL_TIMELAPSE}) == std::vector<std::size_t>{1});
    CHECK(shown_slice_result_warnings({BED_TEMP_TOO_HIGH_THAN_FILAMENT}).empty());
    CHECK(shown_slice_result_warnings({NOZZLE_HRC_CHECKER, SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER, NOT_GENERATE_TIMELAPSE}) ==
          std::vector<std::size_t>{0, 1});
    CHECK(shown_slice_result_warnings({}).empty());
}
