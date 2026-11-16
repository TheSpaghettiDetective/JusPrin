#include "PageLanguage.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

wxString with_page_language(const wxString& url)
{
    const wxString code = wxGetApp().current_language_code();
    if (code.empty())
        return url;
    return url + (url.Contains("?") ? "&" : "?") + "lang=" + code;
}

std::string server_language()
{
    std::string tag = into_u8(wxGetApp().current_language_code());
    std::replace(tag.begin(), tag.end(), '_', '-');
    return tag;
}

} // namespace Slic3r::GUI::JusPrin
