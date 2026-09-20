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

} // namespace Slic3r::GUI::JusPrin::Agent
