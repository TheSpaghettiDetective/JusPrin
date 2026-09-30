#include <catch2/catch_all.hpp>

#include "project_archive_fixture.hpp"
#include "slic3r/GUI/JusPrin/Project/ArchiveCheck.hpp"

#include <fstream>

using namespace Slic3r::GUI::JusPrin::Project;
using namespace project_fixture;

namespace {

const std::vector<Object> two_objects{{"Cube_1", {{65537, "0"}}}, {"Sphere_2", {{65538, "0.5"}, {131074, "0.25"}}}};

} // namespace

TEST_CASE("A whole split archive verifies", "[ArchiveCheck]")
{
    const auto dir = fresh_dir("archive-check");
    REQUIRE(write_archive(dir / "full.3mf", two_objects, true));
    const ArchiveCheck check = verify_project_archive(dir / "full.3mf");
    INFO(check.problem);
    REQUIRE(check.ok);
    REQUIRE(check.meshes == 2);
    REQUIRE(check.entries == 6);
    REQUIRE(check.bytes > 0);
}

TEST_CASE("A backup archive verifies only when its meshes are known to live elsewhere", "[ArchiveCheck]")
{
    const auto dir = fresh_dir("archive-check-backup");
    REQUIRE(write_archive(dir / "backup.3mf", two_objects, false));
    const ArchiveCheck as_full = verify_project_archive(dir / "backup.3mf");
    REQUIRE_FALSE(as_full.ok);
    REQUIRE_THAT(as_full.problem, Catch::Matchers::ContainsSubstring("missing the mesh 3D/Objects/Cube_1.model"));
    REQUIRE(verify_project_archive(dir / "backup.3mf", /*meshes_elsewhere=*/true).ok);
}

TEST_CASE("A missing, empty, or truncated file fails", "[ArchiveCheck]")
{
    const auto dir = fresh_dir("archive-check-broken");
    REQUIRE_FALSE(verify_project_archive(dir / "none.3mf").ok);
    REQUIRE(verify_project_archive(dir / "none.3mf").problem == "The file is missing");

    std::ofstream(dir / "empty.3mf").close();
    REQUIRE_FALSE(verify_project_archive(dir / "empty.3mf").ok);

    REQUIRE(write_archive(dir / "full.3mf", two_objects, true));
    std::ifstream in(dir / "full.3mf", std::ios::binary);
    std::string   bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::ofstream out(dir / "truncated.3mf", std::ios::binary);
    out.write(bytes.data(), bytes.size() - 40);
    out.close();
    const ArchiveCheck truncated = verify_project_archive(dir / "truncated.3mf");
    REQUIRE_FALSE(truncated.ok);
    REQUIRE(truncated.problem == "The file is not a complete zip archive");
}

TEST_CASE("A damaged entry fails only when the data is inflated", "[ArchiveCheck]")
{
    const auto dir = fresh_dir("archive-check-damaged");
    REQUIRE(write_archive(dir / "full.3mf", two_objects, true));
    std::fstream file(dir / "full.3mf", std::ios::in | std::ios::out | std::ios::binary);
    std::string  bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // The first local header is 30 bytes plus the name; the compressed data
    // of "[Content_Types].xml" follows. Flip bytes in it.
    const std::size_t data = 30 + std::string("[Content_Types].xml").size();
    file.seekp(data + 2);
    file.put(static_cast<char>(bytes[data + 2] ^ 0x55));
    file.put(static_cast<char>(bytes[data + 3] ^ 0x55));
    file.close();
    REQUIRE_FALSE(verify_project_archive(dir / "full.3mf").ok);
    REQUIRE(verify_project_archive(dir / "full.3mf", false, /*inflate=*/false).ok);
}

TEST_CASE("Required free space is the archive plus a margin", "[ArchiveCheck]")
{
    REQUIRE(required_free_space(0) == (std::uint64_t(64) << 20));
    REQUIRE(required_free_space(31'467'851) == 31'467'851 + (std::uint64_t(64) << 20));
    const SpaceCheck here = check_free_space(std::filesystem::temp_directory_path(), 1);
    REQUIRE(here.required == required_free_space(1));
    REQUIRE(here.available > 0);
    const SpaceCheck nowhere = check_free_space(std::filesystem::temp_directory_path() / "does-not-exist-anywhere", 1);
    REQUIRE_FALSE(nowhere.enough);
    REQUIRE(nowhere.available == 0);
}
