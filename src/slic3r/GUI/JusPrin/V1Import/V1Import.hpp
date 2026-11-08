#pragma once

// JusPrin 1 on this computer, for the import that brings its printers.
//
// JusPrin 1 is a fork of OrcaSlicer with its own data folder ("JusPrin"), so
// its printers, presets and printer connections are imported the way
// OrcaSlicer's are (Home/OrcaImporter.hpp), and only when the person asks.
// Its own settings (units, language, window places, its sign-in) never come
// along. This adds what JusPrin 1 has beyond an OrcaSlicer folder: where it
// is, and Bambu's network file. Plain file work: no GUI, no AppConfig.

#include <filesystem>

namespace Slic3r::GUI::JusPrin::V1Import {

// Where JusPrin 1 keeps its folder: its default folder for the platform,
// whatever folder JusPrin 2 was started with, so a run on a scratch
// --datadir still finds the person's JusPrin 1.
std::filesystem::path v1_folder();

// Copies BambuNetworkEngine.conf, the Bambu network plug-in's settings, from
// JusPrin 1's folder `v1` into `data_dir`, when `v1` has one and `data_dir`
// has none of its own yet. Whether it copied.
bool copy_network_settings(const std::filesystem::path& v1, const std::filesystem::path& data_dir);

} // namespace Slic3r::GUI::JusPrin::V1Import
