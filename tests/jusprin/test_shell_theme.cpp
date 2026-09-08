// Contract tests for ShellTheme, the fork-owned reader of
// resources/jusprin/ui/design-tokens.json. The metrics and text roles it
// exposes are what the shell widgets size and type themselves with, so the
// real token file is parsed here and a malformed one must be refused.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/init.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>

using namespace Slic3r::GUI::JusPrin;
namespace fs = std::filesystem;

namespace {

const std::string packaged_token_path = std::string(JUSPRIN_SOURCE_DIR) + "/resources/jusprin/ui/design-tokens.json";

ShellTheme load_packaged_theme()
{
    Slic3r::set_resources_dir(std::string(JUSPRIN_SOURCE_DIR) + "/resources");
    return ShellTheme::load_from_resources();
}

// The packaged token text with one substring swapped, to break exactly one thing.
std::string packaged_tokens_with(const std::string& from, const std::string& to)
{
    std::ifstream packaged(packaged_token_path);
    REQUIRE(packaged.is_open());
    std::string tokens((std::istreambuf_iterator<char>(packaged)), std::istreambuf_iterator<char>());
    const auto at = tokens.find(from);
    REQUIRE(at != std::string::npos);
    return tokens.replace(at, from.size(), to);
}

// A resources directory holding one token file with the given content, so a
// parse failure can be provoked without touching the packaged file.
class ScratchResources
{
public:
    explicit ScratchResources(const std::string& tokens)
        : m_dir(fs::temp_directory_path() / ("jusprin-shell-theme-" + std::to_string(std::random_device{}())))
    {
        fs::create_directories(m_dir / "jusprin" / "ui");
        std::ofstream(m_dir / "jusprin" / "ui" / "design-tokens.json") << tokens;
        Slic3r::set_resources_dir(m_dir.string());
    }
    ~ScratchResources() { fs::remove_all(m_dir); }

private:
    fs::path m_dir;
};

} // namespace

TEST_CASE("the packaged token file yields the documented metrics", "[shell][theme]")
{
    const ShellTheme theme = load_packaged_theme();
    const ShellMetrics& m = theme.metrics();

    CHECK(m.radius_standard == 4);
    CHECK(m.radius_compact == 8);
    CHECK(m.radius_container == 8);
    CHECK(m.radius_window == 12);
    CHECK(m.radius_pill == 9999);
    CHECK(m.space_1 == 4);
    CHECK(m.space_12 == 48);

    CHECK(m.chip.height == 26);
    CHECK(m.chip.radius == 4);
    CHECK(m.menu_row.height == 32);
    CHECK(m.menu_row.radius == 4);
    CHECK(m.menu_row.side_inset == 4);
    CHECK(m.popover.padding_y == 4);
    CHECK(m.popover.row_gap == 0);
    CHECK(m.popover.radius == 8);
    CHECK(m.status_row.height == 34);
    CHECK(m.swatch.size == 24);

    CHECK(m.button.icon.width == 26);
    CHECK(m.button.icon.icon_size == 16);
    CHECK_FALSE(m.button.icon.text_role.has_value());
    CHECK(m.button.window.text_role == TextRole::Label);
    CHECK(m.button.compact.text_role == TextRole::Metadata);
    CHECK(m.button.expanded.min_height == 32);
}

TEST_CASE("the packaged token file yields the documented text roles", "[shell][theme]")
{
    const ShellTheme theme = load_packaged_theme();

    CHECK(theme.type_style(TextRole::PageTitle).size == 24);
    CHECK(theme.type_style(TextRole::Section).weight == 700);
    CHECK(theme.type_style(TextRole::Body).size == 14);
    CHECK(theme.type_style(TextRole::Body).line_height == 20);
    CHECK(theme.type_style(TextRole::Label).weight == 400);
    CHECK(theme.type_style(TextRole::LabelBold).weight == 700);
    CHECK(theme.type_style(TextRole::BodyBold).size == 14);
    CHECK(theme.type_style(TextRole::Metadata).size == 10);
}

TEST_CASE("a token file without the component section is refused", "[shell][theme]")
{
    const ScratchResources resources(packaged_tokens_with("\"component\": {", "\"components\": {"));
    CHECK_THROWS_AS(ShellTheme::load_from_resources(), std::runtime_error);
}

TEST_CASE("a token file with a fractional DIP value is refused", "[shell][theme]")
{
    const ScratchResources resources(packaged_tokens_with("\"chip\": {\n      \"height\": 26,", "\"chip\": {\n      \"height\": 26.5,"));
    CHECK_THROWS_WITH(ShellTheme::load_from_resources(), Catch::Matchers::ContainsSubstring("component.chip.height"));
}

TEST_CASE("fonts carry the role weight and are built once", "[shell][theme][wx]")
{
    wxInitializer wx;
    REQUIRE(wx.IsOk());
    const ShellTheme theme = load_packaged_theme();

    const wxFont& label = theme.font(TextRole::LabelBold);
    REQUIRE(label.IsOk());
    CHECK(label.GetWeight() == wxFONTWEIGHT_BOLD);

    const wxFont& body = theme.font(TextRole::Body);
    REQUIRE(body.IsOk());
    CHECK(body.GetWeight() == wxFONTWEIGHT_NORMAL);

    const wxFont& technical = theme.mono_font(TextRole::Label);
    REQUIRE(technical.IsOk());
    CHECK(technical.IsFixedWidth());
    CHECK(technical.GetWeight() == wxFONTWEIGHT_NORMAL);
    CHECK(technical.GetPointSize() == theme.font(TextRole::Label).GetPointSize());
    CHECK(&theme.mono_font(TextRole::Label) == &technical);

    CHECK(&theme.font(TextRole::Body) == &body);
}
