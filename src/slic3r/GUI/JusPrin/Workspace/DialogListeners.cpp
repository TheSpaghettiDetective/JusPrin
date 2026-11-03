#include "DialogListeners.hpp"

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/StepMeshDialog.hpp"

#include <wx/dialog.h>
#include <wx/html/htmlwin.h>
#include <wx/modalhook.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/thread.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <algorithm>

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI::JusPrin::Workspace {

bool dispatch_dialog(wxWindow& dialog, int& answer);

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

// Newest first. Never destroyed: a test's observer may unregister at exit,
// after this file's statics are gone.
std::vector<UnansweredDialogObserver*>& observers()
{
    static auto* const list = new std::vector<UnansweredDialogObserver*>;
    return *list;
}

// Every modal dialog, OrcaSlicer's or a native one, passes wxWidgets'
// ShowModal hook before it is drawn: the listeners answer it here, or it
// shows.
class ListenerHook : public wxModalDialogHook
{
    int Enter(wxDialog* dialog) override
    {
        if (dialog == nullptr || !wxIsMainThread())
            return wxID_NONE;
        if (int answer = wxID_NONE; dispatch_dialog(*dialog, answer)) {
            BOOST_LOG_TRIVIAL(info) << "JusPrin answered the dialog \"" << dialog->GetTitle().ToUTF8().data() << "\" with " << answer
                                    << (std::find(dialogStack.begin(), dialogStack.end(), dialog) == dialogStack.end() ? " (not an OrcaSlicer DPIAware dialog)" : "");
            return answer;
        }
        // A copy: an observer may unregister itself.
        for (UnansweredDialogObserver* observer : std::vector<UnansweredDialogObserver*>(observers()))
            if (const int answer = observer->Enter(dialog); answer != wxID_NONE)
                return answer;
        return wxID_NONE;
    }
};

// Registered once, on first use, and never destroyed: wxWidgets' hook list is
// a static that may be gone by the time a static hook would unregister.
void ensure_hook()
{
    static ListenerHook* const hook = [] {
        auto* created = new ListenerHook;
        created->Register();
        return created;
    }();
    (void) hook;
}
} // namespace

void UnansweredDialogObserver::Register()
{
    ensure_hook();
    auto& list = observers();
    if (std::find(list.begin(), list.end(), this) == list.end())
        list.insert(list.begin(), this);
}

void UnansweredDialogObserver::Unregister()
{
    auto& list = observers();
    list.erase(std::remove(list.begin(), list.end(), this), list.end());
}

DialogFacts read_dialog(wxWindow& dialog)
{
    DialogFacts facts;
    // A native message box draws its words and buttons outside wxWidgets:
    // they are read from what it was made with.
    if (const auto* box = dynamic_cast<const wxMessageDialogBase*>(&dialog)) {
        wxString text = box->GetMessage();
        if (!box->GetExtendedMessage().empty())
            text += "\n" + box->GetExtendedMessage();
        if (text.length() > kMessageTextLimit)
            text = text.Left(kMessageTextLimit) + wxString::FromUTF8("…");
        const long style = box->GetMessageDialogStyle();
        facts.title  = box->GetCaption().ToUTF8().data();
        facts.text   = text.ToUTF8().data();
        facts.yes    = (style & wxYES) != 0;
        facts.no     = (style & wxNO) != 0;
        facts.ok     = !facts.yes && (style & wxOK) != 0;
        facts.cancel = (style & wxCANCEL) != 0;
        return facts;
    }
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
    ensure_hook();
    g_innermost = this;
}

ScopedDialogListener::~ScopedDialogListener()
{
    // Normally the innermost. A listener held across work that ends out of
    // order (a person's open that outlives a tool call's scope) unlinks
    // where it is, so the chain never points at a destroyed listener.
    for (ScopedDialogListener** link = &g_innermost; *link != nullptr; link = &(*link)->m_outer)
        if (*link == this) {
            *link = m_outer;
            return;
        }
}

bool dispatch_dialog(wxWindow& dialog, int& answer)
{
    if (g_innermost == nullptr || !wxIsMainThread())
        return false;
    const DialogFacts facts = read_dialog(dialog);
    for (ScopedDialogListener* listener = g_innermost; listener != nullptr; listener = listener->m_outer)
        if (const std::optional<int> decided = listener->m_listener(dialog, facts)) {
            if (*decided == kShowToPerson)
                return false;
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
