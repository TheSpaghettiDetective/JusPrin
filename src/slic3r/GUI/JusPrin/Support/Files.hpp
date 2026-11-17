#pragma once

#include <filesystem>
#include <string>

namespace Slic3r::GUI::JusPrin {

// Writes `content` to `path`, creating its folder, through a temporary file
// renamed into place, so a crash never leaves a cut-short file. Throws with
// the file system's reason when any part of it fails.
void write_file(const std::filesystem::path& path, const std::string& content);

} // namespace Slic3r::GUI::JusPrin
