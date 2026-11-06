#pragma once

// The first-run sequence, Welcome → Printers → Home, as a pure step function.
// The Orca adapter stores the progress and supplies the facts; this model
// only decides which screen is in front.
//
// Every door on the welcome is a detour that comes back into the sequence:
// signing in waits on the welcome's card, Preferences closes back onto it,
// and "Just slice for now" skips the detour, not the sequence. Each ends by
// leaving the welcome. Back on the Printers step returns to the welcome (from
// the add-printer form that Start fresh opened, to the OrcaSlicer screen), and
// whatever the person finished on the welcome stays finished: it is stored as
// the agent's own settings, not here. Onboarding is done only once Home is
// reached; until then every launch starts at the step the person left.

namespace Slic3r::GUI::JusPrin::Home {

enum class OnboardingStatus { Unfinished, Completed };
enum class OnboardingStep { Hidden, Welcome, Printers };
// Which Printers screen: bring the printers from OrcaSlicer, what that
// import brought, or the printer screen with "Skip for now".
enum class PrintersScreen { OrcaImport, ImportResult, AddPrinter };

struct OnboardingProgress
{
    OnboardingStatus status{OnboardingStatus::Unfinished};
    // The welcome was left through one of its doors. Back clears it.
    bool welcome_left{false};
    // "Start fresh" on the OrcaSlicer screen: the add-printer form instead.
    bool fresh_start{false};
    // A printer was imported, added or assumed on the Printers step.
    bool printer_chosen{false};
};

struct OnboardingFacts
{
    // A JusPrin v1 import ran: its own window greets them over Home and
    // brought their printers, so the Printers step is skipped.
    bool v1_imported{false};
    // A file opened before the sequence finished set the printer, which the
    // chip names "from the file".
    bool printer_from_file{false};
    // An OrcaSlicer folder on this computer holds printers to bring.
    bool orca_printers{false};
    // An OrcaSlicer import ran and its result is on screen until Continue,
    // even once it brought a printer.
    bool import_result_open{false};
};

OnboardingStep next_onboarding_step(const OnboardingProgress& progress, const OnboardingFacts& facts);
PrintersScreen printers_screen(const OnboardingProgress& progress, const OnboardingFacts& facts);

// What Home's Agent group shows once the sequence is over: the three doors
// while no agent is set up, the account and its credits once after signing
// in, and nothing otherwise.
enum class HomeAgentCard { None, Doors, Account };
HomeAgentCard home_agent_card(bool agent_configured, bool signed_in, bool account_card_dismissed);

const char* to_string(OnboardingStatus status);
const char* to_string(OnboardingStep step);
const char* to_string(PrintersScreen screen);
const char* to_string(HomeAgentCard card);

} // namespace Slic3r::GUI::JusPrin::Home
