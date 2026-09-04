#include <catch2/catch_all.hpp>
#include "slic3r/GUI/JusPrin/Mcp/McpConnections.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpConfigFile.hpp"
#include "mcp_test_directory.hpp"
#include <fstream>

using namespace Slic3r::GUI::JusPrin;
using nlohmann::json;

TEST_CASE("MCP connection entries carry literal Unicode and space paths", "[mcp][connections]")
{
    const std::string bridge = "/Applications/Kenny's 打印.app/Contents/MacOS/jusprin-mcp";
    const std::string discovery = "/Users/Kenny/Library/Application Support/JusPrin2/jusprin/mcp.json";
    const auto entries = Mcp::connection_entries(bridge, discovery, false);
    REQUIRE(entries.size() == 6);
    CHECK(entries[0].text.find("--scope user") != std::string::npos);
    CHECK(entries[0].text.find("Kenny'\"'\"'s") != std::string::npos);
    CHECK(entries[1].text.find("codex mcp add jusprin -- ") == 0);
    for (std::size_t i = 2; i < entries.size(); ++i) {
        const auto value = json::parse(entries[i].text);
        const auto& server = value.at(i == 5 ? "servers" : "mcpServers").at("jusprin");
        CHECK(server["command"] == bridge);
        CHECK(server["args"] == json::array({"--discovery", discovery}));
        CHECK_FALSE(server.contains("url"));
        CHECK_FALSE(server.contains("env"));
    }
    CHECK(json::parse(entries[5].text)["servers"]["jusprin"]["type"] == "stdio");
    CHECK(entries[2].text == entries[3].text);
}

TEST_CASE("MCP copied shell commands quote metacharacters without expansion", "[mcp][connections]")
{
    CHECK(Mcp::quote_argument("a'b $HOME `cmd`; c", false) == "'a'\"'\"'b $HOME `cmd`; c'");
    CHECK(Mcp::quote_argument("C:\\Kenny's $home\\jusprin-mcp.exe", true) == "'C:\\Kenny''s $home\\jusprin-mcp.exe'");
    const auto entries = Mcp::connection_entries("C:\\Program Files\\JusPrin\\jusprin-mcp.exe", "C:\\Users\\打印\\mcp.json", true);
    CHECK(entries[0].text.find("-- 'C:\\Program Files\\JusPrin\\jusprin-mcp.exe'") != std::string::npos);
    const auto config = json::parse(entries[2].text);
    CHECK(config["mcpServers"]["jusprin"]["args"][1] == "C:\\Users\\打印\\mcp.json");
}

TEST_CASE("MCP AppImage entries use the installed image and dispatch argument", "[mcp][connections]")
{
    const std::string image = "/home/Kenny's apps/JusPrin 打印.AppImage";
    const std::string discovery = "/home/Kenny/.config/JusPrin/jusprin/mcp.json";
    const auto entries = Mcp::connection_entries(image, discovery, false, {"--mcp-bridge"});
    const std::vector<std::string> args{"--mcp-bridge", "--discovery", discovery};
    for (const auto& entry : entries) {
        if (entry.cli) {
            REQUIRE(entry.arguments.size() >= 4);
            CHECK(std::vector<std::string>(entry.arguments.end() - 3, entry.arguments.end()) == args);
            CHECK(entry.arguments[entry.arguments.size() - 4] == image);
            CHECK(entry.text.find("'--mcp-bridge' '--discovery'") != std::string::npos);
            CHECK(entry.text.find("Kenny'\"'\"'s apps") != std::string::npos);
        } else {
            const auto value = json::parse(entry.text);
            const auto& server = value.at(entry.id == "code" ? "servers" : "mcpServers").at("jusprin");
            CHECK(server["command"] == image);
            CHECK(server["args"] == args);
        }
    }
}

