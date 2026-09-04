#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Mcp {

struct DiscoveryRecord
{
    std::string url;
    std::int64_t pid;
    std::string instance_id;
    std::string app_version;
    std::string started_at;
};

// The caller owns HTTP liveness probing; no network operation runs on the GUI
// thread. read_discovery validates the file, numeric loopback URL and PID only.
std::optional<unsigned short> loopback_port(const std::string& url);
std::optional<DiscoveryRecord> read_discovery(const std::filesystem::path& path);
DiscoveryRecord write_discovery(const std::filesystem::path& path, const std::string& url);
bool remove_discovery(const std::filesystem::path& path, const std::string& instance_id);
std::string mcp_build_version();

} // namespace Slic3r::GUI::JusPrin::Mcp
