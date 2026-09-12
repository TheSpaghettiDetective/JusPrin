#pragma once

// The data behind the Home page, and the one function that turns it into the
// `state` payload of the jusprin-home-bridge protocol. No wx and no Orca types
// appear here, so the payload's shape is testable without a GUI: everything
// that needs a MainFrame, a PresetBundle, or the device layer lives in
// HomeHost, which fills these structs and hands them over.
//
// Optional fields are omitted from the JSON rather than sent as null or as an
// empty string: the page distinguishes "the host has nothing to say" from "the
// host says it is empty", and a serialised null would read as the latter.

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

// Matches ProjectStatusKind in the page's protocol.ts. 'unknown' is what the
// host sends while nothing in the codebase records per-project slice state:
// the text carries what can be derived instead, and only 'printing' earns a
// colored dot on the card.
enum class ProjectStatusKind { Unknown, Draft, Sliced, Printing, Completed };

const char* to_string(ProjectStatusKind kind);

struct ProjectEntry
{
    std::string        id;
    std::string        name;
    std::string        path;
    std::string        thumbnail_url; // empty when the .3mf carries none
    ProjectStatusKind  status_kind{ProjectStatusKind::Unknown};
    std::string        status_text;
};

struct SpoolEntry
{
    std::string colour; // '#RRGGBB'
};

enum class PrinterState { Idle, Printing, Offline };

const char* to_string(PrinterState state);

struct PrinterEntry
{
    std::string             id;
    std::string             name;
    PrinterState            state{PrinterState::Idle};
    std::string             status_text;
    // Below zero whenever there is no job to report; only a printing printer
    // sends a bar.
    int                     progress_percent{-1};
    std::string             connection_text;
    std::string             nozzle_text;
    std::string             material_label;
    std::vector<SpoolEntry> spools;
    bool                    can_launch_monitor{false};
};

struct Snapshot
{
    bool                      dark{false};
    std::vector<ProjectEntry> projects;
    std::vector<PrinterEntry> printers;
};

// The `state` payload: { appearance, projects, printers }.
nlohmann::json state_payload(const Snapshot& snapshot);

}}}} // namespace Slic3r::GUI::JusPrin::Home
