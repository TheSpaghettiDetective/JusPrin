#pragma once

// A printer's window of its own, beside the app rather than inside it: the
// person can move it, minimise it and keep it in view while working in a
// project. It has no parent, so it has its own taskbar entry; whoever opens
// one closes it when the app's main window goes.
//
// The content is one of Orca's own views, and every such view holds a web
// view, so the window owns the one safe way to destroy it (WebViewRelease).

#include "slic3r/GUI/JusPrin/Agent/WebViewRelease.hpp"

#include <wx/frame.h>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class PopOutWindow : public wxFrame
{
public:
    // Destroys the window, or hides it and destroys it once no web view is
    // installing its script handler (WebViewRelease). The title bar's close
    // comes here too.
    void close();
    bool closing() const { return m_closing; }

    // Back in front of the person, out of the Dock if it was minimised.
    void bring_forward();

protected:
    explicit PopOutWindow(const wxString& title);

    // Fills the window with `content`, which must be a child of this window,
    // and sizes and centres it. Called once, before Show().
    void set_content(wxWindow* content);

private:
    void release();

    wxWindow*      m_content{nullptr};
    WebViewRelease m_release;
    bool           m_closing{false};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
