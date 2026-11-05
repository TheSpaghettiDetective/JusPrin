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

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

// Matches ProjectStatusKind in the page's protocol.ts. 'unknown' is a project
// whose records could not be read, and has no line; only 'printing' earns a
// colored dot on the card.
enum class ProjectStatusKind { Unknown, Draft, Sent, Printing, Completed };

const char* to_string(ProjectStatusKind kind);

// What a card or the project pane says about a print history: how much went
// out, how much printed, and how it stands. The page phrases it.
struct HistorySummary
{
    std::size_t gcodes{0};
    std::size_t exports{0};
    std::size_t sends{0};
    std::size_t prints{0};
    std::size_t printing_now{0};
    // The newest finish, ISO 8601 in UTC; empty when nothing finished.
    std::string last_finished_at;

    bool empty() const { return gcodes == 0; }
};

// { gcodes, exports, sends, prints, printingNow, lastFinishedAt? }
nlohmann::json summary_json(const HistorySummary& summary);

struct ProjectEntry
{
    std::string        id;
    std::string        name;
    std::string        path;
    std::string        thumbnail_url; // empty when no preview is available
    // When the model was last saved, ISO 8601 in UTC; the page says it as
    // "Today", "Yesterday" or a date.
    std::string        updated_at;
    ProjectStatusKind  status_kind{ProjectStatusKind::Unknown};
    std::string        status_text;
    // The card's print-history line; an empty one draws none.
    HistorySummary     history;
};

// A spool the printer holds, as a connected printer reports its tray. Either
// field may be empty: a tray that reports no colour is still a material the
// card can name.
struct SpoolEntry
{
    std::string material; // the filament type, such as "PLA"
    std::string colour;   // '#RRGGBB'
};

// What a card says the printer is doing, in one word. Matches PrinterStatus
// in the page's protocol.ts. Each is said only on something the app read:
// NotConnected when it has no connection or none answers, Connecting before
// the first answer, Connected when the printer answers but does not say what
// it is doing, and the rest when it says.
enum class PrinterStatus { NotConnected, Connecting, Connected, Ready, Printing, Paused, NeedsAttention };

const char* to_string(PrinterStatus status);

// How the printer says its last print ended; Unknown when it does not.
enum class LastPrint { Unknown, Finished, Failed, Cancelled };

const char* to_string(LastPrint last);

// The card's one button: a Bambu printer's monitor, a print host's own page
// in a window, Reconnect for a connection that went quiet, or Connect for a
// printer with none.
enum class PrinterAction { None, Monitor, PrinterWindow, Reconnect, Connect };

const char* to_string(PrinterAction action);

// What a card stands for, which decides how the page asks about its actions.
// A named printer is one the person added and names in JusPrin; the page
// confirms a rename or removal itself. A device is a Bambu printer on the
// network or the account that no named printer stands for; its rename and
// removal are Orca's own dialogs, which confirm for themselves.
enum class PrinterKind { Named, Device };

const char* to_string(PrinterKind kind);

// One printer card. Every field but the name and the status is shown only
// when it has a value, so the card grows and shrinks with what the printer
// reports.
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
    PrinterStatus           status{PrinterStatus::NotConnected};
    // The job, while one runs: its name as the printer gives it, how far it
    // is, and the seconds left; each below zero or empty when not reported.
    std::string             job;
    int                     progress_percent{-1};
    int                     seconds_left{-1};
    // The printer's own words about what needs doing, or, on a card that is
    // not connected, why; already translated where they are the app's.
    std::string             message;
    // On a Ready card.
    LastPrint               last_print{LastPrint::Unknown};
    // When the app last heard from it, ISO 8601 in UTC; on a card that is not
    // connected, and empty when the app does not know.
    std::string             last_connected_at;
    // A camera the page can show: an MJPEG stream, or one picture to fetch
    // again and again; empty for none.
    std::string             camera_stream_url;
    std::string             camera_snapshot_url;
    // The facts line, "LAN · X1 Carbon · 0.4 mm": how it is reached ("LAN",
    // "Cloud", "Klipper", "OctoPrint" ...), a print host's address as saved
    // without its scheme, and "X1 Carbon · 0.4 mm". Each part empty when
    // unknown.
    std::string             connection_label;
    std::string             address;
    std::string             model_text;
    // What is loaded, when the printer says; empty otherwise.
    std::vector<SpoolEntry> spools;
    PrinterAction           action{PrinterAction::None};
    // The card's print-history line: what was made for, sent to and printed
    // on this printer, across projects. An empty one draws none.
    HistorySummary          history;
};

// The project gallery: the saved projects, or why they could not be listed.
struct ProjectList
{
    std::vector<ProjectEntry> projects;
    // Already translated; set when the projects folder could not be read, and
    // shown instead of the empty gallery.
    std::string               problem;
};

struct Snapshot
{
    bool                      dark{false};
    std::vector<ProjectEntry> projects;
    std::string               projects_problem; // ProjectList::problem
    std::vector<PrinterEntry> printers;
    // A printer the conversation just added: the page leads with it and
    // draws attention to its card once. Empty for any other push.
    std::string               highlight_printer;
};

// The `state` payload: { appearance, projects, projectsProblem, printers, highlightPrinter }.
nlohmann::json state_payload(const Snapshot& snapshot);


}}}} // namespace Slic3r::GUI::JusPrin::Home
