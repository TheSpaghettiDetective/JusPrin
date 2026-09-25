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

// A spool the printer holds. Either field may be empty: a spool described
// without a colour is still a material the card can name.
struct SpoolEntry
{
    std::string material; // the filament type, such as "PLA"
    std::string colour;   // '#RRGGBB'
};

enum class PrinterState { Idle, Printing, Offline };

const char* to_string(PrinterState state);

// What a card stands for, which decides how the page asks about its actions.
// A named printer is one the person added and names in JusPrin; the page
// confirms a rename or removal itself. A device is a Bambu printer on the
// network or the account that no named printer stands for; its rename and
// removal are Orca's own dialogs, which confirm for themselves.
enum class PrinterKind { Named, Device };

const char* to_string(PrinterKind kind);

// What a card's connection line and dot rest on, apart from its words.
// Online and Offline are a Bambu device's: data in the last 30 seconds, or
// verified before and silent now. Connected is a print host's: an address is
// saved. None is neither, and draws no dot.
enum class ConnectionState { None, Online, Offline, Connected };

const char* to_string(ConnectionState state);

struct PrinterEntry
{
    std::string             id;
    std::string             name;
    PrinterKind             kind{PrinterKind::Named};
    // The card's menu. An action the card cannot offer is sent as false and
    // shown disabled, so the menu keeps its shape.
    bool                    can_open_settings{false};
    bool                    can_rename{false};
    bool                    can_remove{false};
    PrinterState            state{PrinterState::Idle};
    std::string             status_text;
    // Below zero whenever there is no job to report; only a printing printer
    // sends a bar.
    int                     progress_percent{-1};
    // The Connection row: the finished line, the state it rests on, and the
    // transport -- "lan", "cloud", "host", or empty.
    ConnectionState         connection_state{ConnectionState::None};
    std::string             connection_text;
    std::string             connection_kind;
    std::string             connection_action; // "reconnect", or empty for no button
    // A print host's address as saved, without its scheme.
    std::string             address;
    // The Model row, "X1 Carbon · 0.4 mm"; empty when neither is known.
    std::string             model_text;
    // The Loaded row; empty when nothing is known to be loaded.
    std::vector<SpoolEntry> spools;
    bool                    can_launch_monitor{false};
};

struct Snapshot
{
    bool                      dark{false};
    std::vector<ProjectEntry> projects;
    std::vector<PrinterEntry> printers;
    // A printer the conversation just added: the page leads with it and
    // draws attention to its card once. Empty for any other push.
    std::string               highlight_printer;
};

// The `state` payload: { appearance, projects, printers, highlightPrinter }.
nlohmann::json state_payload(const Snapshot& snapshot);


}}}} // namespace Slic3r::GUI::JusPrin::Home
