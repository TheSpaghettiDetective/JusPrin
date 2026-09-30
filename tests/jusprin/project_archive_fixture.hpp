#pragma once

// Synthetic project archives for the checkpoint store and archive checks:
// the shape of OrcaSlicer's split 3MF (a main model whose components refer
// to one 3D/Objects/*.model file per object) without OrcaSlicer.

#include "slic3r/GUI/JusPrin/Project/ZipFile.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace project_fixture {

struct Part
{
    std::uint64_t id;
    std::string   vertex; // one vertex line's x value: the mesh's identity
    std::string   paint{"0"};
};

struct Object
{
    std::string       name; // becomes 3D/Objects/<name>.model
    std::vector<Part> parts;
};

inline std::string mesh_entry(const Object& object)
{
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"millimeter\">\n <resources>\n";
    for (const Part& part : object.parts)
        text += "  <object id=\"" + std::to_string(part.id) + "\" type=\"model\">\n   <mesh>\n    <vertices>\n     <vertex x=\"" +
                part.vertex + "\" y=\"0\" z=\"0\"/>\n     <vertex x=\"1\" y=\"0\" z=\"0\"/>\n     <vertex x=\"0\" y=\"1\" z=\"0\"/>\n"
                "    </vertices>\n    <triangles>\n     <triangle v1=\"0\" v2=\"1\" v3=\"2\" paint_supports=\"" +
                part.paint + "\"/>\n    </triangles>\n   </mesh>\n  </object>\n";
    return text + " </resources>\n <build/>\n</model>\n";
}

inline std::string main_model(const std::vector<Object>& objects)
{
    std::string   text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"millimeter\">\n <resources>\n";
    std::uint64_t next = 1000;
    for (const Object& object : objects) {
        text += "  <object id=\"" + std::to_string(next++) + "\" type=\"model\">\n   <components>\n";
        for (const Part& part : object.parts)
            text += "    <component p:path=\"/3D/Objects/" + object.name + ".model\" objectid=\"" + std::to_string(part.id) +
                    "\" transform=\"1 0 0 0 1 0 0 0 1 0 0 0\"/>\n";
        text += "   </components>\n  </object>\n";
    }
    text += " </resources>\n <build>\n";
    for (std::size_t i = 0; i < objects.size(); ++i)
        text += "  <item objectid=\"" + std::to_string(1000 + i) + "\"/>\n";
    return text + " </build>\n</model>\n";
}

// A split archive: with_meshes writes the per-object entries too (a full
// save); without, only the main model and settings (OrcaSlicer's backup).
inline bool write_archive(const std::filesystem::path& path, const std::vector<Object>& objects, bool with_meshes,
                          const std::string& settings = "layer_height = 0.2\n")
{
    Slic3r::GUI::JusPrin::Project::ZipWriter writer;
    if (!writer.open(path))
        return false;
    bool ok = writer.add("[Content_Types].xml", "<Types/>") && writer.add("_rels/.rels", "<Relationships/>") &&
              writer.add("3D/3dmodel.model", main_model(objects)) && writer.add("Metadata/project_settings.config", settings);
    if (with_meshes)
        for (const Object& object : objects)
            ok = ok && writer.add("3D/Objects/" + object.name + ".model", mesh_entry(object));
    return ok && writer.finish();
}

inline std::filesystem::path fresh_dir(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("jusprin-test-" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace project_fixture
