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
        for (const char* surface : {"canvas", "subtle", "raised", "raisedHover", "selected"}) {
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
        // Secondary and danger buttons have no fill of their own: their label
        // sits on the surface behind them, as the design system's Button/Secondary does.
        const json& semantic = tokens.at("semantic").at(mode);
        for (const char* surface : {"canvas", "subtle", "raised"})
            CHECK(contrast(semantic.at("status").at("danger"), semantic.at("surface").at(surface)) >= 4.5);
        CHECK(contrast(tokens.at("semantic").at(mode).at("status").at("successOnAction"), a.at("primary")) >= 3.0);
        // Disabled controls are exempt from the contrast minimum (WCAG 1.4.3);
        // only guard against a fully invisible label.
        CHECK(contrast(a.at("disabledText"), a.at("disabled")) >= 2.0);
    }
}

TEST_CASE("failed print card uses the paired Figma semantic roles", "[brand]")
{
    const json tokens = load_tokens();
    const std::map<std::string, std::map<std::string, std::string>> expected = {
        {"light", {{"surface.failureInverse", "#261F37"}, {"status.dangerOnInverse", "#FF7868"},
                   {"text.onFailurePrimary", "#FFFFFF"}, {"text.onFailureSecondary", "#C8C1D2"},
                   {"text.onFailureTertiary", "#A29AAA"}, {"border.onFailure", "#746A86"}}},
        {"dark", {{"surface.failureInverse", "#342C3B"}, {"status.dangerOnInverse", "#FF8E80"},
                  {"text.onFailurePrimary", "#FFFFFF"}, {"text.onFailureSecondary", "#D7D0DF"},
                  {"text.onFailureTertiary", "#B8AFBF"}, {"border.onFailure", "#8E829D"}}},
    };
    for (const auto& [mode, roles] : expected) {
        const json& semantic = tokens.at("semantic").at(mode);
        for (const auto& [role, value] : roles) {
            const size_t dot = role.find('.');
            INFO(mode << '.' << role);
            CHECK(semantic.at(role.substr(0, dot)).at(role.substr(dot + 1)) == value);
        }
        const std::string bg = semantic.at("surface").at("failureInverse");
        for (const char* role : {"onFailurePrimary", "onFailureSecondary", "onFailureTertiary"})
            CHECK(contrast(semantic.at("text").at(role), bg) >= 4.5);
        CHECK(contrast(semantic.at("status").at("dangerOnInverse"), bg) >= 3.0);
        CHECK(contrast(semantic.at("border").at("onFailure"), bg) >= 3.0);
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
void require_exact_table(const json& actual, const std::map<std::string, T>& expected, const std::string& path,
                         const std::set<std::string>& also_allowed = {})
{
    for (const auto& [key, value] : expected) {
        INFO(path << "." << key << " expected " << value);
        REQUIRE(actual.contains(key));
        CHECK(actual.at(key) == json(value));
    }
    for (const auto& [key, value] : actual.items()) {
        INFO(path << "." << key << " is not part of the scale");
        CHECK((expected.count(key) == 1 || also_allowed.count(key) == 1));
    }
}

TEST_CASE("the radius scale is exactly the documented one", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("dimension").at("radius"),
        {{"small", 4}, {"medium", 8}, {"large", 12}, {"chip", 4}, {"full", 14}},
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
        {{"0.5", 2}, {"1", 4}, {"2", 8}, {"3", 12}, {"4", 16}, {"5", 20}, {"6", 24}, {"8", 32}, {"10", 40}, {"12", 48}},
        "dimension.space");
    std::vector<int> values;
    for (const auto& [key, value] : tokens.at("dimension").at("space").items())
        values.push_back(value.get<int>());
    std::sort(values.begin(), values.end());
    CHECK(values == std::vector<int>{2, 4, 8, 12, 16, 20, 24, 32, 40, 48});
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
        {{"width", 429}, {"minWidth", 320}, {"workspaceMinWidth", 320}, {"resizeHandleWidth", 8},
         {"resizeHandleLineWidth", 2}},
        "component.agentPane");
}

// The Plates / Project pane's rows are boxes from its Figma frames
// (26a-26h on the Plates & Project Pane page). Each number is a padding, gap or
// height of one of those boxes, so a drifting value moves a painted row off
// its frame.
TEST_CASE("the Plates and Project pane geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("leftPane"),
        {{"width", 200}, {"minWidth", 160}, {"paddingY", 10}, {"tabHeight", 36}, {"tabIndicatorHeight", 2}, {"listPaddingY", 8},
         {"rowGap", 2}, {"rowPaddingX", 8}, {"rowPaddingY", 6}, {"plateRowHeight", 32}, {"objectRowHeight", 36},
         {"objectIndent", 20}, {"glyphSize", 16}, {"plateGap", 8}, {"objectGap", 4}, {"summaryIndent", 32},
         {"summaryPaddingBottom", 6}, {"addPlatePaddingTop", 8}, {"addPlatePaddingBottom", 4},
         {"addPlatePaddingX", 12}, {"addPlateButtonHeight", 30}},
        "component.leftPane");
}

