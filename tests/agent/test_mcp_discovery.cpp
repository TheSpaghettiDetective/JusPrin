#include <catch2/catch_all.hpp>
#include "mcp_test_directory.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"
#include <atomic>
#include <fstream>
#include <future>
#include <thread>

using namespace Slic3r::GUI::JusPrin;
namespace fs = std::filesystem;
using nlohmann::json;

TEST_CASE("MCP discovery ownership distinguishes runtimes with the same PID", "[mcp][discovery]")
{
    JusPrinTest::McpDirectory directory;
    Workspace::FakeWorkspace workspace;
    Agent::ToolExecutionCoordinator coordinator(workspace);
    auto older = std::make_unique<Mcp::McpRuntime>(workspace, coordinator, directory.path());
    const auto first = Mcp::read_discovery(directory.path());
    REQUIRE(first);
    CHECK(first->url == older->server().url());
    auto newer = std::make_unique<Mcp::McpRuntime>(workspace, coordinator, directory.path());
    const auto second = Mcp::read_discovery(directory.path());
    REQUIRE(second);
    CHECK(second->pid == first->pid);
    CHECK(second->instance_id != first->instance_id);
    CHECK(second->url == newer->server().url());
    older.reset();
    REQUIRE(Mcp::read_discovery(directory.path()));
    CHECK(Mcp::read_discovery(directory.path())->instance_id == second->instance_id);
    newer.reset();
    CHECK_FALSE(fs::exists(directory.path()));
}

TEST_CASE("MCP discovery publication is atomic and private", "[mcp][discovery]")
{
    JusPrinTest::McpDirectory directory;
    Mcp::write_discovery(directory.path(), "http://127.0.0.1:12345/mcp");
    std::atomic<bool> finished{false};
    unsigned missing = 0;
    std::thread reader([&] {
        while (!finished.load()) if (!Mcp::read_discovery(directory.path())) ++missing;
    });
    for (unsigned i = 0; i < 50; ++i) Mcp::write_discovery(directory.path(), "http://127.0.0.1:12345/mcp");
    finished.store(true);
    reader.join();
    CHECK(missing == 0);
#ifndef _WIN32
    CHECK((fs::status(directory.path()).permissions() & fs::perms::all) ==
          (fs::perms::owner_read | fs::perms::owner_write));
#endif
    for (const auto& entry : fs::directory_iterator(directory.root))
        CHECK(entry.path().extension() != ".tmp");
}

TEST_CASE("MCP discovery rejects malformed stale and nonlocal records", "[mcp][discovery]")
{
    JusPrinTest::McpDirectory directory;
    CHECK_FALSE(Mcp::read_discovery(directory.path()));
    Mcp::write_discovery(directory.path(), "http://127.0.0.1:12345/mcp");
    json value;
    { std::ifstream input(directory.path()); input >> value; }
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6);
    if (scenario == 0) value["pid"] = 2147483647;
    if (scenario == 1) value["schemaVersion"] = 2;
    if (scenario == 2) value["instanceId"] = "";
    if (scenario == 3) value["protocolVersions"] = "wrong";
    if (scenario == 4) value["url"] = "http://example.com:12345/mcp";
    if (scenario == 5) value["startedAt"] = nullptr;
    { std::ofstream output(directory.path()); output << (scenario == 6 ? "{" : value.dump()); }
    CHECK_FALSE(Mcp::read_discovery(directory.path()));
}

TEST_CASE("MCP discovery cannot expand access beyond numeric loopback", "[mcp][discovery]")
{
    const auto url = GENERATE("https://127.0.0.1:47301/mcp", "http://localhost:47301/mcp",
        "http://127.0.0.1:0/mcp", "http://127.0.0.1:65536/mcp", "http://127.0.0.1:47301/mcp?x=1",
        "http://127.0.0.1:47301@evil.example/mcp", "http://127.0.0.1:47301/other");
    CHECK_FALSE(Mcp::loopback_port(url));
    CHECK(Mcp::loopback_port("http://127.0.0.1:47301/mcp") == 47301);
}

TEST_CASE("MCP discovery serializes concurrent replacement and old-owner removal", "[mcp][discovery]")
{
    JusPrinTest::McpDirectory directory;
    const auto path = directory.root / fs::u8path("path with spaces-打印") / "mcp.json";
    for (int i = 0; i < 20; ++i) {
        const auto old = Mcp::write_discovery(path, "http://127.0.0.1:12345/mcp");
        auto removal = std::async(std::launch::async, [&] { return Mcp::remove_discovery(path, old.instance_id); });
        const auto latest = Mcp::write_discovery(path, "http://127.0.0.1:12346/mcp");
        removal.get();
        const auto read = Mcp::read_discovery(path);
        REQUIRE(read);
        CHECK(read->instance_id == latest.instance_id);
        CHECK(read->url == latest.url);
    }
}

TEST_CASE("MCP discovery startup errors are visible and shutdown tolerates a removed directory", "[mcp][discovery]")
{
    JusPrinTest::McpDirectory directory;
    std::ofstream(directory.root / "not-a-directory") << "test";
    REQUIRE_THROWS_AS(Mcp::write_discovery(directory.root / "not-a-directory" / "mcp.json",
                                          "http://127.0.0.1:12345/mcp"), fs::filesystem_error);
    Workspace::FakeWorkspace workspace;
    Agent::ToolExecutionCoordinator coordinator(workspace);
    auto runtime = std::make_unique<Mcp::McpRuntime>(workspace, coordinator, directory.path());
    fs::remove_all(directory.root);
    REQUIRE_NOTHROW(runtime.reset());
}

#ifndef _WIN32
TEST_CASE("MCP discovery permission loss leaves a safe stale file on shutdown", "[mcp][discovery]")
{
    JusPrinTest::McpDirectory directory;
    Workspace::FakeWorkspace workspace;
    Agent::ToolExecutionCoordinator coordinator(workspace);
    auto runtime = std::make_unique<Mcp::McpRuntime>(workspace, coordinator, directory.path());
    fs::permissions(directory.root, fs::perms::owner_read | fs::perms::owner_exec);
    CHECK_THROWS_AS(Mcp::write_discovery(directory.path(), "http://127.0.0.1:12345/mcp"), std::ios_base::failure);
    CHECK_NOTHROW(runtime.reset());
    const bool stale_file_retained = fs::exists(directory.path());
    fs::permissions(directory.root, fs::perms::owner_all);
    CHECK(stale_file_retained);
}
#endif
