// Which slice-result warnings JusPrin shows: OrcaSlicer's send-dialog rule,
// copied into SliceResultWarnings. Linked against libslic3r, because the
// kinds are OrcaSlicer's.

#include <catch2/catch_all.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceResultWarnings.hpp"

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>

using namespace Slic3r::GUI::JusPrin::Workspace;

namespace {

const std::string kGCodeProcessor = std::string(JUSPRIN_SOURCE_DIR) + "/src/libslic3r/GCode/GCodeProcessor";

// GCodeProcessor.hpp's slice warning constants, name to id, read from the
// file so a constant OrcaSlicer adds is seen without a code change. The
// block runs from its marker comment to the first line that is neither a
// constant, blank nor a comment, so a constant added after a blank line or a
// group comment, or with a comment of its own, is still read. A #define in
// the block that is not a quoted string, or a quoted-string #define after the
// block, fails here rather than being skipped.
std::map<std::string, std::string> defined_kinds()
{
    std::ifstream header(kGCodeProcessor + ".hpp");
    REQUIRE(header.good());
    const std::regex constant(R"re(^\s*#define\s+(\w+)\s+"([^"]+)"\s*(//.*|/\*.*\*/\s*)?$)re");
    const std::regex any_define(R"re(^\s*#\s*define\b.*)re");
    const std::regex string_define(R"re(^\s*#\s*define\s+\w+\s+".*)re");
    const std::regex blank_or_comment(R"re(^\s*(//.*|/\*.*\*/\s*)?$)re");
    std::map<std::string, std::string> kinds;
    enum { Before, In, After } where = Before;
    for (std::string line; std::getline(header, line);) {
        if (where == Before) {
            if (line.find("slice warnings enum strings") != std::string::npos)
                where = In;
            continue;
        }
        std::smatch match;
        if (where == In) {
            if (std::regex_match(line, match, constant)) {
                if (!kinds.emplace(match[1].str(), match[2].str()).second)
                    FAIL_CHECK("a slice warning constant defined twice: " << line);
                continue;
            }
            if (std::regex_match(line, any_define))
                FAIL_CHECK("a #define in the slice warning block this reader cannot read; teach it the form: " << line);
            if (std::regex_match(line, blank_or_comment))
                continue;
            where = After;
        }
        // A string constant outside the block may be a kind added elsewhere:
        // classify it and teach this reader where it lives.
        if (std::regex_match(line, string_define))
            FAIL_CHECK("a string constant after the slice warning block: " << line);
    }
    REQUIRE(where != Before);
    return kinds;
}

} // namespace

TEST_CASE("every slice warning kind OrcaSlicer defines is classified", "[slice-warnings]")
{
    std::set<std::string> defined;
    for (const auto& [name, kind] : defined_kinds())
        defined.insert(kind);
    REQUIRE(defined.size() >= 6);
    std::set<std::string> classified;
    for (const SliceResultWarningRule& rule : slice_result_warning_rules()) {
        CAPTURE(rule.kind);
        CHECK(classified.insert(rule.kind).second);
        // A kind the table names that OrcaSlicer no longer defines.
        CHECK(defined.count(rule.kind) == 1);
    }
    // A kind OrcaSlicer added: copy its choice from SelectMachineDialog's
    // loop over the slice result's warnings into the table.
    for (const std::string& kind : defined) {
        CAPTURE(kind);
        CHECK(classified.count(kind) == 1);
    }
}

TEST_CASE("the G-code processor raises only the slice warning kinds the header defines", "[slice-warnings]")
{
    // A kind written as a string at the call site, or named by a constant
    // from somewhere else, would never reach the check above.
    const std::map<std::string, std::string> defined = defined_kinds();
    std::ifstream                            source(kGCodeProcessor + ".cpp");
    REQUIRE(source.good());
    const std::regex assignment(R"re(\bwarning\.msg\s*=\s*([^;]*?)\s*;)re");
    size_t           raised = 0;
    for (std::string line; std::getline(source, line);) {
        std::smatch match;
        if (!std::regex_search(line, match, assignment))
            continue;
        const std::string value = match[1].str();
        CAPTURE(line);
        ++raised;
        CHECK(defined.count(value) == 1);
    }
    // The processor still sets its warnings this way, so the scan saw them.
    CHECK(raised > 0);
}

TEST_CASE("a kind is shown as the table says, and an unknown kind is not shown", "[slice-warnings]")
{
    // The table is the one copy of the send dialog's choices; this checks the
    // lookup against it rather than typing the choices a second time.
    for (const SliceResultWarningRule& rule : slice_result_warning_rules()) {
        CAPTURE(rule.kind);
        CHECK(slice_result_warning_shown(rule.kind) == rule.shown);
    }
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
