// Unit tests for where apps keep their per-user data, and how first launch
// finds another app's folder: the first of its folders that is there, the
// platform's default, or a variable's.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Support/AppFolders.hpp"
#include "slic3r/GUI/JusPrin/V1Import/V1Import.hpp"

#include <cstdlib>
#include <optional>
#include <string>

using namespace Slic3r::GUI::JusPrin;
namespace fs = std::filesystem;

namespace {

void set_env(const char* name, const std::optional<std::string>& value)
{
#ifdef _WIN32
    // Windows has no empty variable: "" removes it.
    _putenv_s(name, value ? value->c_str() : "");
#else
    if (value)
        setenv(name, value->c_str(), 1);
    else
        unsetenv(name);
#endif
}

// Sets a variable for one test and puts back what was there.
class ScopedEnv
{
public:
    ScopedEnv(const char* name, std::optional<std::string> value) : m_name(name)
    {
        if (const char* before = std::getenv(name))
            m_before = before;
        set_env(name, value);
    }
    ~ScopedEnv() { set_env(m_name, m_before); }

private:
    const char*                m_name;
    std::optional<std::string> m_before;
};

} // namespace

TEST_CASE("apps keep their data where the platform puts per-user app data", "[app_folders]")
{
#ifdef _WIN32
    ScopedEnv profile("USERPROFILE", "C:\\Users\\alex");
    ScopedEnv appdata("APPDATA", "C:\\Users\\alex\\AppData\\Roaming");
    CHECK(user_folders().app_data == fs::u8path("C:\\Users\\alex\\AppData\\Roaming"));
    CHECK(default_app_folder("OrcaSlicer") == fs::u8path("C:\\Users\\alex\\AppData\\Roaming") / "OrcaSlicer");
#elif defined(__APPLE__)
    ScopedEnv home("HOME", "/Users/alex");
    CHECK(user_folders().app_data == fs::path("/Users/alex/Library/Application Support"));
    CHECK(default_app_folder("OrcaSlicer") == fs::path("/Users/alex/Library/Application Support/OrcaSlicer"));
#else
    ScopedEnv home("HOME", "/home/alex");
    {
        ScopedEnv xdg("XDG_CONFIG_HOME", std::nullopt);
        CHECK(default_app_folder("OrcaSlicer") == fs::path("/home/alex/.config/OrcaSlicer"));
    }
    ScopedEnv xdg("XDG_CONFIG_HOME", "/home/alex/settings");
    CHECK(default_app_folder("OrcaSlicer") == fs::path("/home/alex/settings/OrcaSlicer"));
#endif
}

TEST_CASE("another app's folder is the first of its folders that is there, unless a variable names one", "[app_folders]")
{
    const fs::path root = fs::temp_directory_path() / "jusprin-app-folders-test";
    fs::remove_all(root);
    const fs::path portable = root / "portable", usual = root / "usual", fallback = root / "default";
    {
        ScopedEnv unset("JUSPRIN_ORCA_DATADIR", std::nullopt);
        CHECK(installed_app_folder({portable, usual}, fallback, "JUSPRIN_ORCA_DATADIR") == fallback);
        fs::create_directories(usual);
        CHECK(installed_app_folder({portable, usual}, fallback, "JUSPRIN_ORCA_DATADIR") == usual);
        fs::create_directories(portable);
        CHECK(installed_app_folder({portable, usual}, fallback, "JUSPRIN_ORCA_DATADIR") == portable);
    }
    {
        ScopedEnv chosen("JUSPRIN_ORCA_DATADIR", "/fixtures/orca");
        CHECK(installed_app_folder({portable, usual}, fallback, "JUSPRIN_ORCA_DATADIR") == fs::u8path("/fixtures/orca"));
    }
#ifndef _WIN32
    {
        ScopedEnv none("JUSPRIN_ORCA_DATADIR", "");
        CHECK(installed_app_folder({portable, usual}, fallback, "JUSPRIN_ORCA_DATADIR").empty());
    }
#endif
    fs::remove_all(root);
}

TEST_CASE("OrcaSlicer is looked for in a portable copy and in each Flatpak as well as in its default folder", "[app_folders]")
{
    UserFolders folders;
    folders.home     = fs::u8path("/home/alex");
    folders.app_data = fs::u8path("/home/alex/.config");
    folders.programs = fs::u8path("C:/Program Files");
    const std::vector<fs::path> found = orca_data_folders(folders);
#ifdef _WIN32
    CHECK(found == std::vector<fs::path>{fs::u8path("C:/Program Files") / "OrcaSlicer" / "data_dir", folders.app_data / "OrcaSlicer"});
#elif defined(__APPLE__)
    CHECK(found == std::vector<fs::path>{folders.app_data / "OrcaSlicer"});
#else
    CHECK(found == std::vector<fs::path>{folders.app_data / "OrcaSlicer",
                                         fs::path("/home/alex/.var/app/com.orcaslicer.OrcaSlicer/config/OrcaSlicer"),
                                         fs::path("/home/alex/.var/app/io.github.orcaslicer.OrcaSlicer/config/OrcaSlicer"),
                                         fs::path("/home/alex/.var/app/io.github.softfever.OrcaSlicer/config/OrcaSlicer")});
#endif
}

TEST_CASE("v1's folder is v1's default whatever folder JusPrin 2 runs on", "[v1_import][app_folders]")
{
    CHECK(V1Import::v1_folder() == default_app_folder("JusPrin"));
}