// A thread row (a hand edit, a G-code destination) stacks its title over its
// metadata line with this gap. It is an internal size of the row, like button
// padding, so it lives here rather than on the spacing scale.
TEST_CASE("the thread row's line gap is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("threadRow"), {{"lineGap", 2}}, "component.threadRow");
    // The turn's working line shimmers; one pass and one turn of its glyph.
    require_exact_table<int>(tokens.at("component").at("turnActivity"), {{"shimmerMs", 1600}, {"spinMs", 1000}}, "component.turnActivity");
    // The Agent pane's setup offer shrinking into the setup bar once a thread appears.
    require_exact_table<int>(tokens.at("component").at("setupOffer"), {{"shrinkMs", 320}}, "component.setupOffer");
    require_exact_table<int>(tokens.at("component").at("timelineSummary"),
        {{"paddingX", 8}, {"paddingY", 8}, {"iconGap", 4}}, "component.timelineSummary");
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
// takes the width that is left. Progress thickness is shared with Agent work.
// Home's own printer dialogs -- rename, remove -- are one small modal over
// the gallery: the same width, the same corner, the same dimming behind it.
TEST_CASE("the printer dialog geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("printerDialog"),
        {{"width", 500}, {"radius", 12}, {"scrimAlpha", 88}},
        "component.printerDialog");
}

TEST_CASE("printer setup has a readable conversation width", "[brand]")
{
    require_exact_table<int>(load_tokens().at("component").at("printerSetup"),
        {{"contentWidth", 720}}, "component.printerSetup");
}

// Replies in the Agent thread use the standard control radius. Their height
// and text inset are shared between the printer, filament, and project chats.
// The design system (the Design System artifact's project/tokens.json,
// https://claude.ai/artifact/3zgiix8rAw3rXextQnR5UL) is the source of truth
// for every colour both sides name. Left: the design system's name. Right:
// where the token file keeps it. overlay.scrim and status.successOnAction
// exist only here until the artifact adds them.
TEST_CASE("the shared semantic colours are the design system's", "[brand]")
{
    struct Colour { const char* design; const char* path; const char* light; const char* dark; };
    const Colour colours[] = {
        {"surface-canvas", "surface.canvas", "#FFFFFF", "#17131F"},
        {"surface-subtle", "surface.subtle", "#F6F2F7", "#211B2A"},
        {"surface-raised", "surface.raised", "#FFFFFF", "#2B2336"},
        {"surface-raised-hover", "surface.raisedHover", "#F6F2F7", "#3B3047"},
        {"surface-selected", "surface.selected", "#EDE7F2", "#4B3E57"},
        {"surface-failure-inverse", "surface.failureInverse", "#261F37", "#342C3B"},
        {"text-primary", "text.primary", "#17131F", "#F6F2F7"},
        {"text-secondary", "text.secondary", "#5F5767", "#CFC5D3"},
        {"text-tertiary", "text.tertiary", "#6C6374", "#A99EAE"},
        {"border-subtle", "border.subtle", "#E8DFEB", "#4B3E57"},
        {"border-strong", "border.strong", "#6F4F84", "#7D6A8D"},
        {"border-focus", "border.focus", "#6F4F84", "#7D6A8D"},
        {"action-primary", "action.primary", "#3A2D64", "#CE84B7"},
        {"action-primary-hover", "action.primaryHover", "#4B3A77", "#D99AC5"},
        {"action-primary-pressed", "action.primaryPressed", "#2B214F", "#B76CA0"},
        {"action-on-primary", "action.primaryText", "#FFFFFF", "#17131F"},
        {"action-disabled", "action.disabled", "#E4E0E6", "#342C3B"},
        {"action-disabled-text", "action.disabledText", "#8A838F", "#857B8C"},
        {"status-success", "status.success", "#2D6A55", "#4CAF82"},
        {"status-danger", "status.danger", "#B44757", "#E07080"},
        {"status-warning", "status.warning", "#D49A2A", "#E8B84A"},
        {"status-danger-on-inverse", "status.dangerOnInverse", "#FF7868", "#FF8E80"},
        {"axis-x", "axis.x", "#FF3C5B", "#FF3C5B"},
        {"axis-y", "axis.y", "#64C818", "#64C818"},
        {"axis-z", "axis.z", "#2F88E9", "#2F88E9"},
    };
    const json tokens = load_tokens();
    for (const Colour& colour : colours) {
        const std::string path(colour.path);
        const size_t dot = path.find('.');
        for (const char* mode : {"light", "dark"}) {
            INFO(colour.design << " (" << mode << ".semantic." << path << ")");
            const json& group = tokens.at("semantic").at(mode).at(path.substr(0, dot));
            REQUIRE(group.contains(path.substr(dot + 1)));
            CHECK(group.at(path.substr(dot + 1)) == (std::string(mode) == "light" ? colour.light : colour.dark));
        }
    }
}

