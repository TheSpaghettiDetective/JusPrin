#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin {

// The file under `root` that the path of a page address names, such as
// /home/index.html for root/home/index.html (Support/PageScheme.hpp). The
// path is the address's path alone, unescaped, without its query. Nothing
// outside `root`: none when a part of the path is empty, "." or "..", or holds
// a backslash, or when no regular file is there.
std::optional<std::filesystem::path> page_file(const std::filesystem::path& root, const std::string& path);

// The content type a web view must be told for a page file of this kind, such
// as text/html for index.html; empty for a kind the web view works out itself,
// such as an image. Not left to the system's own list: on Linux, wxWidgets
// reads that .html is application/xhtml+xml, and WebKitGTK then parses the
// page as XML and shows an error in its place.
std::string page_file_type(const std::filesystem::path& file);

} // namespace Slic3r::GUI::JusPrin
