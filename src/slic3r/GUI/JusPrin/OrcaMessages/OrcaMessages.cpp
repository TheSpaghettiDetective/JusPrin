#include "OrcaMessages.hpp"

#include "OwnDialogs.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/JusPrin/Workspace/DialogListeners.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/UnsavedChangesDialog.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/format.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <boost/log/trivial.hpp>

#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::OrcaMessages {

namespace {

bool         g_installed = false;
bool         g_taking_for_tests = true;
View         g_view;
// SavedFirst scopes alive: a version holding the changes is already saved.
int          g_saved_first{0};
ScopedCause* g_cause_scope = nullptr;
Cause        g_noted_cause;
unsigned     g_noted_token = 0;

// Never destroyed: a box may open while statics are torn down at exit.
std::vector<TakenMessage>& taken_store()
{
    static auto* const list = new std::vector<TakenMessage>;
    return *list;
}
std::vector<ShownWindow>& shown_store()
{
    static auto* const list = new std::vector<ShownWindow>;
    return *list;
}

void take(TakenMessage::From from, std::string title, std::string text)
{
    TakenMessage message{from, current_cause(), std::move(title), std::move(text)};
    BOOST_LOG_TRIVIAL(info) << "JusPrin took OrcaSlicer's message (" << from_name(from) << ", cause " << cause_name(message.cause.kind)
                            << ", printer \"" << message.cause.printer << "\"): " << message.text;
    taken_store().push_back(message);
    if (g_view.show)
        g_view.show(message);
}

void let_through(wxDialog& dialog, WindowKind kind, const char* why)
{
    const char* type = typeid(dialog).name();
    shown_store().push_back({type, std::string(dialog.GetTitle().ToUTF8().data()), kind, why});
    BOOST_LOG_TRIVIAL(info) << "JusPrin let OrcaSlicer's window \"" << dialog.GetTitle().ToUTF8().data() << "\" (" << type << ") through: " << why;
}

int count_buttons(wxWindow& window)
{
    int count = 0;
    for (wxWindow* child : window.GetChildren()) {
        if (!child->IsShown())
            continue;
        if (dynamic_cast<::Button*>(child) != nullptr || dynamic_cast<wxButton*>(child) != nullptr)
            ++count;
        count += count_buttons(*child);
    }
    return count;
}

// Whether the person has another window open over the main one: OrcaSlicer's
// settings window, the send dialog, the monitor, Preferences. A box that
// opens then belongs with that window, where they are looking.
bool another_window_is_open(const wxWindow* parent, const wxWindow* box)
{
    const wxWindow* frame = wxGetApp().mainframe;
    if (parent != nullptr)
        if (const wxWindow* top = wxGetTopLevelParent(const_cast<wxWindow*>(parent)); top != nullptr && top != frame && top != box)
            return true;
    for (const wxWindow* window : wxTopLevelWindows) {
        if (window == frame || window == box || !window->IsShown())
            continue;
        if (dynamic_cast<const wxDialog*>(window) != nullptr || dynamic_cast<const wxFrame*>(window) != nullptr)
            return true;
    }
    return false;
}

// The two uses of UnsavedChangesDialog that ask what to do with setting
// changes not yet saved in a profile: a switch of profile ("Transfer or
// discard changes", Tab::may_discard_current_dirty_preset), and the question
// before OrcaSlicer reloads profiles ("<caption>: unsaved changes",
// GUI_App::check_and_save_current_preset_changes and
// check_and_keep_current_preset_changes). The class also asks whether to copy
// one nozzle's settings to the other (Tab::sync_excluder), which is a
// question about a copy and stays the person's. OrcaSlicer names the uses
// only in their titles, so they are told apart by those, looked up as
// OrcaSlicer looks them up.
enum class UnsavedUse { Switch, BeforeReload, Other };
UnsavedUse unsaved_use(const wxDialog& dialog)
{
    const wxString title = dialog.GetTitle();
    if (title == _L("Transfer or discard changes"))
        return UnsavedUse::Switch;
    if (title.EndsWith(": " + _L("unsaved changes")))
        return UnsavedUse::BeforeReload;
    return UnsavedUse::Other;
}

// OrcaSlicer's own Discard, for every profile with unsaved changes.
void discard_preset_edits()
{
    for (Tab* tab : wxGetApp().tabs_list)
        if (tab->get_presets() != nullptr && tab->get_presets()->current_is_dirty())
            tab->get_presets()->discard_current_changes();
    wxGetApp().load_current_presets(false);
}

// Without a saved version the dialog is cancelled, which leaves the profile
// and its changes as they were; the view has reported why.
int answer_unsaved_changes(wxDialog& dialog, UnsavedUse use)
{
    if (g_saved_first == 0 && !g_view.save_before_dropping_changes())
        return wxID_CANCEL;
    // A switch goes on to drop the outgoing profile's changes itself; the
    // other callers read the dialog's choice, which is none, so the changes
    // are dropped here.
    if (use == UnsavedUse::BeforeReload)
        discard_preset_edits();
    take(TakenMessage::From::UnsavedChanges, std::string(dialog.GetTitle().ToUTF8().data()),
         "A version was saved and the setting changes were dropped.");
    return wxID_OK;
}

} // namespace