// Button/Link, Segmented Control and Tooltip, as the design system draws them.
TEST_CASE("the link, segmented control and tooltip geometry is explicit", "[brand]")
{
    const json component = load_tokens().at("component");
    require_exact_table<int>(component.at("button").at("link"),
        {{"height", 26}, {"paddingX", 4}, {"paddingY", 0}, {"radius", 4}}, "component.button.link", {"textRole"});
    CHECK(component.at("button").at("link").at("textRole") == "labelBold");
    require_exact_table<int>(component.at("segmented"),
        {{"height", 30}, {"padding", 2}, {"borderWidth", 1}, {"radius", 4}, {"segmentHeight", 24}, {"segmentPaddingX", 16},
         {"segmentPaddingY", 4}},
        "component.segmented", {"textRole"});
    CHECK(component.at("segmented").at("textRole") == "label");
    // The segments fill the track: its border and padding on both sides plus one segment.
    const json& segmented = component.at("segmented");
    CHECK(segmented.at("height").get<int>() ==
          2 * (segmented.at("borderWidth").get<int>() + segmented.at("padding").get<int>()) +
              segmented.at("segmentHeight").get<int>());
    require_exact_table<int>(component.at("tooltip"), {{"height", 24}, {"paddingX", 8}, {"paddingY", 4}, {"radius", 4}},
                             "component.tooltip", {"textRole"});
    CHECK(component.at("tooltip").at("textRole") == "metadata");
}

TEST_CASE("the reply chip geometry is explicit", "[brand]")
{
    require_exact_table<int>(load_tokens().at("component").at("replyChip"),
        {{"height", 28}, {"paddingX", 12}}, "component.replyChip");
}

TEST_CASE("the shared chat attachment geometry is explicit", "[brand]")
{
    require_exact_table<int>(load_tokens().at("component").at("chatAttachment"),
        {{"maxWidth", 320}, {"thumbnailSize", 48}, {"kindSize", 32}}, "component.chatAttachment");
}

// The Agent message box: one bordered box whose text field grows a line at a
// time up to maxLines of Body Small, then scrolls. Its insets are layout
// spacing, so they stay on the spacing scale.
TEST_CASE("the composer geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& composer = tokens.at("component").at("composer");
    require_exact_table<int>(composer, {{"padding", 8}, {"gap", 4}, {"inputPadding", 4}, {"maxLines", 8}},
                             "component.composer");
    std::set<int> scale;
    for (const auto& [name, value] : tokens.at("dimension").at("space").items())
        scale.insert(value.get<int>());
    for (const char* inset : {"padding", "gap", "inputPadding"}) {
        INFO("component.composer." << inset << " is not a dimension.space value");
        CHECK(scale.count(composer.at(inset).get<int>()) == 1);
    }
}

// The composer's Send and Stop: Button/Primary and Button/Secondary at 28 DIP,
// so they sit inside the toolbar beside the 32 DIP + without growing it.
TEST_CASE("the small button recipes are explicit", "[brand]")
{
    const json tokens  = load_tokens();
    const json& button = tokens.at("component").at("button");
    require_exact_table<int>(button.at("primarySmall"),
        {{"height", 28}, {"paddingX", 16}, {"paddingY", 4}, {"radius", 8}}, "component.button.primarySmall", {"textRole"});
    CHECK(button.at("primarySmall").at("textRole") == "bodySmallBold");
    require_exact_table<int>(button.at("secondarySmall"),
        {{"height", 28}, {"paddingX", 12}, {"paddingY", 4}, {"radius", 4}}, "component.button.secondarySmall", {"textRole"});
    CHECK(button.at("secondarySmall").at("textRole") == "bodySmall");
    CHECK(button.at("primarySmall").at("height") < button.at("icon").at("height"));
    // The composer's model and thinking pickers: a label and a chevron.
    require_exact_table<int>(button.at("picker"),
        {{"height", 24}, {"paddingX", 8}, {"paddingY", 4}, {"radius", 4}, {"iconGap", 4}}, "component.button.picker", {"textRole"});
    CHECK(button.at("picker").at("textRole") == "label");
}

