#include "ImageThumbnail.hpp"

#include "slic3r/GUI/JusPrin/Support/Base64.hpp"

#include <wx/image.h>
#include <wx/mstream.h>

#include <algorithm>

namespace Slic3r::GUI::JusPrin::Agent {

std::string image_thumbnail_data_url(const std::string& bytes, int long_edge)
{
    wxMemoryInputStream input(bytes.data(), bytes.size());
    wxImage             image;
    if (!image.LoadFile(input, wxBITMAP_TYPE_ANY) || !image.IsOk())
        return {};
    const double scale = std::min(1.0, double(long_edge) / std::max(image.GetWidth(), image.GetHeight()));
    if (scale < 1.0)
        image.Rescale(std::max(1, int(image.GetWidth() * scale)), std::max(1, int(image.GetHeight() * scale)),
                      wxIMAGE_QUALITY_HIGH);
    image.SetOption(wxIMAGE_OPTION_QUALITY, 85);
    wxMemoryOutputStream output;
    if (!image.SaveFile(output, wxBITMAP_TYPE_JPEG))
        return {};
    std::string encoded(output.GetSize(), '\0');
    output.CopyTo(encoded.data(), encoded.size());
    return encoded.empty() ? std::string() : "data:image/jpeg;base64," + base64_encode(encoded);
}

std::optional<EncodedImage> image_for_model(const std::string& bytes, int long_edge, std::size_t max_bytes)
{
    wxMemoryInputStream input(bytes.data(), bytes.size());
    wxImage             image;
    if (!image.LoadFile(input, wxBITMAP_TYPE_ANY) || !image.IsOk())
        return std::nullopt;
    const int longest = std::max(image.GetWidth(), image.GetHeight());
    if (longest <= long_edge && bytes.size() <= max_bytes)
        return std::nullopt;
    if (longest > long_edge) {
        const double scale = double(long_edge) / longest;
        image.Rescale(std::max(1, int(image.GetWidth() * scale)), std::max(1, int(image.GetHeight() * scale)),
                      wxIMAGE_QUALITY_HIGH);
    }
    const bool transparent = image.HasAlpha() || image.HasMask();
    if (!transparent)
        image.SetOption(wxIMAGE_OPTION_QUALITY, 85);
    wxMemoryOutputStream output;
    if (!image.SaveFile(output, transparent ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_JPEG))
        return std::nullopt;
    EncodedImage encoded{transparent ? "image/png" : "image/jpeg", std::string(output.GetSize(), '\0')};
    output.CopyTo(encoded.bytes.data(), encoded.bytes.size());
    // A re-encoding that came out no smaller is no help.
    if (encoded.bytes.empty() || encoded.bytes.size() >= bytes.size())
        return std::nullopt;
    return encoded;
}

} // namespace Slic3r::GUI::JusPrin::Agent