const char* cause_name(Cause::Kind kind)
{
    switch (kind) {
    case Cause::Kind::None: return "none";
    case Cause::Kind::PrinterConnection: return "printer_connection";
    case Cause::Kind::PrinterMessage: return "printer_message";
    case Cause::Kind::PluginMessage: return "plugin_message";
    case Cause::Kind::Slice: return "slice";
    case Cause::Kind::HostUpload: return "host_upload";
    }
    return "";
}

const char* from_name(TakenMessage::From from)
{
    switch (from) {
    case TakenMessage::From::Box: return "box";
    case TakenMessage::From::ErrorBox: return "error_box";
    case TakenMessage::From::AppDialog: return "app_dialog";
    case TakenMessage::From::PrinterNotification: return "printer_notification";
    case TakenMessage::From::UnsavedChanges: return "unsaved_changes";
    case TakenMessage::From::Tip: return "tip";
    case TakenMessage::From::PageSwitch: return "page_switch";
    }
    return "";
}

const char* window_kind_name(WindowKind kind)
{
    switch (kind) {
    case WindowKind::Own: return "own";
    case WindowKind::Picker: return "picker";
    case WindowKind::Chooser: return "chooser";
    case WindowKind::UnsavedChanges: return "unsaved_changes";
    case WindowKind::CopyQuestion: return "copy_question";
    case WindowKind::Tip: return "tip";
    case WindowKind::Window: return "window";
    case WindowKind::Question: return "question";
    case WindowKind::Message: return "message";
    case WindowKind::MessageOverWindow: return "message_over_window";
    }
    return "";
}

ScopedCause::ScopedCause(Cause::Kind kind, std::string printer, std::string detail)
    : m_cause{kind, std::move(printer), std::move(detail)}, m_outer(g_cause_scope)
{
    g_cause_scope = this;
}

ScopedCause::~ScopedCause() { g_cause_scope = m_outer; }

void note_cause(Cause cause)
{
    g_noted_cause      = std::move(cause);
    const unsigned token = ++g_noted_token;
    // Queued behind the event now being handled, so it runs once that has
    // ended.
    if (wxTheApp != nullptr)
        wxTheApp->CallAfter([token] {
            if (g_noted_token == token)
                g_noted_cause = {};
        });
}

Cause current_cause() { return g_cause_scope != nullptr ? g_cause_scope->m_cause : g_noted_cause; }

void install(View view)
{
    g_view      = std::move(view);
    g_installed = true;
}

void uninstall()
{
    g_installed = false;
    g_view      = {};
}

bool taking() { return g_installed && g_taking_for_tests; }

void set_taking_for_tests(bool on) { g_taking_for_tests = on; }

SavedFirst::SavedFirst()
{
    if (!taking())
        return;
    m_saved   = g_view.save_before_dropping_changes();
    m_holding = m_saved;
    if (m_holding)
        ++g_saved_first;
}

SavedFirst::~SavedFirst()
{
    if (m_holding)
        --g_saved_first;
}

ScopedCause plugin_message_cause(const std::string& device_id, const std::string& code)
{
    return ScopedCause(Cause::Kind::PluginMessage, device_id, code);
}

bool take_app_message(const wxString& text)
{
    if (!taking() || !wxIsMainThread())
        return false;
    if (current_cause().kind == Cause::Kind::None && another_window_is_open(nullptr, nullptr))
        return false;
    // The title of the InfoDialog GUI_App opens for it.
    take(TakenMessage::From::AppDialog, _u8L("Info"), std::string(text.ToUTF8().data()));
    return true;
}

