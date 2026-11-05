#include "PopOutWindow.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <wx/sizer.h>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

// How long to wait before trying again to destroy a closed window whose
// release had to wait for a web view's script handler install.
constexpr int kReleaseRetryMs = 50;

} // namespace

PopOutWindow::PopOutWindow(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title)
    , m_release_timer(this)
{
    if (MainFrame* frame = wxGetApp().mainframe)
        SetIcons(frame->GetIcons());
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { close(); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { release(); }, m_release_timer.GetId());
}

void PopOutWindow::set_content(wxWindow* content)
{
    m_content   = content;
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(content, wxSizerFlags(1).Expand());
    SetSizer(sizer);
    SetSize(FromDIP(wxSize(1200, 800)));
    CentreOnScreen();
    // The title bar follows the app's appearance, as Orca's own frames do.
    wxGetApp().UpdateFrameDarkUI(this);
}

void PopOutWindow::bring_forward()
{
    Iconize(false);
    Raise();
}

void PopOutWindow::close()
{
    if (m_closing)
        return;
    m_closing = true;
    release();
}

// On macOS a new web view installs its script message handler from a
// CallAfter (WebView::CreateWebView), and wx waits for WebKit's reply to that
// install inside a nested event loop, with the app's
// is_adding_script_handler() flag set until it returns. A view destroyed
// during that wait is called back by WebKit after it is gone, and the app
// crashes. wx's own Destroy() of a frame only queues it for the next idle,
// and that idle can come inside the very install the view queued at creation,
// so the content is deleted here, synchronously, at a moment no install is
// running: deleting it also drops an install it has queued but not started.
// The frame, with no web view left in it, then goes the usual way.
void PopOutWindow::release()
{
    if (wxGetApp().is_adding_script_handler()) {
        Hide();
        m_release_timer.StartOnce(kReleaseRetryMs);
        return;
    }
    if (m_content != nullptr) {
        m_content->Destroy();
        m_content = nullptr;
    }
    Destroy();
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
