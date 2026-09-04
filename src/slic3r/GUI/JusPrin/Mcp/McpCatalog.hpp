#pragma once

#include "McpConnections.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Mcp {

struct CatalogPaths
{
    std::filesystem::path home;
    std::filesystem::path config_home;
    bool windows{false};
};

struct CatalogItem
{
    ConnectionEntry entry;
    bool detected{false};
    std::filesystem::path config_path;
    std::string json_root;
};

CatalogPaths default_catalog_paths();
std::string command_on_path(const std::string& command);
std::vector<CatalogItem> make_catalog(const std::string& helper, const std::string& discovery, const CatalogPaths& paths,
                                      std::vector<std::string> launch_arguments = {});

} // namespace Slic3r::GUI::JusPrin::Mcp
