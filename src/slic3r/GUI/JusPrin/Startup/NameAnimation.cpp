#include "NameAnimation.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/path.hpp>

#include <wx/imagwebp.h>
#include <wx/string.h>
#include <wx/wfstream.h>

#include <stdexcept>
#include <vector>

namespace Slic3r::GUI::JusPrin {

std::vector<wxWebPAnimationFrame> read_name_animation()
{
    const auto path = boost::filesystem::path(resources_dir()) / "jusprin" / "startup" /
                      (GUI_App::dark_mode() ? "splash-name-dark.webp" : "splash-name-light.webp");
    std::vector<wxWebPAnimationFrame> frames;
    wxFFileInputStream                stream(wxString::FromUTF8(path.string()));
    if (!stream.IsOk() || !wxWEBPHandler().LoadAnimation(frames, stream, false) || frames.empty())
        throw std::runtime_error("the name animation could not be read: " + path.string());
    return frames;
}

} // namespace Slic3r::GUI::JusPrin
