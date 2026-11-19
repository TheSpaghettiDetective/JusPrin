#pragma once

// What OrcaSlicer tells the person while the JusPrin view is on screen.
//
// OrcaSlicer speaks in one of two ways. It opens a modal box in the same call
// that decided to speak, or it puts the message off: GUI::show_error,
// GUI_App::show_dialog and GUI_App::push_notification queue the box and
// return. JusPrin takes the message at the last point where its reason is
// still known. For a box opened at once that is the modal hook every dialog
// passes (Workspace/DialogListeners.hpp), because the code that asked is still
// running. For a message put off it is one line at the top of each of the
// three functions, which hands the message here while its caller is still on
// the stack.
//
// The reason is a cause, noted at the doors where work comes in: a printer's
// answer to a connection (Printers::record_connection_event), a printer's
// reply (push_notification is given the printer), the network plug-in's
// message (GUI_App::process_network_msg), a slice that ended in an error and a
// failed upload to a print host (JusPrin's listeners of those events, which
// run before OrcaSlicer's own). A person's click in OrcaSlicer's own menus has no cause.
//
// The rule at the hook, for a window no listener answered:
// - A file or folder picker, a window with fields, and a question with a
//   choice are the person's, and open as OrcaSlicer's.
// - A message with only OK is taken, unless another window is open over the
//   main one: a box then belongs with that window, where the person is looking.
// - "Transfer or discard changes", and the unsaved-changes question OrcaSlicer
//   asks before it reloads presets, are answered after a version of the
//   project is saved: the setting changes are dropped and the version keeps
//   them. When no version can be saved, nothing changes and the failure is
//   reported as unexpected.
// - JusPrin's own windows (OwnDialogs.hpp) are never taken.
//
// What becomes of a taken message is the view's: see MessageRoutes.hpp.
//
// GUI thread only.

#include <wx/string.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class wxDialog;
class wxWindow;

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI::JusPrin::OrcaMessages {

// What the app was handling when OrcaSlicer spoke. Noted by the code at the
// source, the only place that knows it.
struct Cause
{
    enum class Kind { None, PrinterConnection, PrinterMessage, PluginMessage, Slice, HostUpload };
    Kind        kind{Kind::None};
    std::string printer; // a Bambu device id, or a print host's address
    std::string detail;  // the plug-in's message code, the connection's answer
    // The code that noted the cause tells the person itself: the print issues
    // tell a failed slice, the printer chat the answer to the connection it
    // is waiting for. Not shown again.
    bool        told_elsewhere{false};
};
const char* cause_name(Cause::Kind kind);

// The cause of what is said while this lives.
class ScopedCause
{
public:
    ScopedCause(Cause::Kind kind, std::string printer, std::string detail);
    ~ScopedCause();
    ScopedCause(const ScopedCause&)            = delete;
    ScopedCause& operator=(const ScopedCause&) = delete;

private:
    Cause        m_cause;
    ScopedCause* m_outer{nullptr};
    friend Cause current_cause();
};

// The cause until the event now being handled has ended: for a listener that
// returns before OrcaSlicer's own handler of the same event runs.
void  note_cause(Cause cause);
Cause current_cause();

// One thing OrcaSlicer wanted to tell the person, taken before its window
// opened, with the reason it was said.
struct TakenMessage
{
    enum class From { Box, ErrorBox, AppDialog, PrinterNotification, UnsavedChanges, Tip, PageSwitch };
    From        from{From::Box};
    Cause       cause;
    std::string title;
    std::string text;
};
const char* from_name(TakenMessage::From from);

// What the rule makes of a window.
enum class WindowKind { Own, Picker, Chooser, UnsavedChanges, CopyQuestion, Tip, Window, Question, Message, MessageOverWindow };
WindowKind  window_kind(wxDialog& dialog);
const char* window_kind_name(WindowKind kind);

// A window of OrcaSlicer's that reached the person while the view takes
// messages, what the rule made of it, and why it was shown.
struct ShownWindow
{
    std::string window_class;
    std::string title;
    WindowKind  kind{WindowKind::Window};
    std::string why;
};

// What the view gives while it is installed.
struct View
{
    // Called before OrcaSlicer drops setting changes that sit on a profile:
    // saves a version that keeps them, or reports why it could not and
    // returns false, and then nothing is dropped. The same function guards a
    // project's close (ShellController::save_before_dropping_changes).
    std::function<bool()> save_before_dropping_changes;
    // Told of each message as it is taken.
    std::function<void(const TakenMessage&)> show;
};
void install(View view);
void uninstall();

// A switch that changes OrcaSlicer's state before OrcaSlicer asks about the
// changes it drops saves the version itself, before that change: the sidebar
// sequence a filament switch runs renames the filament slot first, and a
// version saved at the question would keep the new filament's name with the
// old one's settings. While this scope lives the question is answered without
// saving again. False when no version could be saved: the view has reported
// why, and the switch must not go on. Saves nothing while not taking.
class SavedFirst
{
public:
    SavedFirst();
    ~SavedFirst();
    SavedFirst(const SavedFirst&)            = delete;
    SavedFirst& operator=(const SavedFirst&) = delete;
    explicit operator bool() const { return m_saved; }

private:
    bool m_saved{true};
    bool m_holding{false};
};
// Installed, and taking messages.
bool taking();
// Tests only: turns the taking off and on while the view stays installed, so
// OrcaSlicer's own windows open as they do in the classic view.
void set_taking_for_tests(bool on);

// -- The hand-over points, called from OrcaSlicer's files through
//    OrcaHooks.hpp. Each returns true when the message was taken, and
//    OrcaSlicer's window must not open. ------------------------------------

// GUI_App::process_network_msg: what a box opened below it is about.
[[nodiscard]] ScopedCause plugin_message_cause(const std::string& device_id, const std::string& code);
// GUI_App::show_dialog.
bool take_app_message(const wxString& text);
// GUI_App::push_notification, with the printer it was given.
bool take_printer_message(const MachineObject* printer, const wxString& text, const wxString& title);
// GUI::show_error, through report_error_box (Workspace/FileLoads.hpp).
bool take_error(const std::string& text, wxWindow* parent);

// The modal hook's rule for a window no listener answered: the id ShowModal
// returns instead of showing it, or nothing to show it. A window a listener
// chose to show (`shown_by_listener`) is shown, and written down with what
// the rule would have made of it.
std::optional<int> take_modal(wxDialog& dialog, bool shown_by_listener);

// A switch of the main window to OrcaSlicer's page `page` was refused.
void note_refused_page(const std::string& page);

// Everything taken and let through in this run, for tests.
const std::vector<TakenMessage>& taken_messages();
const std::vector<ShownWindow>&  shown_windows();


} // namespace Slic3r::GUI::JusPrin::OrcaMessages
