// JusPrin's Preferences window writes the keys OrcaSlicer's own dialog
// writes, with the values that dialog writes: each setting goes in through
// apply_action() and comes back out of snapshot() and AppConfig the same way.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Preferences/PreferencesSettings.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>

using namespace Slic3r::GUI::JusPrin::Preferences;
using nlohmann::json;

namespace {

// AppConfig::save() writes into the data directory, from the main thread.
struct ConfigFixture
{
    ConfigFixture()
    {
        Slic3r::save_main_thread_id();
        dir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("jusprin-prefs-%%%%%%");
        boost::filesystem::create_directories(dir);
        Slic3r::set_data_dir(dir.string());
    }
    ~ConfigFixture()
    {
        boost::system::error_code ignored;
        boost::filesystem::remove_all(dir, ignored);
    }

    json set(const std::string& name, const json& value)
    {
        const Outcome outcome = apply_action(config, {{"action", "set"}, {"name", name}, {"value", value}});
        REQUIRE(outcome.error.empty());
        return snapshot(config, facts);
    }

    boost::filesystem::path dir;
    Slic3r::AppConfig       config;
    Facts                   facts;
};

} // namespace

TEST_CASE_METHOD(ConfigFixture, "Appearance settings write OrcaSlicer's keys", "[preferences]")
{
    json state = set("theme", "dark");
    CHECK(config.get("dark_color_mode") == "1");
    CHECK(state["appearance"]["theme"] == "dark");
    state = set("theme", "light");
    CHECK(config.get("dark_color_mode") == "0");
    CHECK(state["appearance"]["theme"] == "light");

    state = set("units", "in");
    CHECK(config.get("use_inches") == "1");
    CHECK(state["appearance"]["units"] == "in");
    state = set("units", "mm");
    CHECK(config.get("use_inches") == "0");
    CHECK(state["appearance"]["units"] == "mm");

    CHECK(apply_action(config, {{"action", "set"}, {"name", "theme"}, {"value", "dark"}}).effect == Effect::Theme);
}

TEST_CASE_METHOD(ConfigFixture, "3D view settings write OrcaSlicer's keys", "[preferences]")
{
    json state = set("navigation", "touchpad");
    CHECK(config.get("camera_navigation_style") == "1");
    CHECK(state["view3d"]["navigation"] == "touchpad");
    state = set("navigation", "mouse");
    CHECK(config.get("camera_navigation_style") == "0");
    CHECK(state["view3d"]["navigation"] == "mouse");

    for (const auto& [name, key] : std::vector<std::pair<std::string, std::string>>{
             {"zoomToPointer", "zoom_to_mouse"}, {"reverseZoom", "reverse_mouse_wheel_zoom"}, {"freeCamera", "use_free_camera"}}) {
        state = set(name, true);
        CHECK(config.get(key) == "true");
        CHECK(state["view3d"][name] == true);
        state = set(name, false);
        CHECK(config.get(key) == "false");
        CHECK(state["view3d"][name] == false);
    }

    // OrcaSlicer's combo boxes store the index of None, Pan, Rotate.
    for (const auto& [name, key] : std::vector<std::pair<std::string, std::string>>{
             {"leftDrag", "left_mouse_drag_action"}, {"middleDrag", "middle_mouse_drag_action"}, {"rightDrag", "right_mouse_drag_action"}}) {
        state = set(name, "none");
        CHECK(config.get(key) == "0");
        CHECK(state["view3d"][name] == "none");
        state = set(name, "pan");
        CHECK(config.get(key) == "1");
        CHECK(state["view3d"][name] == "pan");
        state = set(name, "rotate");
        CHECK(config.get(key) == "2");
        CHECK(state["view3d"][name] == "rotate");
    }
}

