#include "Files.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

namespace Slic3r::GUI::JusPrin {

void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path temp = path.string() + ".tmp";
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

} // namespace Slic3r::GUI::JusPrin
