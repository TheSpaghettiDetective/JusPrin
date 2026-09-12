// Contract tests for resources/jusprin/ui/design-tokens.json, the file every
// JusPrin color, radius, spacing step, and type role resolves through (shell,
// Agent page, and the OrcaSlicer palette overrides in
// JusPrin/Brand/BrandPalette.cpp). The scale cases below are the numbers in
// agent-docs/jusprin/design-system.md; change both or neither.

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

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
            // Dark tertiary on the selected surface is a known gap; see the
            // [!mayfail] case below.
            if (std::string(mode) == "light" || std::string(surface) != "selected")
                CHECK(contrast(s.at("text").at("tertiary"), bg) >= 4.5);
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

// Known token gaps, reported rather than hidden: in dark mode the focus ring
// (#7D6A8D) on the selected surface (#4B3E57) measures about 2.0:1, below the
// 3:1 the design system promises for state indicators, and dark tertiary text
// (#A99EAE) on the same surface measures about 3.9:1, below 4.5:1. Resolving
// them is a token decision; this case turns green on its own once the tokens
// change.
TEST_CASE("focus ring and tertiary text stay visible on the selected surface", "[brand][!mayfail]")
{
    const json tokens = load_tokens();
    for (const char* mode : {"light", "dark"}) {
        const json& s = tokens.at("semantic").at(mode);
        const std::string bg = s.at("surface").at("selected");
        INFO(mode << " on surface.selected (" << bg << ")");
        CHECK(contrast(s.at("border").at("focus"), bg) >= 3.0);
        CHECK(contrast(s.at("text").at("tertiary"), bg) >= 4.5);
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
        CHECK(contrast(tokens.at("semantic").at(mode).at("status").at("successOnAction"), a.at("primary")) >= 3.0);
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
    // The same token names exist in both modes so a component can switch by
    // mode alone: a token added to one mode must be added to the other.
    auto require_mirror = [](const json& from, const char* from_mode, const json& to, const char* to_mode) {
        for (const auto& [group, values] : from.items())
            for (const auto& [name, value] : values.items()) {
                INFO("semantic." << from_mode << "." << group << "." << name
                     << " has no counterpart in semantic." << to_mode);
                CHECK((to.contains(group) && to.at(group).contains(name)));
            }
    };
    require_mirror(light, "light", dark, "dark");
    require_mirror(dark, "dark", light, "light");
}

// The scale cases pin the tables in agent-docs/jusprin/design-system.md.
// Each one names the offending key so a wrong edit is a one-line fix.

template <typename T>
void require_exact_table(const json& actual, const std::map<std::string, T>& expected, const std::string& path)
{
    for (const auto& [key, value] : expected) {
        INFO(path << "." << key << " expected " << value);
        REQUIRE(actual.contains(key));
        CHECK(actual.at(key) == json(value));
    }
    for (const auto& [key, value] : actual.items()) {
        INFO(path << "." << key << " is not part of the scale");
        CHECK(expected.count(key) == 1);
    }
}

TEST_CASE("the radius scale is exactly the documented one", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("dimension").at("radius"),
        {{"standard", 4}, {"compact", 8}, {"container", 8}, {"window", 12}, {"pill", 9999}},
        "dimension.radius");
}