TEST_CASE_METHOD(ConfigFixture, "What OrcaSlicer's dialog wrote is what the window shows", "[preferences]")
{
    // OrcaSlicer's defaults: rotate with the left button, pan with the others.
    json state = snapshot(config, facts);
    CHECK(state["view3d"]["leftDrag"] == "rotate");
    CHECK(state["view3d"]["middleDrag"] == "pan");
    CHECK(state["view3d"]["rightDrag"] == "pan");
    CHECK(state["updates"]["updateProfiles"] == true);

    config.set("left_mouse_drag_action", "1");
    config.set("dark_color_mode", "1");
    state = snapshot(config, facts);
    CHECK(state["view3d"]["leftDrag"] == "pan");
    CHECK(state["appearance"]["theme"] == "dark");
}

TEST_CASE_METHOD(ConfigFixture, "Update and file settings write OrcaSlicer's keys", "[preferences]")
{
    json state = set("updateProfiles", false);
    CHECK(config.get("sync_system_preset") == "false");
    CHECK(state["updates"]["updateProfiles"] == false);

    Outcome outcome = apply_action(config, {{"action", "set"}, {"name", "open3mf"}, {"value", true}});
    CHECK(outcome.effect == Effect::Associate3mf);
    CHECK(outcome.on);
    CHECK(config.get("associate_3mf") == "true");

    outcome = apply_action(config, {{"action", "set"}, {"name", "openModels"}, {"value", true}});
    CHECK(outcome.effect == Effect::AssociateModels);
    CHECK(config.get("associate_stl") == "true");
    CHECK(config.get("associate_step") == "true");
    CHECK(config.get("associate_drc") == "true");
    CHECK(snapshot(config, facts)["projects"]["openModels"] == true);
    outcome = apply_action(config, {{"action", "set"}, {"name", "openModels"}, {"value", false}});
    CHECK_FALSE(outcome.on);
    CHECK(config.get("associate_stl") == "false");
    CHECK(snapshot(config, facts)["projects"]["openModels"] == false);
}

TEST_CASE_METHOD(ConfigFixture, "The language is matched the way OrcaSlicer's dialog matches it", "[preferences]")
{
    facts.languages = {{"en", "English"}, {"de_DE", "Deutsch"}, {"zh_CN", "Chinese"}};
    config.set("language", "de_AT");
    CHECK(snapshot(config, facts)["appearance"]["language"] == "de_DE");
    config.set("language", "zh_CN");
    CHECK(snapshot(config, facts)["appearance"]["language"] == "zh_CN");

    // Choosing one stores it for the next start; the page offers a restart
    // while the app still runs in the other one.
    facts.running_language = "zh_CN";
    CHECK(snapshot(config, facts)["appearance"]["restartFor"].is_null());
    const Outcome outcome = apply_action(config, {{"action", "set"}, {"name", "language"}, {"value", "de_DE"}});
    CHECK(outcome.effect == Effect::None);
    CHECK(config.get("language") == "de_DE");
    CHECK(snapshot(config, facts)["appearance"]["restartFor"] == "Deutsch");
    // Picking the running language again needs no restart.
    apply_action(config, {{"action", "set"}, {"name", "language"}, {"value", "zh_CN"}});
    CHECK(snapshot(config, facts)["appearance"]["restartFor"].is_null());
    CHECK(apply_action(config, {{"action", "restart"}}).effect == Effect::Restart);
}

TEST_CASE_METHOD(ConfigFixture, "Wrong names and values are refused and write nothing", "[preferences]")
{
    config.set("use_inches", "0");
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "units"}, {"value", "furlongs"}}).error.empty());
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "units"}, {"value", true}}).error.empty());
    CHECK(config.get("use_inches") == "0");
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "auto_slice_after_change"}, {"value", true}}).error.empty());
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "zoomToPointer"}, {"value", "yes"}}).error.empty());
    CHECK_FALSE(apply_action(config, {{"action", "format_disk"}}).error.empty());
    CHECK_FALSE(apply_action(config, json::array()).error.empty());
    CHECK_FALSE(apply_action(config, {{"action", "skip_update"}}).error.empty());
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "checkUpdates"}, {"value", "daily"}}).error.empty());
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "stableOnly"}, {"value", true}}).error.empty());
}

