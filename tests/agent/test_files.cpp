// The file helpers JusPrin's own code writes with.

#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/JusPrin/Support/Files.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

using namespace Slic3r::GUI::JusPrin;
namespace fs = std::filesystem;

namespace {

fs::path fresh_folder()
{
    const fs::path folder = fs::temp_directory_path() / ("jusprin-files-" + std::to_string(std::random_device{}()));
    fs::create_directories(folder);
    return folder;
}

std::string read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("a written file holds all of its content, in a folder made for it", "[Files]")
{
    const fs::path folder = fresh_folder();
    const fs::path file   = folder / "inner" / "model.stl";
    write_file(file, "solid cube\nendsolid cube\n");
    CHECK(read(file) == "solid cube\nendsolid cube\n");
    fs::remove_all(folder);
}

#ifndef _WIN32
// A full disk, made small: the process may not write past 1 KiB in any file.
TEST_CASE("a write the file system cuts short is an error, not a shorter file", "[Files]")
{
    const fs::path folder = fresh_folder();
    const fs::path file   = folder / "model.stl";
    rlimit before{};
    REQUIRE(getrlimit(RLIMIT_FSIZE, &before) == 0);
    const auto ignored = std::signal(SIGXFSZ, SIG_IGN);
    rlimit small = before;
    small.rlim_cur = 1024;
    REQUIRE(setrlimit(RLIMIT_FSIZE, &small) == 0);
    bool threw = false;
    try {
        write_file(file, std::string(64 * 1024, 'x'));
    } catch (const std::exception&) {
        threw = true;
    }
    setrlimit(RLIMIT_FSIZE, &before);
    std::signal(SIGXFSZ, ignored);
    CHECK(threw);
    CHECK_FALSE(fs::exists(file));
    fs::remove_all(folder);
}
#endif
