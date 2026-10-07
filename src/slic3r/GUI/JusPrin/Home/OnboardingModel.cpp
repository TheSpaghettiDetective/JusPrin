#include "OnboardingModel.hpp"

namespace Slic3r::GUI::JusPrin::Home {

OnboardingStep next_onboarding_step(const OnboardingProgress& progress, const OnboardingFacts& facts)
{
    if (progress.status != OnboardingStatus::Unfinished)
        return OnboardingStep::Hidden;
    if (!progress.started)
        return OnboardingStep::Welcome;
    if (progress.partial_import_pending)
        return OnboardingStep::Profiles;
    if (!facts.profiles_available && !progress.profile_import_deferred)
        return OnboardingStep::Profiles;
    if (!facts.usable_setup && !progress.offline_example)
        return OnboardingStep::Setup;
    if (facts.usable_setup && !progress.offline_example && !progress.setup_confirmed)
        return OnboardingStep::ConfirmSetup;
    if (!facts.project_open)
        return OnboardingStep::Project;
    return OnboardingStep::Hidden;
}

const char* to_string(OnboardingStatus status)
{
    switch (status) {
    case OnboardingStatus::Completed: return "completed";
    case OnboardingStatus::Dismissed: return "dismissed";
    case OnboardingStatus::Unfinished: break;
    }
    return "unfinished";
}

const char* to_string(OnboardingStep step)
{
    switch (step) {
    case OnboardingStep::Welcome: return "welcome";
    case OnboardingStep::Profiles: return "profiles";
    case OnboardingStep::ConfirmSetup: return "confirm_setup";
    case OnboardingStep::Setup: return "setup";
    case OnboardingStep::Project: return "project";
    case OnboardingStep::Hidden: break;
    }
    return "hidden";
}

} // namespace Slic3r::GUI::JusPrin::Home
