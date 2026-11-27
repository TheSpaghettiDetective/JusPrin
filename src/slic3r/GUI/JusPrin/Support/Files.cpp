#include "Files.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <system_error>

namespace Slic3r::GUI::JusPrin {

void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    // Appended to the path itself: path.string() on Windows converts to the
    // system's code page, and throws for a name with characters outside it.
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            throw std::runtime_error("could not create " + temp.u8string() + ": " + std::strerror(errno));
        out << content;
        out.close();
        if (!out)
            throw std::runtime_error("could not write " + temp.u8string() + ": " + std::strerror(errno));
    }
    std::filesystem::rename(temp, path);
}

bool system_refused(const std::filesystem::filesystem_error& error, const std::filesystem::path& folder)
{
    const std::error_code code = error.code();
    if (code != std::errc::permission_denied && code != std::errc::operation_not_permitted &&
        code != std::errc::read_only_file_system && code != std::errc::no_space_on_device)
        return false;
    const std::filesystem::path& path = error.path1().empty() ? error.path2() : error.path1();
    const std::filesystem::path  inside = path.lexically_normal().lexically_relative(folder.lexically_normal());
    return !path.empty() && !inside.empty() && *inside.begin() != "..";
}

} // namespace Slic3r::GUI::JusPrin
