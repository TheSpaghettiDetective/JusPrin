#include "OwnDialogs.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include <boost/log/trivial.hpp>

#include <wx/debug.h>
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/weakref.h>
#include <wx/window.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::OrcaMessages {

namespace {

// The own windows being shown, innermost last. Never destroyed: a window may
// be shown while statics are torn down at exit.
std::vector<const wxDialog*>& own_dialogs()
{
    static auto* const list = new std::vector<const wxDialog*>;
    return *list;
}

} // namespace

int show_own_modal(wxDialog& dialog)
{
    wxASSERT(wxIsMainThread());
    auto& list = own_dialogs();
    list.push_back(&dialog);
    struct Unmark
    {
        const wxDialog* dialog;
        ~Unmark()
        {
            auto& list = own_dialogs();
            list.erase(std::remove(list.begin(), list.end(), dialog), list.end());
        }
    } unmark{&dialog};
    return dialog.ShowModal();
}

int show_own_message(wxWindow* parent, const wxString& text, const wxString& title, long style)
{
    // Most say what failed; the log keeps every one.
    BOOST_LOG_TRIVIAL(warning) << "JusPrin shows the message \"" << title.ToUTF8().data() << "\": " << text.ToUTF8().data();
    MessageDialog dialog(parent, text, title, style);
    return show_own_modal(dialog);
}

void show_own_error(wxWindow* parent, const wxString& text, const std::string& said)
{
    BOOST_LOG_TRIVIAL(warning) << "JusPrin shows the error: " << text.ToUTF8().data() << (said.empty() ? std::string() : " (" + said + ")");
    wxGetApp().CallAfter([parent = wxWeakRef<wxWindow>(parent), text] {
        ErrorDialog dialog(parent.get(), text, false);
        show_own_modal(dialog);
    });
}

bool is_own_dialog(const wxDialog& dialog)
{
    const auto& list = own_dialogs();
    return std::find(list.begin(), list.end(), &dialog) != list.end();
}

} // namespace Slic3r::GUI::JusPrin::OrcaMessages
