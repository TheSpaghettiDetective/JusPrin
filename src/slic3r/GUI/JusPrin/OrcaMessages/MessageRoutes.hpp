#pragma once

// Where the JusPrin view shows a message it took from OrcaSlicer
// (OrcaMessages.hpp): in the project chat, as an entry of the kind the app's
// own work leaves there (Agent::AgentHost::begin_work), ended failed and
// carrying OrcaSlicer's words. The entry goes to the chat on screen, or to
// the empty chat when none is open, and the chat pane opens for it. On Home
// the pane is not shown; the entry is saved with the project and is there
// when the person opens it.
//
// - One about a printer (its answer to a connection, its reply to a command)
//   is titled with the printer's name as JusPrin names it.
// - A failed upload ends the upload's own entry, which follows OrcaSlicer's
//   progress notification, instead of adding a second one.
// - Any other message is titled as OrcaSlicer's window would have been.
// - One whose cause tells the person itself (Cause::told_elsewhere): nothing
//   more.
// - The answered "Transfer or discard changes", a tip, a refused page switch:
//   nothing more.
//
// GUI thread only.

#include "OrcaMessages.hpp"

#include <functional>
#include <string>

namespace Slic3r::GUI::JusPrin::OrcaMessages {

struct Routes
{
    // The chat's entry for work outside it: begin returns its id, finish
    // ends it with a state (done, cancelled, failed) and a detail.
    std::function<std::string(const std::string& kind, const std::string& title)>                    begin_entry;
    std::function<void(const std::string& id, const std::string& state, const std::string& detail)> finish_entry;
    // Ends the entry of the upload whose notification has these words as
    // failed with `detail`; false when there is none.
    std::function<bool(const std::string& notice_text, const std::string& detail)> fail_upload;
};

// The kinds of chat entry a taken message makes.
inline constexpr const char* kPrinterEntryKind = "printer";
inline constexpr const char* kMessageEntryKind = "message";

void route(const TakenMessage& message, const Routes& routes);

// The name the person knows the printer with this Bambu device id by: the
// named printer linked to it, else the name the printer gives itself, else
// the id.
std::string printer_name(const std::string& device_id);

} // namespace Slic3r::GUI::JusPrin::OrcaMessages
