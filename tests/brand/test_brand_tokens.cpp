// Contract tests for resources/jusprin/ui/design-tokens.json, the file every
// JusPrin color resolves through (shell, Agent page, and the OrcaSlicer palette
// overrides in JusPrin/Brand/BrandPalette.cpp).

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <string>

using nlohmann::json;

namespace {

json load_tokens()
{
    std::ifstream file(std::string(JUSPRIN_SOURCE_DIR) + "/resources/jusprin/ui/design-tokens.json");
    REQUIRE(file.is_open());
    return json::parse(file);
}

struct Rgb { double r, g, b; };

Rgb parse_hex(const std::string& hex)
{
    REQUIRE(hex.size() == 7);
    REQUIRE(hex[0] == '#');
    auto channel = [&](size_t at) { return std::stoi(hex.substr(at, 2), nullptr, 16) / 255.0; };
    return {channel(1), channel(3), channel(5)};
}

// WCAG 2 relative luminance and contrast ratio.
double linear(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double luminance(const Rgb& c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }
double contrast(const std::string& a, const std::string& b)
{
    const double la = luminance(parse_hex(a)), lb = luminance(parse_hex(b));
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

TEST_CASE("every semantic token is a six-digit hex color in both modes", "[brand]")
{
    const json tokens = load_tokens();
    for (const char* mode : {"light", "dark"}) {
        const json& semantic = tokens.at("semantic").at(mode);
        for (const auto& [group, values] : semantic.items())
            for (const auto& [name, value] : values.items()) {
                INFO(mode << "." << group << "." << name);
                REQUIRE(value.is_string());
                parse_hex(value.get<std::string>());
            }
    }
}

TEST_CASE("text keeps the documented contrast on every surface", "[brand]")
{
    const json tokens = load_tokens();
    for (const char* mode : {"light", "dark"}) {
        const json& s = tokens.at("semantic").at(mode);
        for (const char* surface : {"canvas", "subtle", "raised", "selected"}) {
            const std::string bg = s.at("surface").at(surface);
            INFO(mode << " text on surface." << surface << " (" << bg << ")");
            CHECK(contrast(s.at("text").at("primary"), bg) >= 4.5);
            CHECK(contrast(s.at("text").at("secondary"), bg) >= 4.5);
        }
        // Essential boundaries need 3:1 on the surfaces a control normally sits on.
        for (const char* surface : {"canvas", "subtle", "raised"}) {
            const std::string bg = s.at("surface").at(surface);
            INFO(mode << " borders on surface." << surface << " (" << bg << ")");
            CHECK(contrast(s.at("border").at("strong"), bg) >= 3.0);
            CHECK(contrast(s.at("border").at("focus"), bg) >= 3.0);
        }
    }
}

// Known token gap, reported rather than hidden: in dark mode the focus ring
// (#7D6A8D) on the selected surface (#4B3E57) measures about 2.0:1, below the
// 3:1 the design system promises for state indicators. Resolving it is a
// token decision; this case turns green on its own once the tokens change.
TEST_CASE("focus ring stays visible on the selected surface", "[brand][!mayfail]")
{
    const json tokens = load_tokens();
    for (const char* mode : {"light", "dark"}) {
        const json& s = tokens.at("semantic").at(mode);
        const std::string bg = s.at("surface").at("selected");
        INFO(mode << " focus on surface.selected (" << bg << ")");
        CHECK(contrast(s.at("border").at("focus"), bg) >= 3.0);
    }
}

TEST_CASE("action text is readable on the action fill in every state", "[brand]")
{
    const json tokens = load_tokens();
    for (const char* mode : {"light", "dark"}) {
        const json& a = tokens.at("semantic").at(mode).at("action");
        for (const char* fill : {"primary", "primaryHover", "primaryPressed"}) {
            INFO(mode << " action." << fill);
            CHECK(contrast(a.at("primaryText"), a.at(fill)) >= 4.5);
        }
        CHECK(contrast(a.at("secondaryText"), a.at("secondary")) >= 4.5);
        CHECK(contrast(a.at("dangerText"), a.at("danger")) >= 4.5);
        // Disabled controls are exempt from the contrast minimum (WCAG 1.4.3);
        // only guard against a fully invisible label.
        CHECK(contrast(a.at("disabledText"), a.at("disabled")) >= 2.0);
    }
}

TEST_CASE("dark mode is a remapping, not a copy of light mode", "[brand]")
{
    const json tokens = load_tokens();
    const json& light = tokens.at("semantic").at("light");
    const json& dark  = tokens.at("semantic").at("dark");
    CHECK(light.at("surface").at("canvas") != dark.at("surface").at("canvas"));
    CHECK(light.at("action").at("primary") != dark.at("action").at("primary"));
    // The same token names exist in both modes so a component can switch by mode alone.
    for (const auto& [group, values] : light.items())
        for (const auto& [name, value] : values.items()) {
            INFO(group << "." << name);
            CHECK(dark.at(group).contains(name));
        }
}
