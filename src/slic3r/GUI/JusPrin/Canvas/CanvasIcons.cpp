#include "CanvasIcons.hpp"

#include "slic3r/GUI/GLTexture.hpp"
#include "libslic3r/Utils.hpp"

#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

void load_icon(GLTexture& texture, const std::string& name, int size_px)
{
    const boost::filesystem::path path = boost::filesystem::path(resources_dir()) / "jusprin" / "ui" / "icons" / (name + ".svg");
    boost::nowide::ifstream file(path.string());
    if (!file.is_open())
        throw std::runtime_error("canvas icon is missing: " + path.string());
    std::string svg((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // The icons are drawn in currentColor for the web pages; nanosvg reads that as grey.
    boost::replace_all(svg, "currentColor", "#FFFFFF");

    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image(nsvgParse(svg.data(), "px", 96.0f), nsvgDelete);
    if (image == nullptr || image->width <= 0.f)
        throw std::runtime_error("canvas icon could not be parsed: " + path.string());
    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> rasterizer(nsvgCreateRasterizer(), nsvgDeleteRasterizer);
    std::vector<unsigned char> pixels(size_t(size_px) * size_px * 4, 0);
    nsvgRasterize(rasterizer.get(), image.get(), 0, 0, float(size_px) / image->width, pixels.data(), size_px, size_px, size_px * 4);

    texture.reset();
    if (!texture.load_from_raw_data(std::move(pixels), size_px, size_px))
        throw std::runtime_error("canvas icon could not be uploaded: " + path.string());
}

} // namespace

unsigned int canvas_icon_texture(const std::string& name, int size_px)
{
    // Keyed by size as well as name: a display-scale change asks for a
    // different size and gets its own texture rather than a scaled one.
    static std::map<std::pair<std::string, int>, GLTexture> cache;
    GLTexture& texture = cache[{name, size_px}];
    if (texture.get_id() == 0)
        load_icon(texture, name, size_px);
    return texture.get_id();
}

} // namespace Slic3r::GUI::JusPrin