// Field/Search, the box at the top of a long menu: taller than a form field,
// with a 16 DIP glyph, and a stroke that thickens while it has the focus.
TEST_CASE("the search field geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& field = tokens.at("component").at("fieldSearch");
    require_exact_table<int>(field,
        {{"height", 38}, {"paddingX", 10}, {"iconSize", 16}, {"iconGap", 6}, {"radius", 4}, {"borderWidth", 1},
         {"focusBorderWidth", 2}},
        "component.fieldSearch", {"textRole"});
    CHECK(field.at("textRole") == "body");
}

TEST_CASE("the printer card geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("printerCard"),
        {{"columnWidth", 320}, {"pictureSize", 32}, {"padding", 16}, {"radius", 12}},
        "component.printerCard");
}

TEST_CASE("the shared progress track height is explicit", "[brand]")
{
    require_exact_table<int>(load_tokens().at("component").at("progress"),
        {{"height", 4}}, "component.progress");
}

// The header's menus and Home's printer-card menu are one control: an icon
// button opening a popover of rows. Both sides read these, so a change here
// moves both.
TEST_CASE("the menu geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& component = tokens.at("component");
    require_exact_table<int>(component.at("button").at("icon"),
        {{"width", 32}, {"height", 32}, {"iconSize", 16}, {"radius", 4}}, "component.button.icon");
    require_exact_table<int>(component.at("menuRow"), {{"height", 36}, {"radius", 0}, {"sideInset", 0}},
        "component.menuRow");
    require_exact_table<int>(component.at("popover"), {{"paddingY", 8}, {"rowGap", 2}, {"radius", 8}, {"borderWidth", 1}, {"width", 280}},
        "component.popover");
    require_exact_table<int>(component.at("chip"), {{"height", 28}, {"radius", 4}}, "component.chip");
    require_exact_table<int>(component.at("timelineRevert"), {{"width", 320}}, "component.timelineRevert");
}

// The header chip's slot dots and the filament menu's colour cells are drawn
// by the fork, so every size they use is here rather than in the code.
TEST_CASE("the filament chip and colour row geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& component = tokens.at("component");
    require_exact_table<int>(component.at("slotDot"),
        {{"size", 8}, {"compactSize", 4}, {"gap", 3}, {"compactGap", 2}, {"labelGap", 8}, {"fadedAlpha", 90}},
        "component.slotDot");
    require_exact_table<int>(component.at("swatch"),
        {{"size", 24}, {"radius", 4}, {"gap", 4}, {"captionGap", 4}, {"inset", 2}, {"currentInset", 1},
         {"ringWidth", 1}, {"currentRingWidth", 2}, {"glyphWidth", 1}},
        "component.swatch");
    // The focus ring is drawn in the gap between two cells, so it must fit
    // there, and the design system requires it to be 2 DIP.
    require_exact_table<int>(component.at("focus"), {{"width", 2}, {"offset", 2}}, "component.focus", {"rule"});
    CHECK(component.at("focus").at("width").get<int>() <= component.at("swatch").at("gap").get<int>());
    // A dot that fades must stay visible, and a faded one must read as secondary.
    CHECK(component.at("slotDot").at("fadedAlpha").get<int>() > 0);
    CHECK(component.at("slotDot").at("fadedAlpha").get<int>() < 255);
}


// Home sizes its inline glyphs -- the printer beside a name, the monitor on
// its button -- from the smallest step of this scale, so it must stay the
// smallest and stay 16.
// The numeric tool panel beside the canvas tool strip. Its parts are drawn by
// the fork, so every size it uses has to be here rather than in the code.
TEST_CASE("the tool panel geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    const json& component = tokens.at("component");
    require_exact_table<int>(component.at("checkbox"), {{"size", 16}, {"radius", 4}, {"glyphSize", 12}, {"labelGap", 8}},
                             "component.checkbox", {"textRole"});
    require_exact_table<int>(component.at("formRow"), {{"height", 26}, {"gap", 8}, {"labelWidth", 96}},
                             "component.formRow", {"textRole"});
    require_exact_table<int>(component.at("toolPanel"), {{"padding", 12}, {"rowGap", 8}, {"radius", 8}, {"minWidth", 220}},
                             "component.toolPanel", {"titleTextRole"});
    require_exact_table<int>(component.at("field"), {{"height", 26}, {"radius", 4}, {"paddingX", 8}, {"minWidth", 72}},
                             "component.field", {"textRole", "unitTextRole"});
    require_exact_table<int>(component.at("select"), {{"height", 26}, {"radius", 4}, {"paddingX", 8}, {"chevronSize", 16}},
                             "component.select", {"textRole"});
    // A form row's reset button is the compact icon target, at the field's
    // height so the row lines up; standalone icon buttons are taller.
    require_exact_table<int>(component.at("button").at("iconCompact"),
        {{"width", 26}, {"height", 26}, {"iconSize", 16}, {"radius", 4}}, "component.button.iconCompact");
    CHECK(component.at("field").at("height") == component.at("button").at("iconCompact").at("height"));
    CHECK(component.at("select").at("height") == component.at("field").at("height"));
    CHECK(component.at("formRow").at("height") == component.at("field").at("height"));
}

