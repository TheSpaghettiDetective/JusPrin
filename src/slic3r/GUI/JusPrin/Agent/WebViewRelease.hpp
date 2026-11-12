#pragma once

// The one safe moment to destroy a web view on macOS.
//
// A new web view installs its script message handler from a CallAfter
// (WebView::CreateWebView), and wx waits for WebKit's reply to that install
// inside a nested event loop, with the app's is_adding_script_handler() flag
// set until it returns. A view destroyed during that wait is called back by
// WebKit after it is gone, and the app crashes; a window closed as soon as it
// opens meets exactly that. wx's own Destroy() of a top-level window only
// queues it for the next idle, and that idle can come inside the very install
// the view queued at creation, so the owner deletes its views itself, from
// the step given here, at a moment no install is running. Deleting a view
// also drops an install it has queued but not started.
//
// Shell/ShellController's detach_shell() waits the same way with a timer of
// its own, since its step destroys whoever would own this.

#include <wx/timer.h>

#include <functional>

namespace Slic3r::GUI::JusPrin {

class WebViewRelease : private wxTimer
{
public:
    explicit WebViewRelease(std::function<void()> release);

    // Runs the release step now and returns true, or, while an install is
    // running, returns false and runs it once no install is. The caller
    // hides what it is releasing in the meantime.
    bool request();
    // The release is no longer wanted: a window brought back while it waited.
    void cancel() { Stop(); }

private:
    void Notify() override { request(); }

    std::function<void()> m_release;
};

} // namespace Slic3r::GUI::JusPrin
