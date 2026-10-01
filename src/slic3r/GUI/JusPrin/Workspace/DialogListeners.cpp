#include "DialogListeners.hpp"

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/StepMeshDialog.hpp"

#include <wx/html/htmlwin.h>
#include <wx/stattext.h>
#include <wx/thread.h>
#include <wx/toplevel.h>
#include <wx/window.h>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {
ScopedDialogListener* g_innermost = nullptr;

// A message box's words are in a wxHtmlWindow (Orca's MsgDialog) or in
// static text; the buttons are neither.
void collect_text(wxWindow& window, wxString& text)
{
    for (wxWindow* child : window.GetChildren()) {
        if (!child->IsShown())
            continue;
        wxString part;
        if (auto* html = dynamic_cast<wxHtmlWindow*>(child))
            part = html->ToText();
        else if (const auto* label = dynamic_cast<const wxStaticText*>(child))
            part = label->GetLabel();
        part.Trim(true).Trim(false);
        if (!part.empty())
            text += (text.empty() ? "" : "\n") + part;
        collect_text(*child, text);
    }
}

constexpr size_t kMessageTextLimit = 4000; // characters
} // namespace

DialogFacts read_dialog(wxWindow& dialog)
{
    DialogFacts facts;
    if (const auto* window = dynamic_cast<const wxTopLevelWindow*>(&dialog))
        facts.title = window->GetTitle().ToUTF8().data();
    wxString text;
    collect_text(dialog, text);
    if (text.length() > kMessageTextLimit)
        text = text.Left(kMessageTextLimit) + wxString::FromUTF8("…");
    facts.text        = text.ToUTF8().data();
    facts.ok          = wxWindow::FindWindowById(wxID_OK, &dialog) != nullptr;
    facts.yes         = wxWindow::FindWindowById(wxID_YES, &dialog) != nullptr;
    facts.no          = wxWindow::FindWindowById(wxID_NO, &dialog) != nullptr;
    facts.cancel      = wxWindow::FindWindowById(wxID_CANCEL, &dialog) != nullptr;
    facts.step_import = dynamic_cast<const StepMeshDialog*>(&dialog) != nullptr;
    return facts;
}

OrcaMessage least_change_answer(const DialogFacts& facts, int& answer)
{
    OrcaMessage message;
    message.title = facts.title;
    message.text  = facts.text;
    if (facts.ok) message.buttons.emplace_back("ok");
    if (facts.yes) message.buttons.emplace_back("yes");
    if (facts.no) message.buttons.emplace_back("no");
    if (facts.cancel) message.buttons.emplace_back("cancel");

    if (facts.step_import) {
        answer         = wxID_OK;
        message.answer = "ok";
    } else if (facts.no) {
        answer         = wxID_NO;
        message.answer = "no";
    } else if (facts.cancel) {
        answer         = wxID_CANCEL;
        message.answer = "cancel";
    } else if (facts.ok) {
        answer         = wxID_OK;
        message.answer = "ok";
    } else if (facts.yes) {
        answer         = wxID_YES;
        message.answer = "yes";
    } else {
        answer             = wxID_CANCEL;
        message.answer     = "cancel";
        message.recognized = false;
    }
    return message;
}

ScopedDialogListener::ScopedDialogListener(Listener listener) : m_listener(std::move(listener)), m_outer(g_innermost)
{
    wxASSERT(wxIsMainThread());
    g_innermost = this;
}

ScopedDialogListener::~ScopedDialogListener()
{
    wxASSERT(g_innermost == this);
    g_innermost = m_outer;
}

bool dispatch_dialog(wxWindow& dialog, int& answer)
{
    if (g_innermost == nullptr || !wxIsMainThread())
        return false;
    const DialogFacts facts = read_dialog(dialog);
    for (ScopedDialogListener* listener = g_innermost; listener != nullptr; listener = listener->m_outer)
        if (const std::optional<int> decided = listener->m_listener(dialog, facts)) {
            answer = *decided;
            return true;
        }
    return false;
}

ScopedLeastChangeAnswers::ScopedLeastChangeAnswers(bool prefer_cancel)
    : m_listener([this, prefer_cancel](wxWindow&, const DialogFacts& facts) -> std::optional<int> {
        int answer = wxID_CANCEL;
        OrcaMessage message = least_change_answer(facts, answer);
        if (prefer_cancel && facts.cancel && !facts.step_import) {
            answer = wxID_CANCEL;
            message.answer = "cancel";
        }
        m_messages.push_back(std::move(message));
        return answer;
    })
{}

} // namespace Slic3r::GUI::JusPrin::Workspace

namespace Slic3r::GUI {

bool answer_modal(wxWindow& dialog, int& answer)
{
    return JusPrin::Workspace::dispatch_dialog(dialog, answer);
}

} // namespace Slic3r::GUI
