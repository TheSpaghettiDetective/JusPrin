#include "ModalAnswers.hpp"

#include <wx/thread.h>
#include <wx/toplevel.h>
#include <wx/window.h>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {
ScopedModalAnswers* g_innermost = nullptr;
}

ScopedModalAnswers::ScopedModalAnswers(Decide decide) : m_decide(std::move(decide)), m_outer(g_innermost)
{
    wxASSERT(wxIsMainThread());
    g_innermost = this;
}

ScopedModalAnswers::~ScopedModalAnswers()
{
    wxASSERT(g_innermost == this);
    g_innermost = m_outer;
}

bool answer_through_scope(wxWindow& dialog, int& answer)
{
    if (g_innermost == nullptr || !wxIsMainThread())
        return false;
    const wxTopLevelWindow* window = dynamic_cast<wxTopLevelWindow*>(&dialog);
    const wxString          title  = window != nullptr ? window->GetTitle() : wxString();
    answer = g_innermost->m_decide(dialog, title);
    g_innermost->m_records.push_back({title.ToUTF8().data(), answer});
    return true;
}

} // namespace Slic3r::GUI::JusPrin::Workspace

namespace Slic3r::GUI {

bool answer_modal(wxWindow& dialog, int& answer)
{
    return JusPrin::Workspace::answer_through_scope(dialog, answer);
}

} // namespace Slic3r::GUI
