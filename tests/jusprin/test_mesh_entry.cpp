#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Project/MeshEntry.hpp"
#include "slic3r/GUI/JusPrin/Project/ModelReferences.hpp"

using namespace Slic3r::GUI::JusPrin::Project;

namespace {

// Two parts, the way the exporter writes a split archive's per-object file.
std::string entry(unsigned first, unsigned second)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<model unit=\"millimeter\" xml:lang=\"en-US\">\n"
           " <resources>\n"
           "  <object id=\"" + std::to_string(first) + "\" p:UUID=\"00010000-81cb-4c03-9d28-80fed5dfa1dc\" type=\"model\">\n"
           "   <mesh>\n"
           "    <vertices>\n"
           "     <vertex x=\"0\" y=\"0\" z=\"0\"/>\n"
           "     <vertex x=\"1\" y=\"0\" z=\"0\"/>\n"
           "     <vertex x=\"0\" y=\"1\" z=\"0\"/>\n"
           "    </vertices>\n"
           "    <triangles>\n"
           "     <triangle v1=\"0\" v2=\"1\" v3=\"2\" paint_supports=\"4\"/>\n"
           "    </triangles>\n"
           "   </mesh>\n"
           "  </object>\n"
           "  <object id='" + std::to_string(second) + "' p:UUID=\"00020000-81cb-4c03-9d28-80fed5dfa1dc\" type=\"model\">\n"
           "   <mesh><vertices/><triangles/></mesh>\n"
           "  </object>\n"
           " </resources>\n"
           " <build/>\n"
           "</model>\n";
}

} // namespace

TEST_CASE("Mesh entry ids are read in order of appearance", "[MeshEntry]")
{
    REQUIRE(mesh_entry_ids(entry(65537, 131073)) == std::vector<std::uint64_t>{65537, 131073});
    REQUIRE(mesh_entry_ids(entry(5, 6)) == std::vector<std::uint64_t>{5, 6});
    REQUIRE(mesh_entry_ids("<model/>").empty());
}

TEST_CASE("Mesh entry ids ignore attributes that merely end in id", "[MeshEntry]")
{
    const std::string text = "<object puuid=\"7\" objectid=\"8\" id=\"9\"><component objectid=\"3\"/></object>";
    REQUIRE(mesh_entry_ids(text) == std::vector<std::uint64_t>{9});
    REQUIRE(mesh_entry_ids("<objects id=\"1\"/><object\tid = \"2\"/>") == std::vector<std::uint64_t>{2});
}

TEST_CASE("Mesh entry key is the same across id numberings and differs with the mesh", "[MeshEntry]")
{
    const std::string backup = entry(65537, 131073);
    const std::string full   = entry(5, 6);
    REQUIRE(mesh_entry_key(backup) == mesh_entry_key(full));
    REQUIRE(mesh_entry_key(backup).size() == 64);

    std::string moved = full;
    moved.replace(moved.find("x=\"1\""), 5, "x=\"2\"");
    REQUIRE(mesh_entry_key(moved) != mesh_entry_key(full));

    std::string painted = full;
    painted.replace(painted.find("paint_supports=\"4\""), 18, "paint_supports=\"8\"");
    REQUIRE(mesh_entry_key(painted) != mesh_entry_key(full));
}

TEST_CASE("Rewriting ids reproduces the other numbering exactly", "[MeshEntry]")
{
    REQUIRE(rewrite_mesh_entry_ids(entry(5, 6), {65537, 131073}) == entry(65537, 131073));
    REQUIRE(rewrite_mesh_entry_ids(entry(65537, 131073), {5, 6}) == entry(5, 6));
    REQUIRE_THROWS_AS(rewrite_mesh_entry_ids(entry(5, 6), {1}), std::invalid_argument);
    REQUIRE_THROWS_AS(rewrite_mesh_entry_ids(entry(5, 6), {1, 2, 3}), std::invalid_argument);
}

TEST_CASE("Main model references group ids by entry in order of first appearance", "[MeshEntry]")
{
    const std::string main_model =
        "<model>\n <resources>\n"
        "  <object id=\"3\" type=\"model\">\n   <components>\n"
        "    <component p:path=\"/3D/Objects/Bracket &amp; base_1.model\" objectid=\"65537\" transform=\"1 0 0 0 1 0 0 0 1 0 0 0\"/>\n"
        "    <component p:path=\"/3D/Objects/Bracket &amp; base_1.model\" objectid=\"131073\"/>\n"
        "   </components>\n  </object>\n"
        "  <object id=\"6\" type=\"model\">\n   <components>\n"
        "    <component objectid=\"65537\" p:path=\"/3D/Objects/Bracket &amp; base_1.model\"/>\n" // shared part
        "    <component p:path=\"/3D/Objects/Lid_2.model\" objectid=\"65538\"/>\n"
        "   </components>\n  </object>\n"
        " </resources>\n</model>\n";
    const std::vector<MeshReference> references = mesh_references(main_model);
    REQUIRE(references.size() == 2);
    REQUIRE(references[0].path == "3D/Objects/Bracket & base_1.model");
    REQUIRE(references[0].ids == std::vector<std::uint64_t>{65537, 131073});
    REQUIRE(references[1].path == "3D/Objects/Lid_2.model");
    REQUIRE(references[1].ids == std::vector<std::uint64_t>{65538});
}

TEST_CASE("Main model without split meshes has no references", "[MeshEntry]")
{
    REQUIRE(mesh_references("<model><resources><object id=\"1\"><mesh/></object></resources></model>").empty());
    REQUIRE(mesh_references("<component objectid=\"1\"/>").empty());
}