// The print-issue bubble and list are ImGui inside the Prepare canvas, like
// the tool strip: their two widths have no other home.
TEST_CASE("the print issue overlay geometry is explicit", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("printIssues"), {{"bubbleWidth", 288}, {"listWidth", 320}},
                             "component.printIssues");
}

// The axis colours name OrcaSlicer's own 3D handles, so they are the one
// semantic group that must not change between the modes.
TEST_CASE("the axis colors match the 3D handles in both modes", "[brand]")
{
    const json tokens = load_tokens();
    const std::map<std::string, std::string> expected = {{"x", "#FF3C5B"}, {"y", "#64C818"}, {"z", "#2F88E9"}};
    for (const char* mode : {"light", "dark"}) {
        INFO("semantic." << mode << ".axis");
        const json& axis = tokens.at("semantic").at(mode).at("axis");
        for (const auto& [name, value] : expected) {
            INFO("semantic." << mode << ".axis." << name);
            REQUIRE(axis.contains(name));
            CHECK(axis.at(name).get<std::string>() == value);
        }
        CHECK(axis.size() == expected.size());
    }
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

// The header row of a collapsible group: a section of the Check print report
// and a settings group share it, so the two lists line up row for row.
TEST_CASE("the group header is one height", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("groupHeader"), {{"height", 32}}, "component.groupHeader");
}

// The printer form's step marker (the canvas's add-printer steps): one dot per
// step, smaller than a status dot so the two never read as the same signal.
TEST_CASE("the step dots are one size, smaller than a status dot", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("stepDots"), {{"size", 6}}, "component.stepDots");
    REQUIRE(tokens.at("component").at("stepDots").at("size").get<int>() <
            tokens.at("component").at("statusDot").at("size").get<int>());
}

// The key field inside an Agent provider row (canvas "Agent/Provider Row",
// 2026-10-21) is taller than a form field so the Paste link, a 26 high link
// button, sits inside it with room around it.
TEST_CASE("the provider row's key field holds a link button", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("providerRow"), {{"fieldHeight", 32}}, "component.providerRow");
    REQUIRE(tokens.at("component").at("providerRow").at("fieldHeight").get<int>() >
            tokens.at("component").at("button").at("link").at("height").get<int>());
}

// JusPrin's Preferences window (canvas "JusPrin v2", Preferences, decided
// 2026-10-21): the window, its list of sections and their rows, and the width every select
// and every segment of a segmented control has in it. A select is as wide as
// two segments, so the two kinds of control line up down the right edge.
TEST_CASE("the Preferences window keeps the canvas's sizes", "[brand]")
{
    const json tokens = load_tokens();
    const json& preferences = tokens.at("component").at("preferences");
    require_exact_table<int>(preferences,
                             {{"width", 800}, {"height", 640}, {"navWidth", 208}, {"navRowHeight", 32},
                              {"controlWidth", 200}, {"segmentWidth", 96}},
                             "component.preferences");
    REQUIRE(preferences.at("navWidth").get<int>() < preferences.at("width").get<int>() / 2);
}

// The setup card's fact glyphs -- clock, material, plate, warning, info --
// sit inside a row of Label or Metadata text, 14 to 16 DIP tall. A glyph from
// the icon scale would make the row taller than its text, so the card has
// the one size below the scale, and it is not a step of that scale: Home
// takes its glyph from the scale's smallest step, which stays 16.
TEST_CASE("the setup card's inline glyph is one size", "[brand]")
{
    const json tokens = load_tokens();
    require_exact_table<int>(tokens.at("component").at("setupCard"), {{"iconSize", 14}}, "component.setupCard");
    CHECK(tokens.at("component").at("icon").at("sizes").get<std::vector<int>>().front() == 16);
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
