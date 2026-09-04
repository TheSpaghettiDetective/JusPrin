#include "McpDiscoveryFile.hpp"
#include "McpProtocol.hpp"
#include "libslic3r_version.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Slic3r::GUI::JusPrin::Mcp {
namespace fs = std::filesystem;
using nlohmann::json;
namespace {
std::mutex discovery_mutex;

// Never unlink the lock file: different processes must lock the same inode.
class FileLock
{
public:
    explicit FileLock(const fs::path& path) : local(discovery_mutex)
    {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw fs::filesystem_error("Open MCP discovery lock", path, std::error_code(GetLastError(), std::system_category()));
        if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped)) {
            const auto error = GetLastError();
            CloseHandle(handle);
            throw fs::filesystem_error("Lock MCP discovery", path, std::error_code(error, std::system_category()));
        }
#else
        fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd < 0) throw fs::filesystem_error("Open MCP discovery lock", path, std::error_code(errno, std::generic_category()));
        int result;
        do { result = flock(fd, LOCK_EX); } while (result < 0 && errno == EINTR);
        if (result < 0) {
            const int error = errno;
            ::close(fd);
            throw fs::filesystem_error("Lock MCP discovery", path, std::error_code(error, std::generic_category()));
        }
#endif
    }
    ~FileLock()
    {
#ifdef _WIN32
        UnlockFileEx(handle, 0, 1, 0, &overlapped);
        CloseHandle(handle);
#else
        ::close(fd);
#endif
    }
private:
    std::unique_lock<std::mutex> local;
#ifdef _WIN32
    HANDLE handle;
    OVERLAPPED overlapped{};
#else
    int fd;
#endif
};

std::int64_t process_id()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

bool process_alive(std::int64_t pid)
{
    if (pid <= 0 || pid > 0x7fffffff) return false;
#ifdef _WIN32
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    CloseHandle(handle);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
}

std::string mcp_build_version()
{
    return SLIC3R_VERSION;
}

std::optional<unsigned short> loopback_port(const std::string& url)
{
    constexpr std::string_view prefix = "http://127.0.0.1:";
    if (url.compare(0, prefix.size(), prefix) != 0 || url.size() <= prefix.size() + 4 ||
        url.compare(url.size() - 4, 4, "/mcp") != 0) return std::nullopt;
    const auto digits = std::string_view(url).substr(prefix.size(), url.size() - prefix.size() - 4);
    unsigned value = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size() || value == 0 || value > 65535)
        return std::nullopt;
    return static_cast<unsigned short>(value);
}

std::optional<DiscoveryRecord> read_discovery(const fs::path& path)
{
    std::string bytes(4097, '\0');
#ifdef _WIN32
    // Permit atomic replacement while this reader still holds the old file.
    HANDLE input = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) return std::nullopt;
    DWORD size = 0;
    const bool read = ReadFile(input, bytes.data(), DWORD(bytes.size()), &size, nullptr) != 0;
    CloseHandle(input);
    if (!read) return std::nullopt;
    bytes.resize(size);
#else
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt; // Absent/unreadable discovery is offline, not a GUI failure.
    input.read(bytes.data(), bytes.size());
    bytes.resize(std::size_t(input.gcount()));
#endif
    if (bytes.size() > 4096) return std::nullopt;
    const auto value = json::parse(bytes, nullptr, false);
    if (!value.is_object() || value.value("schemaVersion", json()) != 1 ||
        !value.value("pid", json()).is_number_integer()) return std::nullopt;
    for (const auto* key : {"url", "instanceId", "appVersion", "startedAt"})
        if (!value.value(key, json()).is_string() || value[key].get_ref<const std::string&>().empty()) return std::nullopt;
    const auto versions = value.value("protocolVersions", json());
    if (!versions.is_array() || versions.empty()) return std::nullopt;
    for (const auto& version : versions) if (!version.is_string()) return std::nullopt;
    const auto pid = value["pid"].get<std::int64_t>();
    const auto url = value["url"].get<std::string>();
    if (!process_alive(pid) || !loopback_port(url)) return std::nullopt;
    return DiscoveryRecord{url, pid, value["instanceId"], value["appVersion"], value["startedAt"]};
}

DiscoveryRecord write_discovery(const fs::path& path, const std::string& url)
{
    if (path.empty() || !loopback_port(url)) throw std::invalid_argument("Invalid MCP discovery path or URL");
    fs::create_directories(path.parent_path());
    FileLock lock(fs::u8path(path.u8string() + ".lock"));
    DiscoveryRecord record{url, process_id(), boost::uuids::to_string(boost::uuids::random_generator()()),
                           mcp_build_version(), timestamp()};
    const auto temporary = fs::u8path(path.u8string() + "." + record.instance_id + ".tmp");
    const json body{{"schemaVersion", 1}, {"url", url}, {"pid", record.pid}, {"instanceId", record.instance_id},
                    {"appVersion", record.app_version}, {"protocolVersions", json::array({kProtocolVersion})},
                    {"startedAt", record.started_at}};
    // Filesystem exceptions surface at startup. A failed temporary write is
    // never published, so readers keep the previous complete record.
    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(temporary, std::ios::binary | std::ios::trunc);
#ifndef _WIN32
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write);
#endif
    output << body.dump(2) << '\n';
    output.close();
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("Publish MCP discovery", temporary, path, std::error_code(GetLastError(), std::system_category()));
#else
    fs::rename(temporary, path);
#endif
    return record;
}

bool remove_discovery(const fs::path& path, const std::string& instance_id)
{
    if (!fs::exists(path.parent_path())) return false;
    FileLock lock(fs::u8path(path.u8string() + ".lock"));
    const auto current = read_discovery(path);
    return current && current->instance_id == instance_id && fs::remove(path);
}
} // namespace Slic3r::GUI::JusPrin::Mcp