namespace {
void fixture(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    REQUIRE(out.good());
}
std::string contents(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}

TEST_CASE("MCP JSON setup preserves unrelated bytes and keeps a recovery backup", "[mcp][connections]")
{
    JusPrinTest::McpDirectory directory;
    const auto path = directory.path();
    const std::string original = "{\n// personal settings\n\"theme\": \"dark\",\n\"mcpServers\": {\n"
        "\"other\": {\"command\": \"keep\", \"env\": {\"KEY\": \"fixture\"}}, // keep me\n"
        "\"jusprin\": {\"url\": \"http://127.0.0.1:1/mcp\"},\n},\n}\n";
    fixture(path, original);
    const json server{{"command", "/Applications/打印.app/jusprin-mcp"}, {"args", json::array({"--discovery", "/data path/mcp.json"})}};
    const auto edit = Mcp::prepare_json_connection(path, "mcpServers", server);
    CHECK(contents(path) == original); // preview is read-only
    const auto begin = original.find("{\"url\"");
    const auto end = original.find('}', begin) + 1;
    CHECK(edit.after == original.substr(0, begin) + server.dump(2) + original.substr(end));
    const auto backup = Mcp::apply_config_edit(edit);
    CHECK(contents(backup) == original);
    CHECK(contents(path) == edit.after);
    const auto again = Mcp::prepare_json_connection(path, "mcpServers", server);
    CHECK(again.after == edit.after);
    CHECK(Mcp::apply_config_edit(again).empty());
}

TEST_CASE("MCP setup handles new files and missing or empty server maps", "[mcp][connections]")
{
    JusPrinTest::McpDirectory directory;
    const json server{{"type", "stdio"}, {"command", "helper"}};
    for (const auto& initial : {std::string("{\"setting\": [1,2,3]}"), std::string("{\"servers\": {/* keep */}}"),
                               std::string("{\"servers\": {\"other\": {\"command\": \"keep\"},}}")}) {
        fixture(directory.path(), initial);
        const auto edit = Mcp::prepare_json_connection(directory.path(), "servers", server);
        CHECK(edit.after.find("\"jusprin\"") != std::string::npos);
        CHECK_FALSE(Mcp::apply_config_edit(edit).empty());
    }
    const auto missing = directory.path().parent_path() / "new" / "mcp.json";
    const auto edit = Mcp::prepare_json_connection(missing, "servers", server);
    CHECK_FALSE(std::filesystem::exists(missing));
    CHECK(Mcp::apply_config_edit(edit).empty());
    CHECK(json::parse(contents(missing))["servers"]["jusprin"] == server);
}

TEST_CASE("MCP setup refuses stale previews and malformed configuration", "[mcp][connections]")
{
    JusPrinTest::McpDirectory directory;
    const json server{{"command", "helper"}};
    fixture(directory.path(), "{}");
    const auto edit = Mcp::prepare_json_connection(directory.path(), "servers", server);
    fixture(directory.path(), "{\"concurrent\":true}");
    CHECK_THROWS_WITH(Mcp::apply_config_edit(edit), Catch::Matchers::ContainsSubstring("changed after the preview"));
    CHECK(contents(directory.path()) == "{\"concurrent\":true}");
    for (const auto& bad : {"{", "[]", "{\"servers\": []}", "{/* unfinished", "{\"servers\":{},\"servers\":{}}"}) {
        fixture(directory.path(), bad);
        CHECK_THROWS(Mcp::prepare_json_connection(directory.path(), "servers", server));
        CHECK(contents(directory.path()) == bad);
    }
}

TEST_CASE("MCP setup preserves a UTF-8 BOM and Windows line endings", "[mcp][connections]")
{
    JusPrinTest::McpDirectory directory;
    const std::string original = "\xef\xbb\xbf{\r\n\"servers\": {\"jusprin\": {\"command\": \"old\"}},\r\n\"theme\": \"dark\"\r\n}\r\n";
    fixture(directory.path(), original);
    const json server{{"command", "new"}};
    const auto edit = Mcp::prepare_json_connection(directory.path(), "servers", server);
    const auto begin = original.find("{\"command\"");
    const auto end = original.find('}', begin) + 1;
    CHECK(edit.after == original.substr(0, begin) + server.dump(2) + original.substr(end));
    const auto backup = Mcp::apply_config_edit(edit);
    CHECK(contents(backup) == original);
    CHECK(json::parse(contents(directory.path()))["servers"]["jusprin"] == server);
}

#ifndef _WIN32
TEST_CASE("MCP setup refuses symlink replacement and preserves file permissions", "[mcp][connections]")
{
    namespace fs = std::filesystem;
    JusPrinTest::McpDirectory directory;
    const json server{{"command", "helper"}};
    const auto target = directory.root / "target.json";
    fixture(target, "{}");
    fs::create_symlink(target, directory.path());
    CHECK_THROWS_WITH(Mcp::prepare_json_connection(directory.path(), "servers", server),
                      Catch::Matchers::ContainsSubstring("symbolic link"));
    CHECK(contents(target) == "{}");
    fs::remove(directory.path());
    fixture(directory.path(), "{}");
    const auto permissions = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read;
    fs::permissions(directory.path(), permissions);
    const auto edit = Mcp::prepare_json_connection(directory.path(), "servers", server);
    const auto backup = Mcp::apply_config_edit(edit);
    CHECK((fs::status(directory.path()).permissions() & fs::perms::all) == permissions);
    CHECK((fs::status(backup).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));
    const auto stale = Mcp::prepare_json_connection(directory.path(), "servers", json{{"command", "replacement"}});
    fs::remove(directory.path());
    fs::create_symlink(target, directory.path());
    CHECK_THROWS_WITH(Mcp::apply_config_edit(stale), Catch::Matchers::ContainsSubstring("symbolic link"));
    CHECK(contents(target) == "{}");
}
#endif
