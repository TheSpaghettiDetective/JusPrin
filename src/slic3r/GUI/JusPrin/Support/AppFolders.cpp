#include "AppFolders.hpp"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Slic3r::GUI::JusPrin {
namespace fs = std::filesystem;

fs::path environment_path(const char* name)
{
#ifdef _WIN32
    const std::wstring key(name, name + std::strlen(name));
    std::wstring value(MAX_PATH, L'\0');
    DWORD size = GetEnvironmentVariableW(key.c_str(), value.data(), DWORD(value.size()));
    if (size > value.size()) {
        value.resize(size);
        size = GetEnvironmentVariableW(key.c_str(), value.data(), DWORD(value.size()));
    }
    value.resize(size <= value.size() ? size : 0);
    return fs::path(value);
#else
    const char* value = std::getenv(name);
    return value ? fs::u8path(value) : fs::path();
#endif
}

UserFolders user_folders()
{
    UserFolders folders;
#ifdef _WIN32
    const fs::path profile = environment_path("USERPROFILE");
    folders.home = profile.empty() ? fs::current_path() : profile;
    const fs::path appdata = environment_path("APPDATA");
    folders.app_data = appdata.empty() ? folders.home : appdata;
    folders.programs = environment_path("ProgramFiles");
#elif defined(__APPLE__)
    const fs::path home = environment_path("HOME");
    folders.home = home.empty() ? fs::current_path() : home;
    folders.app_data = folders.home / "Library" / "Application Support";
#else
    const fs::path home = environment_path("HOME");
    folders.home = home.empty() ? fs::current_path() : home;
    const fs::path xdg = environment_path("XDG_CONFIG_HOME");
    folders.app_data = xdg.empty() ? folders.home / ".config" : xdg;
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
    if (std::getenv(override_variable) != nullptr)
        return environment_path(override_variable);
    std::error_code error;
    for (const fs::path& candidate : candidates)
        if (fs::is_directory(candidate, error))
            return candidate;
    return fallback;
}

} // namespace Slic3r::GUI::JusPrin
