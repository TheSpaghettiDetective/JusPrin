#include "DroppedFileUrl.hpp"

#include <wx/filename.h>
#include <wx/filesys.h>

namespace Slic3r::GUI::JusPrin {

std::optional<wxString> dropped_file(const wxString& url, const wxString& own_page)
{
    if (!url.Lower().StartsWith("file:"))
        return std::nullopt;
    // The page's own address may carry a query (?panel=printer); a file name
    // with a question mark in it arrives escaped.
    const wxFileName file = wxFileSystem::URLToFileName(url.BeforeFirst('?'));
    if (file.SameAs(wxFileName(own_page)))
        return std::nullopt;
    return file.GetFullPath();
}

} // namespace Slic3r::GUI::JusPrin