bool take_printer_message(const MachineObject* printer, const wxString& text, const wxString& title)
{
    if (!taking() || !wxIsMainThread())
        return false;
    const ScopedCause cause(Cause::Kind::PrinterMessage, printer != nullptr ? printer->get_dev_id() : std::string(), {});
    take(TakenMessage::From::PrinterNotification, std::string(title.ToUTF8().data()), std::string(text.ToUTF8().data()));
    return true;
}

bool take_error(const std::string& text, wxWindow* parent)
{
    // A tool call or a file open has its own listener, which answers the box
    // when it opens.
    if (!taking() || !wxIsMainThread() || Workspace::dialog_listener_active())
        return false;
    if (current_cause().kind == Cause::Kind::None && another_window_is_open(parent, nullptr))
        return false;
    // The title of the ErrorDialog GUI::show_error opens.
    take(TakenMessage::From::ErrorBox, format(_u8L("%s error"), SLIC3R_APP_FULL_NAME), text);
    return true;
}

WindowKind window_kind(wxDialog& dialog)
{
    if (is_own_dialog(dialog))
        return WindowKind::Own;
    const Workspace::DialogFacts facts = Workspace::read_dialog(dialog);
    if (facts.picker)
        return WindowKind::Picker;
    if (facts.chooser)
        return WindowKind::Chooser;
    if (dynamic_cast<UnsavedChangesDialog*>(&dialog) != nullptr)
        return unsaved_use(dialog) == UnsavedUse::Other ? WindowKind::CopyQuestion : WindowKind::UnsavedChanges;
    if (dynamic_cast<TipsDialog*>(&dialog) != nullptr && !facts.yes && !facts.no)
        return WindowKind::Tip;
    const bool native = dynamic_cast<wxMessageDialogBase*>(&dialog) != nullptr;
    if (!native && dynamic_cast<MsgDialog*>(&dialog) == nullptr)
        return WindowKind::Window;
    const bool only_ok = facts.ok && !facts.yes && !facts.no && !facts.cancel && (native || count_buttons(dialog) == 1);
    if (!only_ok)
        return WindowKind::Question;
    if (current_cause().kind == Cause::Kind::None && another_window_is_open(dialog.GetParent(), &dialog))
        return WindowKind::MessageOverWindow;
    return WindowKind::Message;
}

std::optional<int> take_modal(wxDialog& dialog, bool shown_by_listener)
{
    if (!taking())
        return std::nullopt;
    const WindowKind kind = window_kind(dialog);
    if (kind == WindowKind::Own)
        return std::nullopt;
    if (shown_by_listener) {
        let_through(dialog, kind, "a listener showed it");
        return std::nullopt;
    }
    switch (kind) {
    case WindowKind::Own: return std::nullopt;
    case WindowKind::Picker:
    case WindowKind::Chooser: let_through(dialog, kind, "a picker"); return std::nullopt;
    case WindowKind::UnsavedChanges: return answer_unsaved_changes(dialog, unsaved_use(dialog));
    case WindowKind::CopyQuestion: let_through(dialog, kind, "a question about copying settings"); return std::nullopt;
    case WindowKind::Tip: {
        const Workspace::DialogFacts facts = Workspace::read_dialog(dialog);
        take(TakenMessage::From::Tip, facts.title, facts.text);
        return wxID_OK;
    }
    case WindowKind::Window: let_through(dialog, kind, "a window"); return std::nullopt;
    case WindowKind::Question: let_through(dialog, kind, "a question"); return std::nullopt;
    case WindowKind::MessageOverWindow: let_through(dialog, kind, "a message, with another window open"); return std::nullopt;
    case WindowKind::Message: {
        const Workspace::DialogFacts facts = Workspace::read_dialog(dialog);
        take(TakenMessage::From::Box, facts.title, facts.text);
        return wxID_OK;
    }
    }
    return std::nullopt;
}

void note_refused_page(const std::string& page) { take(TakenMessage::From::PageSwitch, {}, page); }

const std::vector<TakenMessage>& taken_messages() { return taken_store(); }
const std::vector<ShownWindow>&  shown_windows() { return shown_store(); }

} // namespace Slic3r::GUI::JusPrin::OrcaMessages