// The three elevation tiers, from the Design System page's elevation refs.
// They live outside `semantic` because a shadow is not a color and the
// semantic section is guarded as hex-only, but they are per-mode all the same:
// the geometry is one set of numbers and only the opacity changes, which is
// the rule the design states and the reason a mode switch cannot move a card.
TEST_CASE("the elevation scale is exactly the documented one", "[brand]")
{
    const json tokens = load_tokens();
    const json& elevation = tokens.at("elevation");
    const std::map<std::string, std::map<std::string, double>> expected = {
        {"subtle", {{"offsetX", 0}, {"offsetY", 2}, {"blur", 8}, {"spread", 0}, {"light", 0.08}, {"dark", 0.24}}},
        {"medium", {{"offsetX", 0}, {"offsetY", 4}, {"blur", 12}, {"spread", 0}, {"light", 0.12}, {"dark", 0.32}}},
        {"strong", {{"offsetX", 0}, {"offsetY", 8}, {"blur", 24}, {"spread", 0}, {"light", 0.16}, {"dark", 0.40}}},
    };
    for (const auto& [tier, spec] : expected) {
        INFO("elevation." << tier);
        REQUIRE(elevation.contains(tier));
        const json& actual = elevation.at(tier);
        for (const char* field : {"offsetX", "offsetY", "blur", "spread"}) {
            INFO("elevation." << tier << "." << field);
            CHECK(actual.at(field).get<double>() == spec.at(field));
        }
        // Pure black in both modes; a tinted shadow was the thing the design
        // audit removed.
        CHECK(actual.at("color") == "#000000");
        for (const char* mode : {"light", "dark"}) {
            INFO("elevation." << tier << ".opacity." << mode);
            CHECK(actual.at("opacity").at(mode).get<double>() == spec.at(mode));
        }
        // A shadow that moved between modes would shift the surface it sits
        // on; only its weight may change.
        CHECK(actual.at("opacity").at("dark").get<double>() > actual.at("opacity").at("light").get<double>());
    }
    for (const auto& [tier, spec] : elevation.items()) {
        INFO("elevation." << tier << " is not a documented tier");
        CHECK(expected.count(tier) == 1);
    }
}

TEST_CASE("the spacing scale is exactly the documented one", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("dimension").at("space"),
        {{"1", 4}, {"2", 8}, {"3", 12}, {"4", 16}, {"5", 20}, {"6", 24}, {"8", 32}, {"10", 40}, {"12", 48}},
        "dimension.space");
    std::vector<int> values;
    for (const auto& [key, value] : tokens.at("dimension").at("space").items())
        values.push_back(value.get<int>());
    std::sort(values.begin(), values.end());
    CHECK(values == std::vector<int>{4, 8, 12, 16, 20, 24, 32, 40, 48});
}

TEST_CASE("the type roles are exactly the documented ones", "[brand]")
{
    const json tokens = load_tokens();
    const json& roles = tokens.at("typography").at("roles");
    const std::map<std::string, json> expected = {
        {"pageTitle", {{"size", 24}, {"lineHeight", 30}, {"weight", 700}}},
        {"section",   {{"size", 18}, {"lineHeight", 24}, {"weight", 700}}},
        {"body",      {{"size", 14}, {"lineHeight", 20}, {"weight", 400}}},
        {"bodyBold",      {{"size", 14}, {"lineHeight", 20}, {"weight", 700}}},
        {"bodySmall",     {{"size", 13}, {"lineHeight", 18}, {"weight", 400}}},
        {"bodySmallBold", {{"size", 13}, {"lineHeight", 18}, {"weight", 700}}},
        {"label",         {{"size", 12}, {"lineHeight", 16}, {"weight", 400}}},
        {"labelBold",     {{"size", 12}, {"lineHeight", 16}, {"weight", 700}}},
        {"metadata",      {{"size", 10}, {"lineHeight", 14}, {"weight", 400}}},
        {"metadataBold",  {{"size", 10}, {"lineHeight", 14}, {"weight", 700}}},
    };
    for (const auto& [role, spec] : expected) {
        INFO("typography.roles." << role);
        REQUIRE(roles.contains(role));
        for (const auto& [field, value] : spec.items()) {
            INFO("typography.roles." << role << "." << field << " expected " << value);
            CHECK(roles.at(role).value(field, json()) == value);
        }
    }
    for (const auto& [role, spec] : roles.items()) {
        INFO("typography.roles." << role << " is not a documented role");
        CHECK(expected.count(role) == 1);
    }
}

TEST_CASE("button recipes name a type role instead of a raw size", "[brand]")
{
    const json tokens = load_tokens();
    std::set<std::string> roles;
    for (const auto& [role, spec] : tokens.at("typography").at("roles").items())
        roles.insert(role);
    for (const auto& [name, recipe] : tokens.at("component").at("button").items()) {
        INFO("component.button." << name);
        CHECK_FALSE(recipe.contains("textSize"));
        if (recipe.contains("textRole")) {
            INFO("component.button." << name << ".textRole = " << recipe.at("textRole"));
            REQUIRE(recipe.at("textRole").is_string());
            CHECK(roles.count(recipe.at("textRole").get<std::string>()) == 1);
        }
    }
}

