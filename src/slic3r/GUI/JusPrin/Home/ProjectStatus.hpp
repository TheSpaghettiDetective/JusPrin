#pragma once

// What a saved project's card says about it, from the Sent and Printed events
// the project keeps. Pure: the events go in, a kind and the facts for its line
// come out, so the rule is tested with records rather than with a GUI. The
// adapter phrases the line.

#include "HomeSnapshot.hpp"
#include "slic3r/GUI/JusPrin/Agent/ManufacturingHistory.hpp"

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

struct LedgerStatus
{
    // Draft, Sent or Completed; Printing comes from a printer, not from the
    // events.
    ProjectStatusKind kind{ProjectStatusKind::Draft};
    // The presets the event named, as it recorded them; empty for a draft.
    std::string printer;
    std::string material;
    // When it was sent, or when the print ended: ISO 8601 in UTC.
    std::string at;
};

// The newer of the latest Sent event and the latest completed print decides:
// a completed print makes the project Completed, a Sent event makes it Sent,
// and neither leaves it a Draft. A failed or cancelled print is not an
// outcome the card reports.
LedgerStatus ledger_status(const std::vector<Agent::SentRecord>&          sent,
                           const std::vector<Agent::PhysicalPrintRecord>& prints);

}}}} // namespace Slic3r::GUI::JusPrin::Home
