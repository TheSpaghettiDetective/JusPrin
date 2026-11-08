#pragma once

// Where apps keep their per-user data, by platform.
//
// The rule is the one OrcaSlicer's GUI_App applies to its own default
// data_dir(): macOS ~/Library/Application Support/<name>, Windows
// %APPDATA%\<name>, Linux $XDG_CONFIG_HOME/<name> or ~/.config/<name>.
// GUI_App works it out inline through wxStandardPaths; this is the same rule
// in plain std, so files compiled into the GUI-free test binaries can use it.

#include <filesystem>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

struct UserFolders
{
    std::filesystem::path home;
    // The folder apps keep their data folders in.
    std::filesystem::path app_data;
    // Windows: the folder installers put apps in, %ProgramFiles%. Empty
    // elsewhere.
    std::filesystem::path programs;
};

// From the environment: HOME (USERPROFILE on Windows), APPDATA, ProgramFiles,
// XDG_CONFIG_HOME.
UserFolders user_folders();

// Where the app called `name` keeps its data when it runs on its default
// folder, whatever folder this app was started with.
std::filesystem::path default_app_folder(const std::string& name);

// The data folders an OrcaSlicer install here may use, in the order
// OrcaSlicer itself would pick between them.
// - Windows: a portable copy's "data_dir" beside orca-slicer.exe, which
//   OrcaSlicer uses over its default when there is one (GUI_App's
//   init_app_config), where the installer puts it, %ProgramFiles%\OrcaSlicer;
//   then %APPDATA%\OrcaSlicer. A portable copy unzipped anywhere else is not
//   recorded anywhere, so only the folder picker finds it.
// - Linux: $XDG_CONFIG_HOME/OrcaSlicer (~/.config/OrcaSlicer, an AppImage or
//   a distribution package), then each Flatpak's, ~/.var/app/<app id>/config/
//   OrcaSlicer: com.orcaslicer.OrcaSlicer on Flathub since 2026-03, and the
//   ids before it, io.github.orcaslicer.OrcaSlicer and
//   io.github.softfever.OrcaSlicer.
// - macOS: ~/Library/Application Support/OrcaSlicer.
std::vector<std::filesystem::path> orca_data_folders(const UserFolders& folders);

// The data folder of another app installed here, as first launch looks for
// it: the value of `override_variable` when set, empty (none) when it is set
// to "", and otherwise the first of `candidates` that is a folder, or
// `fallback` when none is. Test runs set the variable so they never read the
// person's real installs.
std::filesystem::path installed_app_folder(const std::vector<std::filesystem::path>& candidates,
                                           const std::filesystem::path& fallback, const char* override_variable);

} // namespace Slic3r::GUI::JusPrin
