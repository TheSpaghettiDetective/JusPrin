// Unit tests for what JusPrin 1's import adds to an OrcaSlicer import: Bambu's
// network file, copied only when JusPrin 2 has none of its own.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/V1Import/V1Import.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Slic3r::GUI::JusPrin;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path)
{
    std::ifstream     in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// A JusPrin 1 folder with Bambu's network file, and an empty JusPrin 2
// folder beside it.
struct Folders
{
    fs::path root;
    fs::path v1;
    fs::path v2;

    Folders()
    {
        // A folder of its own: test cases run as separate processes, at once
        // under ctest -j, and each deletes its folder when done.
        root = fs::temp_directory_path() / ("jusprin-v1-import-" + boost::uuids::to_string(boost::uuids::random_generator()()));
        v1   = root / "JusPrin";
        v2   = root / "JusPrin2";
        fs::create_directories(v2);
        write(v1 / "BambuNetworkEngine.conf", "{\"from\": \"v1\"}");
    }
    ~Folders() { fs::remove_all(root); }
};

} // namespace

TEST_CASE("JusPrin 1's Bambu network file is copied when JusPrin 2 has none", "[v1_import]")
{
    Folders f;
    CHECK(V1Import::copy_network_settings(f.v1, f.v2));
    CHECK(read(f.v2 / "BambuNetworkEngine.conf") == "{\"from\": \"v1\"}");
    // JusPrin 1's folder is only read.
    CHECK(read(f.v1 / "BambuNetworkEngine.conf") == "{\"from\": \"v1\"}");
}

TEST_CASE("JusPrin 2's own Bambu network file is never replaced, and nothing is copied without one", "[v1_import]")
{
    Folders f;
    write(f.v2 / "BambuNetworkEngine.conf", "{\"from\": \"v2\"}");
    CHECK_FALSE(V1Import::copy_network_settings(f.v1, f.v2));
    CHECK(read(f.v2 / "BambuNetworkEngine.conf") == "{\"from\": \"v2\"}");

    Folders g;
    fs::remove(g.v1 / "BambuNetworkEngine.conf");
    CHECK_FALSE(V1Import::copy_network_settings(g.v1, g.v2));
    CHECK_FALSE(fs::exists(g.v2 / "BambuNetworkEngine.conf"));
}
