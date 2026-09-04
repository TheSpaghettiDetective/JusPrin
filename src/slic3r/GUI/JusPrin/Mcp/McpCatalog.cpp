#include "McpCatalog.hpp"

#include <cstdlib>
#include <sstream>

namespace Slic3r::GUI::JusPrin::Mcp {
namespace fs = std::filesystem;

namespace {
std::string env(const char* key)
{
    const char* value = std::getenv(key);
    return value ? value : "";
}

bool executable_file(const fs::path& path)
{
    std::error_code error;
    const auto status = fs::status(path, error);
    if (error || !fs::is_regular_file(status)) return false;
#ifdef _WIN32
    return true;
#else
    return (status.permissions() & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) !=
           fs::perms::none;
#endif
}
} // namespace

CatalogPaths default_catalog_paths()
{
    CatalogPaths paths;
#ifdef _WIN32
    paths.windows = true;
    const auto profile = env("USERPROFILE");
    paths.home = profile.empty() ? fs::current_path() : fs::u8path(profile);
    const auto appdata = env("APPDATA");
    paths.config_home = appdata.empty() ? paths.home : fs::u8path(appdata);
#elif defined(__APPLE__)
    const auto home = env("HOME");
    paths.home = home.empty() ? fs::current_path() : fs::u8path(home);
    paths.config_home = paths.home / "Library" / "Application Support";
#else
    const auto home = env("HOME");
    paths.home = home.empty() ? fs::current_path() : fs::u8path(home);
    const auto xdg = env("XDG_CONFIG_HOME");
    paths.config_home = xdg.empty() ? paths.home / ".config" : fs::u8path(xdg);
#endif
    return paths;
}

std::string command_on_path(const std::string& command)
{
#ifdef _WIN32
    const std::string filename = command + ".exe";
    const std::string path = env("PATH");
    const char separator = ';';
#else
    const std::string filename = command;
    const std::string path = env("PATH");
    const char separator = ':';
#endif
    std::string directory;
    std::istringstream stream(path);
    while (std::getline(stream, directory, separator)) {
        if (directory.empty()) continue;
        const auto candidate = fs::u8path(directory) / filename;
        if (executable_file(candidate)) return candidate.u8string();
    }
#ifndef _WIN32
    const auto home = env("HOME");
    for (const auto& extra : {fs::u8path(home) / ".local" / "bin", fs::path("/opt/homebrew/bin"), fs::path("/usr/local/bin")}) {
        const auto candidate = extra / filename;
        if (executable_file(candidate)) return candidate.u8string();
    }
#endif
    return {};
}

std::vector<CatalogItem> make_catalog(const std::string& helper, const std::string& discovery, const CatalogPaths& paths,
                                      std::vector<std::string> launch_arguments)
{
    const auto entries = connection_entries(helper, discovery, paths.windows, std::move(launch_arguments));
    const std::vector<fs::path> configs{
        paths.home / ".claude.json",
        paths.home / ".codex" / "config.toml",
        paths.config_home / "Claude" / "claude_desktop_config.json",
        paths.config_home / "Claude" / "claude_desktop_config.json",
        paths.home / ".cursor" / "mcp.json",
        paths.config_home / "Code" / "User" / "mcp.json",
    };
    std::vector<CatalogItem> items;
    items.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        CatalogItem item;
        item.entry = entries[i];
        item.config_path = configs[i];
        if (!item.entry.cli) item.json_root = item.entry.id == "code" ? "servers" : "mcpServers";
        std::error_code error;
        item.detected = fs::exists(item.config_path, error) || !command_on_path(item.entry.id).empty();
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace Slic3r::GUI::JusPrin::Mcp
