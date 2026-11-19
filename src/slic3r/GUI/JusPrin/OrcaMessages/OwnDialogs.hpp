#pragma once

// JusPrin's own modal windows. Every modal window passes the hook in
// Workspace/DialogListeners.hpp, where a file open or a tool call answers
// OrcaSlicer's dialogs and the JusPrin view takes OrcaSlicer's messages
// (OrcaMessages.hpp). A window JusPrin opens for the person must reach them
// through all of that, so it opens through one of these calls, which mark it
// as JusPrin's own while it is shown.
//
// GUI thread only.

#include <wx/defs.h>
#include <wx/string.h>

class wxDialog;
class wxWindow;

namespace Slic3r::GUI::JusPrin::OrcaMessages {

// Shows `dialog` modally as JusPrin's own; returns what ShowModal returns.
int show_own_modal(wxDialog& dialog);

// JusPrin's own message box, OrcaSlicer's MessageDialog (MsgDialog.hpp);
// returns what ShowModal returns.
int show_own_message(wxWindow* parent, const wxString& text, const wxString& title, long style = wxOK | wxICON_ERROR);

// JusPrin's own error box, after the current event, as GUI::show_error opens
// OrcaSlicer's.
void show_own_error(wxWindow* parent, const wxString& text);

// Whether `dialog` was opened through one of the calls above.
bool is_own_dialog(const wxDialog& dialog);

} // namespace Slic3r::GUI::JusPrin::OrcaMessages
