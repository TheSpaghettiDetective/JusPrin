#include "PageScheme.hpp"

#include "slic3r/GUI/JusPrin/Support/PageFiles.hpp"

#include "libslic3r/Utils.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/filesys.h>
#include <wx/sharedptr.h>
#include <wx/string.h>
#include <wx/uri.h>
#include <wx/webview.h>
#include <wx/wfstream.h>

namespace Slic3r::GUI::JusPrin {

namespace {

std::filesystem::path pages_folder() { return std::filesystem::u8path(resources_dir()) / "jusprin"; }

#ifndef _WIN32
constexpr const char* kScheme = "jusprin";

// Serves jusprin:///<path> from the pages folder, and nothing outside it.
class PageFilesHandler : public wxWebViewHandler
{
public:
    PageFilesHandler() : wxWebViewHandler(kScheme) {}

    wxFSFile* GetFile(const wxString& uri) override
    {
        const wxString path = wxURI::Unescape(wxURI(uri).GetPath());
        const std::optional<std::filesystem::path> file = page_file(pages_folder(), std::string(path.ToUTF8()));
        if (!file)
            return nullptr;
        auto stream = std::make_unique<wxFFileInputStream>(wxString::FromUTF8(file->u8string()));
        if (!stream->IsOk())
            return nullptr;
        // The web view takes the file and frees it. An empty type is worked
        // out from the address's extension.
        return new wxFSFile(stream.release(), wxFileSystem::FileNameToURL(wxFileName(wxString::FromUTF8(file->u8string()))),
                            wxString::FromUTF8(page_file_type(*file)), wxEmptyString, wxDateTime());
    }
};
#endif

} // namespace

wxString page_url(const std::string& page)
{
    const std::filesystem::path file = pages_folder() / page / "index.html";
    std::error_code             error;
    if (!std::filesystem::is_regular_file(file, error))
        return {};
#ifndef _WIN32
    return wxString::Format("%s:///%s/index.html", kScheme, wxString::FromUTF8(page));
#else
    return wxFileSystem::FileNameToURL(wxFileName(wxString::FromUTF8(file.u8string())));
#endif
}

std::vector<wxSharedPtr<wxWebViewHandler>> page_handlers()
{
#ifndef _WIN32
    // One handler for every view, never freed: on macOS wxWidgets gives
    // WKWebView only a plain pointer to it (WebViewCustomProtocol), and
    // WebKit can still ask it for a file after the view that registered it
    // is gone. It holds nothing of its own, so the views can share it.
    static const auto* const handler = new wxSharedPtr<wxWebViewHandler>(new PageFilesHandler());
    return {*handler};
#else
    return {};
#endif
}

} // namespace Slic3r::GUI::JusPrin
