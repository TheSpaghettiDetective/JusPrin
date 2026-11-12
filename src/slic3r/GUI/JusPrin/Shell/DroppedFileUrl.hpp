#pragma once

#include <optional>

#include <wx/string.h>

namespace Slic3r::GUI::JusPrin {

// A file let go on a web view opens in place of its page: on macOS the app
// makes every web view do that, and the page never sees the drag
// (WKWebView (DragDrop) in MacDarkMode.mm); WebView2 and WebKitGTK do it when
// the page leaves a drop alone. A view that must stay on its page cancels
// that navigation and handles the file itself, as OrcaSlicer's home page
// does (WebViewPanel::OnNavigationRequest).
//
// The local file a navigation to `url` would open, or nothing when the URL is
// not a local file or is `own_page` (a file path) itself.
std::optional<wxString> dropped_file(const wxString& url, const wxString& own_page);

} // namespace Slic3r::GUI::JusPrin
