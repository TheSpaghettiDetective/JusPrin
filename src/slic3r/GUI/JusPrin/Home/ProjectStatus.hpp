#pragma once

// What a saved project's card says about it, from the G-code records
// the project keeps. Pure: the records go in, a kind and the facts for its line
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
    // records.
    ProjectStatusKind kind{ProjectStatusKind::Draft};
    // The presets the record named, as it recorded them; empty for a draft.
    std::string printer;
    std::string material;
    // When it was sent, or when the print ended: ISO 8601 in UTC.
    std::string at;
};

// The newer of the latest send and the latest finished print decides: a
// finished print makes the project Completed, a send makes it Sent, and
// neither leaves it a Draft. An export alone leaves it a Draft.
LedgerStatus ledger_status(const std::vector<Agent::GcodeRecord>& records);

}}}} // namespace Slic3r::GUI::JusPrin::Home
