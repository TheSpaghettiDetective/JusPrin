#include "AppFolders.hpp"

#include <cstdlib>

namespace Slic3r::GUI::JusPrin {
namespace fs = std::filesystem;

namespace {
std::string env(const char* key)
{
    const char* value = std::getenv(key);
    return value ? value : "";
}
} // namespace

UserFolders user_folders()
{
    UserFolders folders;
#ifdef _WIN32
    const auto profile = env("USERPROFILE");
    folders.home = profile.empty() ? fs::current_path() : fs::u8path(profile);
    const auto appdata = env("APPDATA");
    folders.app_data = appdata.empty() ? folders.home : fs::u8path(appdata);
    folders.programs = fs::u8path(env("ProgramFiles"));
#elif defined(__APPLE__)
    const auto home = env("HOME");
    folders.home = home.empty() ? fs::current_path() : fs::u8path(home);
    folders.app_data = folders.home / "Library" / "Application Support";
#else
    const auto home = env("HOME");
    folders.home = home.empty() ? fs::current_path() : fs::u8path(home);
    const auto xdg = env("XDG_CONFIG_HOME");
    folders.app_data = xdg.empty() ? folders.home / ".config" : fs::u8path(xdg);
#endif
    return folders;
}

fs::path default_app_folder(const std::string& name) { return user_folders().app_data / fs::u8path(name); }

std::vector<fs::path> orca_data_folders(const UserFolders& folders)
{
    std::vector<fs::path> candidates;
#ifdef _WIN32
    if (!folders.programs.empty())
        candidates.push_back(folders.programs / "OrcaSlicer" / "data_dir");
#endif
    candidates.push_back(folders.app_data / "OrcaSlicer");
#if !defined(_WIN32) && !defined(__APPLE__)
    for (const char* id : {"com.orcaslicer.OrcaSlicer", "io.github.orcaslicer.OrcaSlicer", "io.github.softfever.OrcaSlicer"})
        candidates.push_back(folders.home / ".var" / "app" / id / "config" / "OrcaSlicer");
#endif
    return candidates;
}

fs::path installed_app_folder(const std::vector<fs::path>& candidates, const fs::path& fallback, const char* override_variable)
{
    if (const char* chosen = std::getenv(override_variable))
        return fs::u8path(chosen);
    std::error_code error;
    for (const fs::path& candidate : candidates)
        if (fs::is_directory(candidate, error))
            return candidate;
    return fallback;
}

} // namespace Slic3r::GUI::JusPrin
