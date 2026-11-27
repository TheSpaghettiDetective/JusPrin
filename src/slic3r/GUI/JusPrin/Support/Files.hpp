#pragma once

#include <filesystem>
#include <string>

namespace Slic3r::GUI::JusPrin {

// Writes `content` to `path`, creating its folder, through a temporary file
// renamed into place, so a crash never leaves a cut-short file. Throws with
// the file system's reason when any part of it fails.
void write_file(const std::filesystem::path& path, const std::string& content);

// The system will not let a read of a path inside `folder`, or a write into
// it, happen: no permission, a read-only volume or a full disk. The person
// can change that. Any other file system error is ours.
bool system_refused(const std::filesystem::filesystem_error& error, const std::filesystem::path& folder);

} // namespace Slic3r::GUI::JusPrin