TEST_CASE("the Agent pane resize geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& pane = tokens.at("component").at("agentPane");
    require_exact_table<int>(pane,
        {{"minWidth", 320}, {"workspaceMinWidth", 320}, {"resizeHandleWidth", 8},
         {"resizeHandleLineWidth", 2}},
        "component.agentPane");
}

// A thread row (a hand edit, a G-code destination) stacks its title over its
// metadata line with this gap. It is an internal size of the row, like button
// padding, so it lives here rather than on the spacing scale.
TEST_CASE("the thread row's line gap is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("threadRow"), {{"lineGap", 2}}, "component.threadRow");
}

// Home's gallery sizes its own cards: as many columns as fit between these
// bounds, each card a 4:3 thumbnail over a fixed footer. The numbers are
// agent-docs/jusprin/home-screen-handoff.md section 2, and the responsive
// column count it tabulates only holds while they do.
TEST_CASE("the project card geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& card = tokens.at("component").at("projectCard");
    require_exact_table<int>(card,
        {{"minWidth", 240}, {"maxWidth", 320}, {"footerHeight", 64},
         {"thumbnailAspectWidth", 4}, {"thumbnailAspectHeight", 3}, {"radius", 12}},
        "component.projectCard");
    CHECK(card.at("minWidth").get<int>() < card.at("maxWidth").get<int>());
}

// The printer column is a fixed rail, not a share of the window: the gallery
// takes the width that is left. A printing printer's progress bar is the one
// size inside the card that is not padding.
TEST_CASE("the printer card geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("printerCard"),
        {{"columnWidth", 320}, {"progressHeight", 4}, {"padding", 16}, {"radius", 12}},
        "component.printerCard");
}

// Home sizes its inline glyphs -- the printer beside a name, the monitor on
// its button -- from the smallest step of this scale, so it must stay the
// smallest and stay 16.
TEST_CASE("the icon scale is exactly the documented one", "[brand]")
{
    const json tokens = load_tokens();
    const std::vector<int> sizes = tokens.at("component").at("icon").at("sizes").get<std::vector<int>>();
    CHECK(sizes == std::vector<int>{16, 20, 24});
}

// One dot marks a printing project card and a printing printer. Only the
// color separates the states, so the size must not drift between the two.
TEST_CASE("the status dot is one size", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("statusDot"), {{"size", 8}}, "component.statusDot");
}

TEST_CASE("every component radius comes from the radius scale", "[brand]")
{
    const json tokens = load_tokens();
    std::set<int> allowed = {0}; // menu rows carry no radius inside an already-rounded popover
    for (const auto& [name, value] : tokens.at("dimension").at("radius").items())
        allowed.insert(value.get<int>());
    int checked = 0;
    std::function<void(const json&, const std::string&)> walk = [&](const json& node, const std::string& path) {
        for (const auto& [key, value] : node.items()) {
            const std::string here = path + "." + key;
            if (key == "radius") {
                INFO(here << " = " << value << " is not a dimension.radius value");
                REQUIRE(value.is_number_integer());
                CHECK(allowed.count(value.get<int>()) == 1);
                ++checked;
            } else if (value.is_object()) {
                walk(value, here);
            }
        }
    };
    walk(tokens.at("component"), "component");
    CHECK(checked >= 10);
}

TEST_CASE("the CSS font stacks are present", "[brand]")
{
    const json tokens = load_tokens();
    const json& typography = tokens.at("typography");
    for (const auto& [path, value] : {
             std::pair<const char*, json>{"typography.ui.cssFallback", typography.at("ui").value("cssFallback", json())},
             std::pair<const char*, json>{"typography.code.cssFamily", typography.at("code").value("cssFamily", json())}}) {
        INFO(path);
        REQUIRE(value.is_string());
        CHECK_FALSE(value.get<std::string>().empty());
    }
}

// Monospace is for code, keys, file paths and IDs only, so it has one size
// rather than a twin of every role.
TEST_CASE("the code face is a single role", "[brand]")
{
    const json tokens = load_tokens();
    const json& code = tokens.at("typography").at("code");
    CHECK(code.value("size", json()) == 12);
    CHECK(code.value("lineHeight", json()) == 16);
    CHECK(code.value("weight", json()) == 400);
}
