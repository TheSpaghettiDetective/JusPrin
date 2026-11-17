#pragma once

#include <string>
#include <vector>

#include <wx/sharedptr.h>
#include <wx/string.h>

class wxWebViewHandler;

namespace Slic3r::GUI::JusPrin {

// Where the app's own pages load from: resources/jusprin/<page>/index.html,
// such as "home" or "agent".
//
// On macOS and Linux that is jusprin:///<page>/index.html, served from the
// resources/jusprin folder by the handler page_handlers() gives. WKWebView
// and WebKitGTK tell a file:// page nothing about an uncaught error but
// "Script error.", with no file, line or stack, so the page's Sentry gets
// nothing to report; a page on its own scheme gets all of it. On Windows the
// pages load from file://: WebView2 tells a file:// page the whole error.
//
// Each web view registers the handler with its own web context before it
// loads: WKWebView keeps schemes per view, and wxWidgets gives every WebKitGTK
// view a context of its own. The views share one handler, which lives as long
// as the app (PageScheme.cpp says why).

// The page's address, without a query; empty when this build has no such page.
wxString page_url(const std::string& page);

// The scheme handlers a web view needs to load a page_url(), for
// WebView::CreateWebView. Empty where the pages load from file://.
std::vector<wxSharedPtr<wxWebViewHandler>> page_handlers();

} // namespace Slic3r::GUI::JusPrin
