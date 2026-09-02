#include "BrandPalette.hpp"

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

// Loaded once. A missing or malformed token file keeps the stock OrcaSlicer
// colors, the same recovery the shell applies when its theme cannot load.
const ShellTheme* brand_theme()
{
    static const ShellTheme* theme = []() -> const ShellTheme* {
        try {
            return new ShellTheme(ShellTheme::load_from_resources());
        } catch (const std::exception& error) {
            BOOST_LOG_TRIVIAL(error) << "JusPrin brand palette not applied, keeping the standard colors: "
                                     << error.what();
            return nullptr;
        }
    }();
    return theme;
}

std::string hex(const wxColour& color) { return color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString(); }

// OrcaSlicer color literals that carry brand or surface meaning, paired with
// the JusPrin token that takes their place. Keys are the light-mode literals
// the widget code writes; StateColor consults this table before its own
// dark-mode remap, so the same keys serve both modes with mode-specific
// values. Literals not listed here (status oranges, axis colors, filament
// swatches) keep their OrcaSlicer meaning.
std::vector<std::pair<wxColour, wxColour>> widget_overrides(const ShellPalette& p)
{
    return {
        // Brand teal family -> decisive action color and its states.
        {wxColour("#009688"), p.action_primary},
        {wxColour("#009687"), p.action_primary}, // HyperLink's deliberately off-by-one teal
        {wxColour("#26A69A"), p.action_primary_hover},
        {wxColour("#52C7B8"), p.action_primary_hover},
        {wxColour("#00675B"), p.action_primary_pressed},
        {wxColour("#22BFB0"), p.border_focus},
        {wxColour("#00FFD4"), p.border_focus},
        // Teal-tinted selection and focus surfaces.
        {wxColour("#BFE1DE"), p.surface_selected},
        {wxColour("#E5F0EE"), p.surface_subtle},
        {wxColour("#EBF9F0"), p.surface_selected},
        {wxColour("#EDFAF2"), p.surface_selected},
        {wxColour("#DBFDD5"), p.surface_selected},
        {wxColour("#D7E8DE"), p.surface_selected},
        // Neutral surfaces: windows, grouped panels, buttons, disabled fills.
        {wxColour("#FFFFFF"), p.surface_raised},
        {wxColour("#FEFFFF"), p.surface_canvas},
        {wxColour("#F8F8F8"), p.surface_subtle},
        {wxColour("#F4F4F4"), p.surface_subtle},
        {wxColour("#F1F1F1"), p.surface_subtle},
        {wxColour("#DFDFDF"), p.surface_subtle},
        {wxColour("#D4D4D4"), p.surface_selected},
        {wxColour("#F0F0F1"), p.action_disabled},
        // Quiet structure: separators, input borders.
        {wxColour("#EEEEEE"), p.border_subtle},
        {wxColour("#E8E8E8"), p.border_subtle},
        {wxColour("#DBDBDB"), p.border_subtle},
        {wxColour("#A6A9AA"), p.border_subtle},
        // Text.
        {wxColour("#000000"), p.text_primary},
        {wxColour("#262E30"), p.text_primary},
        {wxColour("#363636"), p.text_primary},
        {wxColour("#323A3D"), p.text_secondary},
        {wxColour("#323A3C"), p.text_secondary},
        {wxColour("#303A3C"), p.text_secondary},
        {wxColour("#2C2C2E"), p.text_secondary},
        {wxColour("#6B6A6A"), p.text_secondary},
        {wxColour("#6B6B6A"), p.text_secondary},
        {wxColour("#6B6B6B"), p.action_disabled_text},
        {wxColour("#ACACAC"), p.action_disabled_text},
        {wxColour("#909090"), p.action_disabled_text},
        {wxColour("#9E9E9E"), p.action_disabled_text},
        // Text painted on the confirm button's teal fill.
        {wxColour("#FEFEFE"), p.action_primary_text},
        {wxColour("#FFFFFD"), p.action_primary_text},
    };
}

// SVG icon substitutions. OrcaSlicer's icons name the brand teal both as an
// attribute value (quoted) and inside style strings (bare), and BitmapCache
// applies its own quoted teal rewrite first, so both spellings are covered.
std::map<std::string, std::string> icon_replaces(const ShellPalette& p)
{
    const std::string primary = hex(p.action_primary);
    const std::string hover   = hex(p.action_primary_hover);
    const std::string text    = hex(p.text_primary);
    return {
        {"\"#009688\"", "\"" + primary + "\""},
        {"\"#0x00AE42\"", "\"" + primary + "\""},
        {"\"#00FF00\"", "\"" + hover + "\""},
        {"#009688", primary},
        {"#00675b", primary},
        {"#26A69A", hover},
        {"#52c7b8", hover},
        // Monochrome brand mark and icon line work follow the text color.
        {"\"#262E30\"", "\"" + text + "\""},
        {"#262E30", text},
    };
}

} // namespace

void apply_brand_palette()
{
    // Brand artwork under OrcaSlicer's asset names; see resources/jusprin/overlay/README.md.
    set_var_overlay_dir((boost::filesystem::path(resources_dir()) / "jusprin" / "overlay" / "images").string());

    const ShellTheme* theme = brand_theme();
    if (theme == nullptr)
        return;

    const ShellPalette& p = theme->palette(GUI_App::dark_mode());
    StateColor::SetColorOverrides(widget_overrides(p));
    BitmapCache::SetColorReplaces(icon_replaces(p));
}

} // namespace Slic3r::GUI::JusPrin
