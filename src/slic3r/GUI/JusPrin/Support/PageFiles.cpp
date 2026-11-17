#include "PageFiles.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace Slic3r::GUI::JusPrin {

std::optional<std::filesystem::path> page_file(const std::filesystem::path& root, const std::string& path)
{
    if (path.size() < 2 || path.front() != '/')
        return std::nullopt;
    std::filesystem::path file = root;
    std::string::size_type start = 1;
    while (start <= path.size()) {
        const std::string::size_type end  = std::min(path.find('/', start), path.size());
        const std::string            part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.find('\\') != std::string::npos)
            return std::nullopt;
        file /= std::filesystem::u8path(part);
        start = end + 1;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
        return std::nullopt;
    return file;
}

std::string page_file_type(const std::filesystem::path& file)
{
    std::string extension = file.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".html")
        return "text/html";
    if (extension == ".js" || extension == ".mjs")
        return "text/javascript";
    if (extension == ".css")
        return "text/css";
    if (extension == ".svg")
        return "image/svg+xml";
    if (extension == ".json")
        return "application/json";
    return {};
}

} // namespace Slic3r::GUI::JusPrin
