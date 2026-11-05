#include "OnboardingModel.hpp"

namespace Slic3r::GUI::JusPrin::Home {

OnboardingStep next_onboarding_step(const OnboardingProgress& progress, const OnboardingFacts& facts)
{
    if (progress.status != OnboardingStatus::Unfinished)
        return OnboardingStep::Hidden;
    // The welcome shows once whatever is already set up: a v1 account, a
    // key, or a file opened at launch.
    if (!progress.welcome_left)
        return OnboardingStep::Welcome;
    if (facts.import_result_open)
        return OnboardingStep::Printers;
    // The app needs one printer, and each of these brought one.
    if (progress.printer_chosen || facts.v1_imported || facts.printer_from_file)
        return OnboardingStep::Hidden;
    return OnboardingStep::Printers;
}

PrintersScreen printers_screen(const OnboardingProgress& progress, const OnboardingFacts& facts)
{
    if (facts.import_result_open)
        return PrintersScreen::ImportResult;
    return facts.orca_printers && !progress.fresh_start ? PrintersScreen::OrcaImport : PrintersScreen::AddPrinter;
}

HomeAgentCard home_agent_card(bool agent_configured, bool signed_in, bool account_card_dismissed)
{
    if (!agent_configured)
        return HomeAgentCard::Doors;
    return signed_in && !account_card_dismissed ? HomeAgentCard::Account : HomeAgentCard::None;
}

const char* to_string(OnboardingStatus status)
{
    return status == OnboardingStatus::Completed ? "completed" : "unfinished";
}

const char* to_string(OnboardingStep step)
{
    switch (step) {
    case OnboardingStep::Welcome: return "welcome";
    case OnboardingStep::Printers: return "printers";
    case OnboardingStep::Hidden: break;
    }
    return "hidden";
}

const char* to_string(PrintersScreen screen)
{
    switch (screen) {
    case PrintersScreen::OrcaImport: return "orca_import";
    case PrintersScreen::ImportResult: return "import_result";
    case PrintersScreen::AddPrinter: break;
    }
    return "add_printer";
}

const char* to_string(HomeAgentCard card)
{
    switch (card) {
    case HomeAgentCard::Doors: return "doors";
    case HomeAgentCard::Account: return "account";
    case HomeAgentCard::None: break;
    }
    return "none";
}

} // namespace Slic3r::GUI::JusPrin::Home
