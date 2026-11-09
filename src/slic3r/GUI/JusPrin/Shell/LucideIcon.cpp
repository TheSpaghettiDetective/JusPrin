#include "LucideIcon.hpp"

#include "libslic3r/Utils.hpp"
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <cstddef>
#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/image.h>
#include <wx/string.h>

#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace Slic3r::GUI::JusPrin {

const wxBitmap& lucide_bitmap(const char* name, int size_px, const wxColour& color)
{
    using Key = std::tuple<std::string, int, unsigned long>;
    static std::map<Key, wxBitmap> cache;
    const Key key{name, size_px, color.GetRGBA()};
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;

    const auto path = boost::filesystem::path(resources_dir()) / "jusprin" / "ui" / "icons" / (std::string(name) + ".svg");
    boost::nowide::ifstream file(path.string());
    if (!file.is_open()) throw std::runtime_error("Lucide icon is missing: " + path.string());
    std::string svg((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string hex = wxString::Format("#%02X%02X%02X", color.Red(), color.Green(), color.Blue()).ToStdString();
    boost::replace_all(svg, "currentColor", hex);
    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> parsed(nsvgParse(svg.data(), "px", 96.f), nsvgDelete);
    if (!parsed || parsed->width <= 0.f || parsed->height <= 0.f)
        throw std::runtime_error("Lucide icon could not be parsed: " + path.string());
    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> rasterizer(nsvgCreateRasterizer(), nsvgDeleteRasterizer);
    if (!rasterizer) throw std::runtime_error("Lucide icon rasterizer could not be created");
    std::vector<unsigned char> pixels(size_t(size_px) * size_px * 4, 0);
    nsvgRasterize(rasterizer.get(), parsed.get(), 0, 0, float(size_px) / parsed->width,
                  pixels.data(), size_px, size_px, size_px * 4);

    wxImage image(size_px, size_px);
    image.InitAlpha();
    unsigned char* rgb = image.GetData();
    unsigned char* alpha = image.GetAlpha();
    for (size_t i = 0; i < size_t(size_px) * size_px; ++i) {
        rgb[i * 3 + 0] = pixels[i * 4 + 0];
        rgb[i * 3 + 1] = pixels[i * 4 + 1];
        rgb[i * 3 + 2] = pixels[i * 4 + 2];
        alpha[i] = pixels[i * 4 + 3];
    }
    return cache.emplace(key, wxBitmap(image)).first->second;
}

} // namespace Slic3r::GUI::JusPrin
