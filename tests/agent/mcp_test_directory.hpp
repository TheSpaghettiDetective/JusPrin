#pragma once
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <filesystem>

namespace JusPrinTest {
struct McpDirectory
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("jusprin-mcp-test-" + boost::uuids::to_string(boost::uuids::random_generator()()));
    McpDirectory() { std::filesystem::create_directory(root); }
    ~McpDirectory() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    std::filesystem::path path() const { return root / "mcp.json"; }
};
}
