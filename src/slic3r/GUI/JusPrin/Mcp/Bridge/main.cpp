#include "Bridge.hpp"
#include "libslic3r_version.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;
namespace Mcp = Slic3r::GUI::JusPrin::Mcp;
namespace {
std::string environment(const char* key)
{
    const char* value = std::getenv(key);
    return value ? value : "";
}

// Windows hands main() its arguments and getenv() its values in the active code
// page, which cannot spell every path a user's data directory may contain.
// Anything that becomes a path therefore comes from the wide APIs; on POSIX the
// same bytes already are the native encoding.
#ifdef _WIN32
using NativeString = std::wstring;
#else
using NativeString = std::string;
#endif

NativeString native(const char* ascii) { return NativeString(ascii, ascii + std::strlen(ascii)); }

fs::path native_path(const NativeString& value)
{
#ifdef _WIN32
    return fs::path(value); // already UTF-16; u8path does not accept it
#else
    return fs::u8path(value);
#endif
}

NativeString environment_path(const char* key)
{
#ifdef _WIN32
    const auto name = native(key);
    std::wstring value(MAX_PATH, L'\0');
    auto size = GetEnvironmentVariableW(name.c_str(), value.data(), DWORD(value.size()));
    if (size > value.size()) {
        value.resize(size);
        size = GetEnvironmentVariableW(name.c_str(), value.data(), DWORD(value.size()));
    }
    value.resize(size);
    return value;
#else
    return environment(key);
#endif
}

std::vector<NativeString> command_arguments(int argc, char** argv)
{
#ifdef _WIN32
    (void) argc; (void) argv;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) throw std::runtime_error("Cannot read the command line");
    std::vector<NativeString> arguments(wide + 1, wide + count);
    LocalFree(wide);
    return arguments;
#else
    std::vector<NativeString> arguments;
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    return arguments;
#endif
}

fs::path executable_path()
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!size || size == buffer.size()) throw std::runtime_error("Cannot locate jusprin-mcp executable");
    buffer.resize(size); return fs::path(buffer);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size)) throw std::runtime_error("Cannot locate jusprin-mcp executable");
    return fs::weakly_canonical(fs::u8path(buffer.c_str()));
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}

fs::path default_discovery_path()
{
    auto root = executable_path().parent_path();
#ifdef __APPLE__
    if (root.filename() == "MacOS" && root.parent_path().filename() == "Contents")
        root = root.parent_path().parent_path().parent_path();
#endif
    if (fs::is_directory(root / "data_dir")) return root / "data_dir" / "jusprin" / "mcp.json";
#ifdef _WIN32
    const auto base = environment_path("APPDATA");
#elif defined(__APPLE__)
    const auto home = environment_path("HOME");
    const auto base = home.empty() ? "" : home + "/Library/Application Support";
#else
    const auto home = environment_path("HOME");
    const auto xdg = environment_path("XDG_CONFIG_HOME");
    const auto base = !xdg.empty() ? xdg : home.empty() ? "" : home + "/.config";
#endif
    if (base.empty()) throw std::runtime_error("Cannot locate application data; pass --discovery with the JusPrin discovery-file path");
    return native_path(base) / SLIC3R_APP_KEY / "jusprin" / "mcp.json";
}

// Decouple pipe backpressure from network cancellation and shutdown. The queue
// is bounded; a client that never reads cannot grow the bridge indefinitely.
class Output
{
public:
    Output()
    {
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
        m_flags = fcntl(STDOUT_FILENO, F_GETFL);
        if (m_flags < 0 || fcntl(STDOUT_FILENO, F_SETFL, m_flags | O_NONBLOCK) < 0) fatal();
#endif
        m_worker = std::thread([this] { run(); });
    }
    ~Output() { stop(); }
    void send(Mcp::Bridge::Delivery delivery)
    {
        std::string bytes = delivery.message.dump() + '\n';
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) return;
        if (m_bytes + bytes.size() > 1024 * 1024) fatal();
        m_bytes += bytes.size();
        m_queue.push_back({std::move(bytes), std::move(delivery.cancelled)});
        m_ready.notify_one();
    }
    void stop()
    {
        m_stopped = true;
        m_ready.notify_all();
        if (!m_worker.joinable()) return;
#ifdef _WIN32
        // Standard anonymous pipes are synchronous on Windows. Repeatedly
        // cancel until the worker exits, including a write started just after
        // the first cancellation. No forced thread termination is used.
        while (WaitForSingleObject(m_worker.native_handle(), 10) == WAIT_TIMEOUT)
            CancelSynchronousIo(m_worker.native_handle());
#endif
        m_worker.join();
#ifndef _WIN32
        fcntl(STDOUT_FILENO, F_SETFL, m_flags);
#endif
    }
