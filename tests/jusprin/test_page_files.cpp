#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/JusPrin/Support/PageFiles.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

using namespace Slic3r::GUI::JusPrin;

namespace {

// A pages folder holding home/index.html, and secret.txt beside the folder.
struct PagesFolder
{
    std::filesystem::path top  = std::filesystem::temp_directory_path() / ("jusprin-page-files-" + std::to_string(std::random_device{}()));
    std::filesystem::path root = top / "pages";

    PagesFolder()
    {
        std::filesystem::create_directories(root / "home");
        std::ofstream(root / "home" / "index.html") << "<!doctype html>";
        std::ofstream(top / "secret.txt") << "secret";
    }
    ~PagesFolder() { std::filesystem::remove_all(top); }
};

} // namespace

TEST_CASE("A page address names the file under the pages folder", "[PageFiles]")
{
    const PagesFolder pages;
    const std::optional<std::filesystem::path> file = page_file(pages.root, "/home/index.html");
    REQUIRE(file.has_value());
    CHECK(std::filesystem::equivalent(*file, pages.root / "home" / "index.html"));
}

TEST_CASE("A page address names nothing outside the pages folder", "[PageFiles]")
{
    const PagesFolder pages;
    CHECK_FALSE(page_file(pages.root, "/../secret.txt").has_value());
    CHECK_FALSE(page_file(pages.root, "/home/../../secret.txt").has_value());
    CHECK_FALSE(page_file(pages.root, "/home/..\\..\\secret.txt").has_value());
    CHECK_FALSE(page_file(pages.root, "//secret.txt").has_value());
    CHECK_FALSE(page_file(pages.root, "/./home/index.html").has_value());
}

TEST_CASE("A page address of a folder or a missing file names nothing", "[PageFiles]")
{
    const PagesFolder pages;
    CHECK_FALSE(page_file(pages.root, "/home").has_value());
    CHECK_FALSE(page_file(pages.root, "/home/").has_value());
    CHECK_FALSE(page_file(pages.root, "/").has_value());
    CHECK_FALSE(page_file(pages.root, "").has_value());
    CHECK_FALSE(page_file(pages.root, "/home/missing.html").has_value());
}

TEST_CASE("A page file names the content type its kind needs", "[PageFiles]")
{
    CHECK(page_file_type("/pages/home/index.html") == "text/html");
    CHECK(page_file_type("/pages/home/INDEX.HTML") == "text/html");
    CHECK(page_file_type("/pages/home/app.js") == "text/javascript");
    CHECK(page_file_type("/pages/home/app.css") == "text/css");
    CHECK(page_file_type("/pages/images/logo.svg") == "image/svg+xml");
    CHECK(page_file_type("/pages/home/protocol.json") == "application/json");
    CHECK(page_file_type("/pages/startup/splash.webp").empty());
    CHECK(page_file_type("/pages/home/README").empty());
}
