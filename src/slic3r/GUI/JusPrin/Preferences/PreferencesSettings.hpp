#pragma once

// What JusPrin's Preferences window shows and changes. Every setting is read
// from and written to the AppConfig key OrcaSlicer's own Preferences dialog
// uses, with the values that dialog writes, so either window can change a
// setting and the other one shows it.
//
// GUI-free: a change that needs more than its key written -- a theme switch,
// a file association, a restart, a folder picker -- comes back as an
// Effect, and the window carries it out through OrcaSlicer's own functions.

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Slic3r { class AppConfig; }

namespace Slic3r::GUI::JusPrin::Preferences {

// What only the running app can find out; the rest comes from AppConfig.
struct Facts
{
    // Canonical language code and the name the language is shown by.
    std::vector<std::pair<std::string, std::string>> languages;
    // The language the app is running in; the stored one takes effect at the
    // next start. Empty: the same as the stored one.
    std::string running_language;
    std::string version;
    // A newer version JusPrin's own update check found, and its release page;
    // empty when none. That check is not built yet; see known_update() in
    // PreferencesWindow.cpp.
    std::string update_version;
    std::string update_url;
    // Where version history is kept.
    std::string versions_folder;
    // File and link associations are a Windows feature in OrcaSlicer.
    bool windows{false};
    // Whether this JusPrin opens the "Open in slicer" links of Printables
    // (prusaslicer://) and Thingiverse (cura://).
    bool links_associated{false};
    // Providers with a stored key, in setup's order: what a new chat can
    // start with.
    std::vector<std::string> configured_providers;
};

// The page's preferences payload: {kind: "preferences", ...}.
nlohmann::json snapshot(const AppConfig& config, const Facts& facts);

enum class Effect {
    None,
    Theme,            // dark_color_mode changed
    Associate3mf,     // associate_3mf changed to `on`
    AssociateModels,  // associate_stl, associate_step and associate_drc changed to `on`
    AssociateLinks,   // the model-site links should open this JusPrin: `on`
    ChooseDownloads,  // pick a new download_path
    ShowVersions,     // reveal the version-history folder
    OpenUpdate,       // open the newer version's release page
    OrcaPreferences,  // open OrcaSlicer's own Preferences dialog
    Restart,          // quit and start JusPrin again, in the stored language
};

struct Outcome
{
    Effect      effect{Effect::None};
    bool        on{false};
    std::string value;
    // Set when the action was refused; nothing was written.
    std::string error;
};

// Applies one preferences_action from the page: {action: "set", name, value}
// writes that setting's key and saves; any other action only names its
// effect. An unknown name or a value of the wrong kind is refused.
Outcome apply_action(AppConfig& config, const nlohmann::json& action);

} // namespace Slic3r::GUI::JusPrin::Preferences