private:
    [[noreturn]] static void fatal()
    {
        std::fputs("jusprin-mcp: stdout failed or the client stopped consuming responses\n", stderr);
        // Broken stdout cannot carry an MCP error. Process exit also closes all
        // request sockets, cancelling pending native approvals.
        std::_Exit(1);
    }
    void run()
    {
        for (;;) {
            Frame frame;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait(lock, [this] { return m_stopped || !m_queue.empty(); });
                if (m_stopped) return;
                frame = std::move(m_queue.front()); m_queue.pop_front(); m_bytes -= frame.bytes.size();
            }
            const auto& bytes = frame.bytes;
            std::size_t offset = 0;
            while (offset < bytes.size() && !m_stopped) {
                if (offset == 0 && frame.cancelled && frame.cancelled->load()) break;
#ifdef _WIN32
                DWORD written = 0;
                if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data() + offset,
                               DWORD(bytes.size() - offset), &written, nullptr)) {
                    if (m_stopped) return;
                    fatal();
                }
                offset += written;
#else
                const auto written = ::write(STDOUT_FILENO, bytes.data() + offset, bytes.size() - offset);
                if (written > 0) { offset += std::size_t(written); continue; }
                if (written < 0 && errno == EINTR) continue;
                if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    pollfd descriptor{STDOUT_FILENO, POLLOUT, 0};
                    ::poll(&descriptor, 1, 50);
                    continue;
                }
                fatal();
#endif
            }
        }
    }
    std::atomic<bool> m_stopped{false};
    std::mutex m_mutex;
    std::condition_variable m_ready;
    struct Frame { std::string bytes; std::shared_ptr<std::atomic<bool>> cancelled; };
    std::deque<Frame> m_queue;
    std::size_t m_bytes{0};
    std::thread m_worker;
#ifndef _WIN32
    int m_flags;
#endif
};
}

int main(int argc, char** argv)
{
    try {
        Mcp::Bridge::Config config;
        config.url_override = environment("JUSPRIN_MCP_URL");
        const auto configured = environment_path("JUSPRIN_MCP_DISCOVERY");
        if (!configured.empty()) config.discovery_path = native_path(configured);
        const auto arguments = command_arguments(argc, argv);
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& option = arguments[i];
            if (option == native("--discovery") && i + 1 < arguments.size()) config.discovery_path = native_path(arguments[++i]);
            else if (option == native("--version")) { std::cout << Mcp::mcp_build_version() << '\n'; return 0; }
            else { std::cerr << "Usage: jusprin-mcp [--discovery PATH] [--version]\n"; return 2; }
        }
        if (config.discovery_path.empty()) config.discovery_path = default_discovery_path();
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);
        Output output;
        Mcp::Bridge::Server server(std::move(config), [&output](auto message) { output.send(std::move(message)); },
                                   [](const auto& message) { std::cerr << "jusprin-mcp: " << message << '\n'; });
        std::string line;
        char c;
        while (std::cin.get(c)) {
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                server.submit(std::move(line)); line.clear();
            } else if (line.size() <= Mcp::kBodyLimit) line += c;
        }
        server.stop();
        output.stop();
        return std::cin.bad() ? 1 : 0;
    } catch (const std::exception& error) {
        // Process boundary: startup/argument/filesystem failures cannot be
        // represented as a response to a request that has not arrived.
        std::cerr << "jusprin-mcp: " << error.what() << '\n';
        return 1;
    }
}