TEST_CASE_METHOD(ConfigFixture, "New chats start with a provider that has a key, and its built-in model", "[preferences]")
{
    // The app's own fallback when setup stored nothing.
    CHECK(snapshot(config, facts)["agent"]["newChatProvider"] == "openai");
    CHECK(snapshot(config, facts)["agent"]["newChatChoices"].empty());

    facts.configured_providers = {"openai", "anthropic"};
    const json choices = snapshot(config, facts)["agent"]["newChatChoices"];
    REQUIRE(choices.size() == 2);
    CHECK(choices[1]["provider"] == "anthropic");
    CHECK(choices[1]["model"] == "claude-opus-5-5");
    CHECK(choices[0]["model"] == "gpt-5.4-mini");

    // Setup's key: the provider a chat with no choice of its own talks to.
    CHECK(set("newChatProvider", "anthropic")["agent"]["newChatProvider"] == "anthropic");
    CHECK(config.get("jusprin_agent", "provider") == "anthropic");
    CHECK_FALSE(apply_action(config, {{"action", "set"}, {"name", "newChatProvider"}, {"value", "skynet"}}).error.empty());
    CHECK(config.get("jusprin_agent", "provider") == "anthropic");
}

TEST_CASE_METHOD(ConfigFixture, "JusPrin's own update check runs from the Updates section", "[preferences]")
{
    // On until turned off.
    CHECK(snapshot(config, facts)["updates"]["checkUpdates"] == true);
    CHECK(set("checkUpdates", false)["updates"]["checkUpdates"] == false);
    CHECK(config.get("jusprin_update_check") == "false");
    CHECK(set("checkUpdates", true)["updates"]["checkUpdates"] == true);

    CHECK(apply_action(config, {{"action", "check_updates"}}).effect == Effect::CheckUpdates);
    const Outcome skip = apply_action(config, {{"action", "skip_update"}, {"version", "2.1.0"}});
    CHECK(skip.effect == Effect::SkipUpdate);
    CHECK(skip.value == "2.1.0");
}

TEST_CASE_METHOD(ConfigFixture, "Facts only the app knows reach the page", "[preferences]")
{
    CHECK(snapshot(config, facts)["updates"]["status"] == "notChecked");
    CHECK(snapshot(config, facts)["updates"]["checked"].is_null());

    facts.version          = "2.0.0";
    facts.update_status    = "available";
    facts.update_checked   = 1'800'000'000;
    facts.update_version   = "2.1.0";
    facts.update_url       = "https://example.invalid/release";
    facts.update_published = "2026-11-02T10:00:00Z";
    facts.update_notes     = "## Fixes";
    facts.windows          = true;
    facts.links_associated = true;
    facts.versions_folder  = "C:/data/jusprin/projects";
    config.set("download_path", "C:/Downloads");
    const json state = snapshot(config, facts);
    CHECK(state["kind"] == "preferences");
    CHECK(state["updates"]["version"] == "2.0.0");
    CHECK(state["updates"]["status"] == "available");
    CHECK(state["updates"]["checked"] == 1'800'000'000);
    CHECK(state["updates"]["available"]["version"] == "2.1.0");
    CHECK(state["updates"]["available"]["url"] == "https://example.invalid/release");
    CHECK(state["updates"]["available"]["published"] == "2026-11-02T10:00:00Z");
    CHECK(state["updates"]["available"]["notes"] == "## Fixes");
    CHECK(state["projects"]["associations"] == true);
    CHECK(state["projects"]["openLinks"] == true);
    CHECK(state["projects"]["downloadPath"] == "C:/Downloads");
    CHECK(state["projects"]["versionsFolder"] == "C:/data/jusprin/projects");
    CHECK(snapshot(config, Facts{})["updates"]["available"].is_null());
}
