#include "PreferencesSettings.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"

#include <array>

namespace Slic3r::GUI::JusPrin::Preferences {

using nlohmann::json;

namespace {

// OrcaSlicer's mouse-button combo boxes store the item's index: None, Pan,
// Rotate.
constexpr std::array<const char*, 3> kDragActions{"none", "pan", "rotate"};

std::string drag_action(const AppConfig& config, const std::string& key)
{
    const std::string stored = config.get(key);
    const int index = stored.empty() ? 0 : std::atoi(stored.c_str());
    return index >= 0 && index < int(kDragActions.size()) ? kDragActions[index] : kDragActions[0];
}

// The language list entry a code means, the way OrcaSlicer's dialog matches
// the stored one: the exact code, or else the first one in the same language
// ("de" for "de_AT").
std::string listed_language(const std::string& stored, const Facts& facts)
{
    for (const auto& [code, name] : facts.languages)
        if (code == stored)
            return code;
    if (stored.size() >= 2)
        for (const auto& [code, name] : facts.languages)
            if (code.compare(0, 2, stored, 0, 2) == 0)
                return code;
    // A language with no catalog in this build stays what is stored, so
    // that every listed language, English too, can still be chosen.
    return stored;
}

// The section setup stores its keys and provider under.
constexpr const char* kAgentSection = "jusprin_agent";

// The provider a new chat talks to, as the app reads it: the stored one, or
// OpenAI when none is.
std::string new_chat_provider(const AppConfig& config)
{
    const std::string stored = config.get(kAgentSection, "provider");
    return stored.empty() ? "openai" : stored;
}

// One switch the page can turn, and the key it writes.
struct Switch
{
    const char* name;
    const char* key;
};

// Written with set_bool, as OrcaSlicer's checkboxes write them.
constexpr std::array<Switch, 4> kBoolSwitches{{
    {"zoomToPointer", "zoom_to_mouse"},
    {"reverseZoom", "reverse_mouse_wheel_zoom"},
    {"freeCamera", "use_free_camera"},
    {"updateProfiles", "sync_system_preset"},
}};

constexpr std::array<Switch, 3> kDragSettings{{
    {"leftDrag", "left_mouse_drag_action"},
    {"middleDrag", "middle_mouse_drag_action"},
    {"rightDrag", "right_mouse_drag_action"},
}};

Outcome refused(std::string reason)
{
    Outcome outcome;
    outcome.error = std::move(reason);
    return outcome;
}

Outcome set_setting(AppConfig& config, const std::string& name, const json& value)
{
    Outcome outcome;
    for (const Switch& entry : kBoolSwitches) {
        if (name != entry.name)
            continue;
        if (!value.is_boolean())
            return refused(name + " takes true or false");
        config.set_bool(entry.key, value.get<bool>());
        config.save();
        return outcome;
    }
    for (const Switch& entry : kDragSettings) {
        if (name != entry.name)
            continue;
        const std::string choice = value.is_string() ? value.get<std::string>() : std::string();
        for (std::size_t index = 0; index < kDragActions.size(); ++index)
            if (choice == kDragActions[index]) {
                config.set(entry.key, std::to_string(index));
                config.save();
                return outcome;
            }
        return refused(name + " takes none, pan or rotate");
    }

    const std::string choice = value.is_string() ? value.get<std::string>() : std::string();
    if (name == "theme") {
        if (choice != "light" && choice != "dark")
            return refused("theme takes light or dark");
        config.set("dark_color_mode", choice == "dark" ? "1" : "0");
        config.save();
        outcome.effect = Effect::Theme;
        return outcome;
    }
    if (name == "units") {
        if (choice != "mm" && choice != "in")
            return refused("units takes mm or in");
        config.set("use_inches", choice == "in" ? "1" : "0");
        config.save();
        return outcome;
    }
    if (name == "navigation") {
        if (choice != "mouse" && choice != "touchpad")
            return refused("navigation takes mouse or touchpad");
        config.set("camera_navigation_style", choice == "touchpad" ? "1" : "0");
        config.save();
        return outcome;
    }
    if (name == "newChatProvider") {
        // Each provider's model is fixed for now (ProviderProfile::model), so
        // choosing what new chats start with is choosing the provider.
        if (Agent::profile_for(choice) == nullptr)
            return refused("newChatProvider takes a provider this build has");
        config.set(kAgentSection, "provider", choice);
        config.save();
        return outcome;
    }
    if (name == "language") {
        // Stored only: JusPrin loads it at its next start. Switching in place
        // would rebuild every window while the web views of the old language
        // still run, and WebView2 refuses new ones whose language differs.
        if (choice.empty())
            return refused("language takes a language code");
        config.set("language", choice);
        config.save();
        return outcome;
    }
    if (name == "open3mf" || name == "openModels" || name == "openLinks") {
        if (!value.is_boolean())
            return refused(name + " takes true or false");
        outcome.on = value.get<bool>();
        if (name == "open3mf") {
            config.set_bool("associate_3mf", outcome.on);
            outcome.effect = Effect::Associate3mf;
        } else if (name == "openModels") {
            // One switch for the model formats; OrcaSlicer keeps a key each.
            config.set_bool("associate_stl", outcome.on);
            config.set_bool("associate_step", outcome.on);
            config.set_bool("associate_drc", outcome.on);
            outcome.effect = Effect::AssociateModels;
        } else {
            // Kept in the registry, not in AppConfig: OrcaSlicer reads it back
            // from there too.
            outcome.effect = Effect::AssociateLinks;
        }
        if (outcome.effect != Effect::AssociateLinks)
            config.save();
        return outcome;
    }
    return refused("no setting is called " + name);
}

} // namespace

json snapshot(const AppConfig& config, const Facts& facts)
{
    json languages = json::array();
    for (const auto& [code, name] : facts.languages)
        languages.push_back({{"code", code}, {"name", name}});

    json update = nullptr;
    if (!facts.update_version.empty())
        update = {{"version", facts.update_version}, {"url", facts.update_url}};

    const std::string stored_language  = listed_language(config.get("language"), facts);
    const std::string running_language = facts.running_language.empty() ? stored_language
                                                                         : listed_language(facts.running_language, facts);
    std::string stored_name = stored_language;
    for (const auto& [code, name] : facts.languages)
        if (code == stored_language)
            stored_name = name;

    json choices = json::array();
    for (const std::string& provider : facts.configured_providers)
        if (const Agent::ProviderProfile* profile = Agent::profile_for(provider))
            choices.push_back({{"provider", provider}, {"model", profile->model}, {"label", profile->display_name}});

    return {
        {"kind", "preferences"},
        {"agent", {{"newChatProvider", new_chat_provider(config)}, {"newChatChoices", std::move(choices)}}},
        {"appearance",
         {{"language", stored_language},
          // The language a restart would bring in, when it is not the one
          // the app is running in.
          {"restartFor", running_language == stored_language ? json(nullptr) : json(stored_name)},
          {"languages", std::move(languages)},
          {"theme", config.get("dark_color_mode") == "1" ? "dark" : "light"},
          {"units", config.get("use_inches") == "1" ? "in" : "mm"}}},
        {"view3d",
         {{"navigation", config.get_bool("camera_navigation_style") ? "touchpad" : "mouse"},
          {"zoomToPointer", config.get_bool("zoom_to_mouse")},
          {"reverseZoom", config.get_bool("reverse_mouse_wheel_zoom")},
          {"freeCamera", config.get_bool("use_free_camera")},
          {"leftDrag", drag_action(config, "left_mouse_drag_action")},
          {"middleDrag", drag_action(config, "middle_mouse_drag_action")},
          {"rightDrag", drag_action(config, "right_mouse_drag_action")}}},
        {"projects",
         {{"versionsFolder", facts.versions_folder},
          {"downloadPath", config.get("download_path")},
          {"associations", facts.windows},
          {"open3mf", config.get_bool("associate_3mf")},
          {"openModels", config.get_bool("associate_stl") && config.get_bool("associate_step")},
          {"openLinks", facts.links_associated}}},
        {"updates",
         {{"version", facts.version},
          {"available", std::move(update)},
          {"updateProfiles", config.get_bool("sync_system_preset")}}},
    };
}

Outcome apply_action(AppConfig& config, const json& action)
{
    if (!action.is_object() || !action.contains("action") || !action["action"].is_string())
        return refused("a preferences action names what it does");
    const std::string what = action["action"].get<std::string>();
    if (what == "set") {
        if (!action.contains("name") || !action["name"].is_string() || !action.contains("value"))
            return refused("set names a setting and a value");
        return set_setting(config, action["name"].get<std::string>(), action["value"]);
    }
    Outcome outcome;
    if (what == "choose_downloads")
        outcome.effect = Effect::ChooseDownloads;
    else if (what == "show_versions")
        outcome.effect = Effect::ShowVersions;
    else if (what == "open_update")
        outcome.effect = Effect::OpenUpdate;
    else if (what == "orca_preferences")
        outcome.effect = Effect::OrcaPreferences;
    else if (what == "restart")
        outcome.effect = Effect::Restart;
    else
        return refused("no preferences action is called " + what);
    return outcome;
}

} // namespace Slic3r::GUI::JusPrin::Preferences
