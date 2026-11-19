# OrcaSlicer's messages in the JusPrin view

OrcaSlicer tells the person things in windows: a box with OK, a question, the
"Transfer or discard changes" window, a tip. In the JusPrin view most of those
windows would cover a screen built around the project chat, and many of them
are about pages the view hides. JusPrin takes each such message before its
window opens, keeps the reason it was said, and shows it where the view shows
that kind of news. The code is in `src/slic3r/GUI/JusPrin/OrcaMessages/`.

## Where a message is taken

OrcaSlicer speaks in one of two ways.

- It opens a modal window inside the call that decided to speak. Every modal
  window passes wxWidgets' modal hook, where JusPrin's dialog listeners
  already answer the windows a tool call or a file open raises
  (`Workspace/DialogListeners.hpp`). A window no listener answered goes to
  `OrcaMessages::take_modal`, while the code that asked is still on the stack.
- It puts the message off. `GUI::show_error`, `GUI_App::show_dialog` and
  `GUI_App::push_notification` queue the window and return, so by the time it
  opens the reason is gone. One line at the top of each of the three functions
  hands the message over first: `report_error_box` (`Workspace/FileLoads.hpp`),
  `take_app_message` and `take_printer_message`. A fourth line in
  `GUI_App::process_network_msg` notes that what follows is the network
  plug-in's message. The lines reach JusPrin through `JusPrin/OrcaHooks.hpp`.

Those four lines are the fork's whole footprint in OrcaSlicer's files for this
feature. Each is one neutral statement marked `// JusPrin:`.

## The cause

A message is only as useful as its reason. The reason, a `Cause`, is noted at
the doors where work comes in, by code that knows it:

| Cause | Noted by |
| --- | --- |
| A printer's answer to a connection | `Printers::record_connection_event`, called from OrcaSlicer's connection handler before it opens its box |
| A printer's reply to a command | `take_printer_message`, which `push_notification` gives the printer |
| The network plug-in's message | `plugin_message_cause`, at the top of `process_network_msg` |
| A slice that ended in an error | JusPrin's listener of the finished slice, which runs before OrcaSlicer's |
| A failed upload to a print host | JusPrin's listener of the upload queue's error event, which notes the words of the upload's notification |

A cause noted by a listener lasts until the event being handled ends
(`note_cause`); one noted by a scope lasts while the scope does
(`ScopedCause`). A person's click in OrcaSlicer's own menus has no cause.

A cause can say that the code that noted it tells the person itself
(`told_elsewhere`): the print issues tell a failed slice, and the printer chat
tells the answer to a connection it started and is waiting for
(`Printers::expect_connection_answer`). Such a message is taken and shown
nowhere else, so the person does not read it twice.

## The rule at the hook

For a window no listener answered, while the view is installed:

- A file or folder picker and the chooser of a ZIP's files are the person's and
  open as OrcaSlicer's. A listener of a file the person opens themself shows
  them too, rather than cancelling them.
- A window with fields, and a question with more than one answer, are the
  person's and open.
- A message with only OK is taken, unless another window is open over the main
  one, such as OrcaSlicer's settings window or Preferences, and the message has
  no cause. The box then belongs with that window, where the person is
  looking.
- "Transfer or discard changes", and the "unsaved changes" question OrcaSlicer
  asks before it reloads profiles (`check_and_save_current_preset_changes`,
  `check_and_keep_current_preset_changes`), are answered after a version of
  the project is saved to its history: the setting changes are dropped and
  the version keeps them. The person never chooses between saving and
  discarding; version history is the way back. If no version can be saved,
  the window is cancelled, so nothing changes, and the failure is reported as
  an unexpected error. Closing the project follows the same rule through the
  same function, `ShellController::save_before_dropping_changes`, which
  Plater's hook before a project is released also calls: whenever OrcaSlicer
  is about to drop setting changes that sit on a profile, a version is saved
  first, or nothing is dropped and nothing closes. Restoring that version
  from Version history brings back the profiles, their unsaved changes and
  the model as they were just before.
- A switch from the header's printer or filament menu leaves one line in the
  project chat (`StatusRow::on_printer_switched`, `on_filament_visit`) that
  says the unsaved setting changes were dropped and that the settings now
  follow the newly selected profiles: all of them for a printer, the
  filament's for a filament. The model reads that line, in English, as a
  developer message on its next request. Without it, the chat's history
  still holds the model's own result saying its change was applied, and
  nothing in the request contradicts it. The line names no settings: the app
  does not keep a list of what was dropped.
- The version must hold the project as it was before the switch began, not
  halfway through it. A filament switch runs the sidebar's sequence
  (`SetupCommands::select_filament_preset`), which renames the filament slot
  before OrcaSlicer asks, so a version saved at the question would name the
  new filament with the old one's settings. That switch saves first itself,
  before the slot changes (`OrcaMessages::SavedFirst`), and the question that
  follows is answered without saving again. The printer and process switches
  ask before they change anything.
- The same window class asks whether to copy one nozzle's settings to the
  other on a two-nozzle printer (`Tab::sync_excluder`). That is a question
  about a copy and stays the person's. OrcaSlicer tells the three uses apart
  only by their titles, so JusPrin compares the titles as OrcaSlicer builds
  them.
- A tip window is taken.
- JusPrin's own windows are never taken. They open through
  `OrcaMessages/OwnDialogs.hpp` (`show_own_modal`, `show_own_message`,
  `show_own_error`), which marks them as JusPrin's while they are shown.

The view also refuses a switch of the main window to any page other than Home,
Prepare and Project (`ShellController::on_notebook_page_changing`), so code
that jumps to OrcaSlicer's Device page, or the key that steps through its
pages, leaves the person where they are.

## Where a taken message is shown

Every taken message that is not told elsewhere goes into the project chat,
the same way for every cause (`OrcaMessages/MessageRoutes.hpp`). If a chat is
open the entry is added to it; otherwise it starts the chat, which is what
the empty chat panel shows. The entry is the kind the app's own work leaves
there (`Agent::AgentHost::begin_work`), marked failed and carrying
OrcaSlicer's words; the chat card shows an entry's words only in that state.
The chat pane opens for it. The entry is saved with the project, so a message
that comes while the person is on Home, where the chat pane is not shown, is
in the chat when they open the project.

- A message about a printer, a connection's answer or a command's reply, is
  titled with the printer's name as JusPrin names it.
- A failed upload to a print host ends the upload's own entry as failed,
  rather than adding a second one.
- Any other message is titled as OrcaSlicer's window would have been: "Info"
  for `GUI_App::show_dialog`, "<app> error" for `GUI::show_error`, the box's
  own title for a modal box.
- A message told elsewhere, an answered unsaved-changes question, a tip, a
  refused page switch: nothing more.

Nothing taken is drawn on the canvas. The words are OrcaSlicer's, unchanged
and translated by its catalogs.

## Testing

`JusPrinShellHarness --orca-messages <dir>` makes OrcaSlicer speak through
its real code in each of these ways and checks which window opened, what was
taken with which cause, and where it was shown. `--classic-messages` turns the
taking off while the view stays installed (`set_taking_for_tests`), so the same
run shows OrcaSlicer's windows as the classic view does. Every harness mode
fails its run if, while the view takes messages, an OrcaSlicer message with
only OK or an unsaved-changes question reached the person, or a printer
message was taken without its printer.
