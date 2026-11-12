#include "PopOutWindow.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <wx/sizer.h>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

PopOutWindow::PopOutWindow(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title)
    , m_release([this] { release(); })
{
    if (MainFrame* frame = wxGetApp().mainframe)
        SetIcons(frame->GetIcons());
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { close(); });
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
    if (!m_release.request())
        Hide();
}

// The content, and the web view in it, goes first and synchronously (see
// WebViewRelease); the frame, with no web view left in it, then goes the
// usual way.
void PopOutWindow::release()
{
    if (m_content != nullptr) {
        m_content->Destroy();
        m_content = nullptr;
    }
    Destroy();
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
