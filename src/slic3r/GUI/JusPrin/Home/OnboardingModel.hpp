#pragma once

// The first-run sequence, Welcome → Printers → Home, as a pure step function.
// The Orca adapter stores the progress and supplies the facts; this model
// only decides which screen is in front.
//
// Every door on the welcome is a detour that comes back into the sequence:
// signing in waits on the welcome's card, Preferences closes back onto it,
// and "Just slice for now" skips the detour, not the sequence. Each ends by
// leaving the welcome. Back on the Printers step returns to the welcome, from
// whichever printer screen, and whatever the person finished on the welcome
// stays finished: it is stored as the agent's own settings, not here. Onboarding is done only once Home is
// reached; until then every launch starts at the step the person left.

#include <string>

namespace Slic3r::GUI::JusPrin::Home {

enum class OnboardingStatus { Unfinished, Completed };
enum class OnboardingStep { Hidden, Welcome, Printers };
// Which Printers screen: bring the printers from JusPrin 1 or from
// OrcaSlicer, what that import brought, or the printer screen with "Skip for
// now".
enum class PrintersScreen { V1Import, OrcaImport, ImportResult, AddPrinter };
// The Printers screen the person picked with one of its "Other ways" links:
// one of the two imports, or the printer screen. None shows the one the
// app picks by what it found.
enum class PrintersWay { None, V1Import, OrcaImport, AddPrinter };

struct OnboardingProgress
{
    OnboardingStatus status{OnboardingStatus::Unfinished};
    // The welcome was left through one of its doors. Back clears it.
    bool welcome_left{false};
    // An "Other ways" link. Back clears it.
    PrintersWay way{PrintersWay::None};
    // A printer was imported, added or assumed on the Printers step.
    bool printer_chosen{false};
};

struct OnboardingFacts
{
    // A file opened before the sequence finished set the printer, which the
    // chip names "from the file".
    bool printer_from_file{false};
    // A JusPrin 1 folder holds printers to bring: the one on this computer,
    // or one the person picked.
    bool v1_printers{false};
    // The same for an OrcaSlicer folder.
    bool orca_printers{false};
    // An import ran and its result is on screen until Continue, even once
    // it brought a printer.
    bool import_result_open{false};
};

OnboardingStep next_onboarding_step(const OnboardingProgress& progress, const OnboardingFacts& facts);
// The way the person picked, while it has something to show; otherwise
// JusPrin 1's import when its folder has printers, even with OrcaSlicer's
// too, then OrcaSlicer's, then the printer screen.
PrintersScreen printers_screen(const OnboardingProgress& progress, const OnboardingFacts& facts);

// What Home's Agent group shows once the sequence is over: the three doors
// while no agent is set up, the account and its credits once after signing
// in, and nothing otherwise.
enum class HomeAgentCard { None, Doors, Account };
HomeAgentCard home_agent_card(bool agent_configured, bool signed_in, bool account_card_dismissed);

const char* to_string(OnboardingStatus status);
const char* to_string(OnboardingStep step);
const char* to_string(PrintersScreen screen);
const char* to_string(PrintersWay way);
// None for a name it does not know, the empty one included.
PrintersWay printers_way_from_string(const std::string& name);
const char* to_string(HomeAgentCard card);

} // namespace Slic3r::GUI::JusPrin::Home
